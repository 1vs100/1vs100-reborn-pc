// Edge layer: the GPU-facing calls of the title's D3D runtime.
//
// The runtime is statically linked into the title, so its entry points are guest
// code. Each hook runs on the guest thread, turns the call into a renderer record
// and then calls the runtime so its own bookkeeping (ring cursor, dirty masks,
// pending fences) stays intact.

#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "render/gpu.h"
#include "render/guest/d3d_device.h"

namespace onevs100::render::edge {

namespace {

void (*g_vertex_shader_patch)(PPCContext& ctx, uint8_t* base) = nullptr;

}  // namespace

void SetVertexShaderPatcher(void (*patch)(PPCContext& ctx, uint8_t* base)) {
  g_vertex_shader_patch = patch;
}

void PatchVertexShader(PPCContext& ctx, uint8_t* base) {
  if (g_vertex_shader_patch != nullptr) {
    g_vertex_shader_patch(ctx, base);
  }
}

}  // namespace onevs100::render::edge

namespace {

using onevs100::render::guest::D3DDevice;

template <typename T>
T* Guest(uint32_t address) {
  return REX_KERNEL_MEMORY()->TranslateVirtual<T*>(address);
}

}  // namespace

// D3D::CDevice::SetFence, after the advanced fence counter is stored, 0x822D9084.
// r8 is the fence being issued and r7 the tagged ring position; the runtime's own
// CPU-completion branch publishes the same two words at 0x822D9070.
void OneVs100FenceIssued(PPCRegister& r7, PPCRegister& r8, PPCRegister& r11) {
  const auto* device = Guest<const D3DDevice>(r11.u32);
  if (device == nullptr) {
    return;
  }
  const uint32_t writeback = device->writeback_page;
  if (writeback == 0) {
    return;
  }
  auto* words = Guest<rex::be<uint32_t>>(writeback);
  if (words == nullptr) {
    return;
  }
  words[0] = r8.u32;
  words[1] = r7.u32;
}

// After the primary write index is stored, 0x822D8D88: the ring has no reader,
// so its read pointer follows the write index and range waits do not block.
void OneVs100PrimaryKick(PPCRegister& r11, PPCRegister& r29) {
  const auto* device = Guest<const D3DDevice>(r29.u32);
  if (device == nullptr) {
    return;
  }
  const uint32_t writeback = device->writeback_page;
  if (writeback == 0) {
    return;
  }
  auto* words = Guest<rex::be<uint32_t>>(writeback);
  if (words == nullptr) {
    return;
  }
  words[0x3C / 4] = r11.u32;
}

// After each add to the outstanding chunk count, 0x822D9120 and 0x822D915C: the
// GPU would retire the buffers, so the count drops straight back to zero.
void OneVs100ChunkQueued(PPCRegister& r31) {
  auto* device = Guest<D3DDevice>(r31.u32);
  if (device == nullptr) {
    return;
  }
  device->chunk_count = 0;
}
