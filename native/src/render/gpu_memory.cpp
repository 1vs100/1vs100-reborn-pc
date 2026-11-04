// Range tracker plus the resource-lifetime observer hooks. Each hook calls through first
// so the runtime's own header writes land before the range is read back out of the header.

#include "render/gpu.h"
#include "render/gpu_memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include <rex/graphics/xenos.h>
#include <rex/hash.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "render/gpu_texture_decode.h"
#include "render/gpu_tracker.h"
#include "render/guest/d3d_device.h"

extern "C" REX_FUNC(__imp__sub_82538150);
extern "C" REX_FUNC(__imp__sub_82538218);
extern "C" REX_FUNC(__imp__sub_82538228);
extern "C" REX_FUNC(__imp__sub_82538DD0);
extern "C" REX_FUNC(__imp__sub_82539330);
extern "C" REX_FUNC(__imp__sub_82539380);
extern "C" REX_FUNC(__imp__sub_8253A700);
extern "C" REX_FUNC(__imp__sub_8253A7D8);
extern "C" REX_FUNC(__imp__sub_8253B0E0);
extern "C" REX_FUNC(__imp__sub_8276E158);
extern "C" REX_FUNC(__imp__sub_8276E1C0);
extern "C" REX_FUNC(__imp__sub_8276E0E8);

namespace onevs100::render::memory {

struct Range {
  const uint32_t end;
  std::atomic<uint32_t> generation;
  std::atomic<bool> active{true};

