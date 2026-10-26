// Gameserver mode: QoS probes complete at once with fixed good values, as with
// reborn. Results come from the system heap, which XNetQosRelease frees.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <algorithm>
#include <cstring>
#include "online_service.h"
#include "runtime_export.h"

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

// XNQOS: count, pending count, then one 24-byte XNQOSINFO per target.
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kInfoSize = 24;
constexpr uint8_t kInfoComplete = 0x01, kInfoTargetContacted = 0x02;
constexpr uint8_t kQosData = 'A';
constexpr uint32_t kBitsPerSec = 10 * 1024 * 1024 * 8;

// Builds a completed result for `targets` entries, stores it at `out` and sets `event`.
void CompleteQos(uint8_t* base, uint32_t targets, uint32_t event, uint32_t out) {
  if (out) {
    targets = std::max<uint32_t>(targets, 1);
    const uint32_t size = kHeaderSize + targets * kInfoSize + 1;
    const uint32_t qos = REX_KERNEL_MEMORY()->SystemHeapAlloc(size);
    const uint32_t data = qos + size - 1;
    std::memset(base + qos, 0, size);
    base[data] = kQosData;
    store_and_swap<uint32_t>(base + qos, targets);
    for (uint32_t i = 0; i < targets; ++i) {
      uint8_t* info = base + qos + kHeaderSize + i * kInfoSize;
      info[0] = kInfoComplete | kInfoTargetContacted;
      store_and_swap<uint16_t>(info + 2, 8);   // probes sent
      store_and_swap<uint16_t>(info + 4, 8);   // probes received
      store_and_swap<uint16_t>(info + 6, 1);   // data length
      store_and_swap<uint32_t>(info + 8, data);
      store_and_swap<uint16_t>(info + 12, 9);  // minimum round trip, ms
      store_and_swap<uint16_t>(info + 14, 10); // median round trip, ms
      store_and_swap<uint32_t>(info + 16, kBitsPerSec);
      store_and_swap<uint32_t>(info + 20, kBitsPerSec);
    }
    store_and_swap<uint32_t>(base + out, qos);
  }
  if (event)
    if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event)) ev->Set(0, false);
}

// Arguments past the eighth are 8-byte big-endian stack slots.
uint32_t StackArgument(PPCContext& ctx, uint8_t* base, uint32_t index) {
  return load_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54 + 8 * index);
}
}  // namespace

// XNetQosLookup(caller, cxna, apxna, apxnkid, apxnkey, cina, aina, service_ids,
//               probes, bits_per_sec, flags, event, result)
REX_HOOK_RAW(__imp__NetDll_XNetQosLookup) {
  static PPCFunc* const original = RuntimeExport("__imp__NetDll_XNetQosLookup");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  CompleteQos(base, ctx.r4.u32 + ctx.r8.u32, StackArgument(ctx, base, 3), StackArgument(ctx, base, 4));
  ctx.r3.u64 = 0;
}

// XNetQosServiceLookup(caller, flags, event, result)
REX_HOOK_RAW(__imp__NetDll_XNetQosServiceLookup) {
  static PPCFunc* const original = RuntimeExport("__imp__NetDll_XNetQosServiceLookup");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  CompleteQos(base, 1, ctx.r5.u32, ctx.r6.u32);
  ctx.r3.u64 = 0;
}

// XNetQosListen(caller, xnkid, data, size, bits_per_sec, flags): hosts advertise
// their session to QoS probes; there is nobody to answer here.
REX_HOOK_RAW(__imp__NetDll_XNetQosListen) {
  static PPCFunc* const original = RuntimeExport("__imp__NetDll_XNetQosListen");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  ctx.r3.u64 = 0;
}
