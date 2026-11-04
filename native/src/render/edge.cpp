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
// Publish the submitted fence plus the runtime's +2 advance, matching the
// published baseline the waits compare against, with its ring position.
// CPU-completion branch publishes the same two words at 0x822D9070.
void OneVs100FenceIssued(PPCRegister& r7, PPCRegister& r8, PPCRegister& r11) {
  // The writeback page belongs to the graphics system in use. The plugin owns it when the
  // renderer is off; when the renderer is on this stand-in has to answer from the first
  // call, which is before the renderer itself has started.
  if (!onevs100::render::Enabled()) {
    return;
  }
  if (!onevs100::render::ValidGuest(r11.u32)) {
    onevs100::render::NoteBadGuest(r11.u32, "SetFence device");
    return;
  }
  const auto* device = Guest<const D3DDevice>(r11.u32);
  const uint32_t writeback = device->writeback_page;
  if (!onevs100::render::ValidGuest(writeback)) {
    onevs100::render::NoteBadGuest(writeback, "SetFence writeback");
    return;
  }
  auto* words = Guest<rex::be<uint32_t>>(writeback);
  words[0] = r8.u32 + 2;
  words[1] = r7.u32;
}

// After the primary write index is stored, 0x822D8D88: the ring has no reader,
// so its read pointer follows the write index and range waits do not block.
void OneVs100PrimaryKick(PPCRegister& r11, PPCRegister& r29) {
  // The writeback page belongs to the graphics system in use. The plugin owns it when the
  // renderer is off; when the renderer is on this stand-in has to answer from the first
  // call, which is before the renderer itself has started.
  if (!onevs100::render::Enabled()) {
    return;
  }
  if (!onevs100::render::ValidGuest(r29.u32)) {
    onevs100::render::NoteBadGuest(r29.u32, "PrimaryKick device");
    return;
  }
  const auto* device = Guest<const D3DDevice>(r29.u32);
  const uint32_t writeback = device->writeback_page;
  if (!onevs100::render::ValidGuest(writeback)) {
    onevs100::render::NoteBadGuest(writeback, "PrimaryKick writeback");
    return;
  }
  auto* words = Guest<rex::be<uint32_t>>(writeback);
  words[0x3C / 4] = r11.u32;
}

// After each add to the outstanding chunk count, 0x822D9120 and 0x822D915C: the
// GPU would retire the buffers, so the count drops straight back to zero.
void OneVs100ChunkQueued(PPCRegister& r31) {
  (void)r31;
  // 0x2B04 has no verified counter in this build (layout doc 7.2: the title
  // keeps a ring alloc list at 0x3500). Disabled until a trace finds the wait.
  return;
}