  Range(uint32_t end, uint32_t generation) : end(end), generation(generation) {}
};

namespace {

namespace xenos = rex::graphics::xenos;

std::mutex g_mutex;
std::map<uint32_t, std::shared_ptr<Range>> g_ranges;  // registered, keyed by base, non-overlapping
uint32_t g_generation = 1;
std::atomic<uint64_t> g_revision{1};

template <typename T>
const T* Guest(uint32_t address) {
  return REX_KERNEL_MEMORY()->TranslateVirtual<const T*>(address);
}

// Erases registered ranges overlapping [base, end) and returns whether any existed.
void EraseOverlaps(uint32_t base, uint32_t end) {
  auto it = g_ranges.upper_bound(base);
  if (it != g_ranges.begin()) {
    auto prev = std::prev(it);
    if (prev->second->end > base) it = prev;
  }
  while (it != g_ranges.end() && it->first < end) {
    it->second->active.store(false, std::memory_order_release);
    it = g_ranges.erase(it);
  }
}

// Physical data ranges of a resource header: up to two (texture base and mips).
int ResourceRanges(uint32_t resource_address, uint32_t (&base)[2], uint32_t (&size)[2]) {
  if (!resource_address) return 0;
  const auto* resource = Guest<guest::D3DResource>(resource_address);
  const uint32_t type = uint32_t(resource->Common) & 0xF;
  if (type == 1) {
    const auto* vb = Guest<guest::D3DVertexBuffer>(resource_address);
    base[0] = uint32_t(vb->Format[0]) & ~3u;
    size[0] = uint32_t(vb->Format[1]) & 0x3FFFFFC;
    // Engine-built headers over XPhysicalAlloc blocks hold the CPU view of the data; the draw reads the fetch
    // constant's physical address, so the range is keyed the way the draw looks it up.
    if (base[0] >= 0x20000000) {
      const uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(base[0]);
      base[0] = physical != UINT32_MAX ? physical : base[0] & 0x1FFFFFFF;
    }
    return base[0] && size[0] ? 1 : 0;
  }
  if (type == 2) {
    // Index buffers carry a virtual address (DrawIndexedVertices converts it for the GPU), so they are keyed by it.
    const auto* ib = Guest<guest::D3DIndexBuffer>(resource_address);
    base[0] = ib->Address;
    size[0] = ib->Size;
    return base[0] && size[0] ? 1 : 0;
  }
  if (type == 3) {
    const auto* texture = Guest<guest::D3DBaseTexture>(resource_address);
    xenos::xe_gpu_texture_fetch_t fetch;
    for (int i = 0; i < 6; ++i) (&fetch.dword_0)[i] = texture->Format.dword[i];
    uint32_t n = 0;
    uint32_t b, bs, m, ms;
    if (TextureMemory(fetch, b, bs, m, ms)) {
      if (b && bs) base[n] = b, size[n++] = bs;
      if (m && ms) base[n] = m, size[n++] = ms;
    }
    return int(n);
  }
  return 0;
}

}  // namespace

void Register(uint32_t base, uint32_t size) {
  if (!base || !size) return;
  std::lock_guard lock(g_mutex);
  // A header for a piece of a registered block keeps the block registered; only a range that does not fit inside
  // an existing one replaces what it overlaps.
  auto it = g_ranges.upper_bound(base);
  if (it != g_ranges.begin()) {
    auto prev = std::prev(it);
    if (prev->first <= base && prev->second->end >= base + size) {
      prev->second->generation.store(++g_generation, std::memory_order_release);
      return;
    }
  }
  EraseOverlaps(base, base + size);
  g_ranges[base] = std::make_shared<Range>(base + size, ++g_generation);
  g_revision.fetch_add(1, std::memory_order_release);
}

void MarkDirty(uint32_t base, uint32_t size) {
  if (!base || !size) return;
  std::lock_guard lock(g_mutex);
  const uint32_t end = base + size;
  auto it = g_ranges.upper_bound(base);
  if (it != g_ranges.begin()) {
    auto prev = std::prev(it);
    if (prev->second->end > base) it = prev;
  }
  for (; it != g_ranges.end() && it->first < end; ++it) {
    it->second->generation.store(++g_generation, std::memory_order_release);
  }
}

RangeState Query(uint32_t base, uint32_t size) {
  thread_local std::array<QueryCache, 512> cache;
  const uint64_t key = (uint64_t(base) << 32) | size;
  auto& cached = cache[XXH3_64bits(&key, sizeof(key)) & (cache.size() - 1)];
  return Query(base, size, cached);
}

RangeState Query(uint32_t base, uint32_t size, QueryCache& cached) {
  const uint64_t key = (uint64_t(base) << 32) | size;
  if (cached.key == key) {
    if (cached.range && cached.range->active.load(std::memory_order_acquire)) {
      return {true, cached.range->generation.load(std::memory_order_acquire)};
    }
    if (!cached.range && cached.revision == g_revision.load(std::memory_order_acquire)) return {};
  }

  // Cached owners publish dirty versions directly; replacement retires the old owner before erasing it.
  // Misses follow the map revision so a new registration cannot leave an unowned result cached.
  std::lock_guard lock(g_mutex);
  auto it = g_ranges.upper_bound(base);
  std::shared_ptr<Range> range;
  if (it != g_ranges.begin()) {
    --it;
    if (it->second->end >= base + size) range = it->second;
  }
  cached = {key, g_revision.load(std::memory_order_relaxed), std::move(range)};
  return cached.range ? RangeState{true, cached.range->generation.load(std::memory_order_relaxed)} : RangeState{};
}

void RegisterResource(uint32_t resource_address) {
  if (!resource_address || !ValidGuest(resource_address)) {
    NoteBadGuest(resource_address, "RegisterResource");
    return;
  }
  uint32_t base[2], size[2];
  const int n = ResourceRanges(resource_address, base, size);
  for (int i = 0; i < n; ++i) {
    tracker::InvalidateResolved(base[i], size[i]);
    Register(base[i], size[i]);
  }
}

void DirtyResource(uint32_t resource_address) {
  if (!resource_address || !ValidGuest(resource_address)) {
    NoteBadGuest(resource_address, "DirtyResource");
    return;
  }
  uint32_t base[2], size[2];
  const int n = ResourceRanges(resource_address, base, size);
  for (int i = 0; i < n; ++i) {
    tracker::InvalidateResolved(base[i], size[i]);
    MarkDirty(base[i], size[i]);
  }
}

}  // namespace onevs100::render::memory

namespace memory = onevs100::render::memory;

// D3DDevice_CreateVertexBuffer 0x82538150 (Length, Usage, Pool) returns the buffer.
