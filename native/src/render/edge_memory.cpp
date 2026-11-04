// Edge layer: the resource and memory entry points of the title's D3D runtime.
//
// A buffer or texture becomes tracked guest memory when its header starts owning data
// (creation, or one of the XGOffset calls that point the header at its block) and is
// dirtied when the guest writes through a Lock. Addresses are this title's; the bodies
// follow CrashCourse2Recomp's hooks for the same entry points.

#include "onevs100_init.h"

#include <cstdint>

#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "render/gpu.h"
#include "render/gpu_memory.h"
#include "render/guest/d3d_device.h"

namespace {

namespace memory = onevs100::render::memory;
namespace guest = onevs100::render::guest;

// ReferenceCount of a resource header; read before a Release retires the header.
uint32_t ReferenceCount(uint32_t resource) {
  const auto* header = REX_KERNEL_MEMORY()->TranslateVirtual<const guest::D3DResource*>(resource);
  return header ? uint32_t(header->ReferenceCount) : 0;
}

}  // namespace

// D3DDevice_CreateVertexBuffer 0x822DAB10 (Length, Usage, Pool) returns the buffer.
REX_HOOK_RAW(sub_822DAB10) {
  __imp__sub_822DAB10(ctx, base);
  // Without the renderer the plugin owns the GPU state and there is nothing to track.
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::RegisterResource(ctx.r3.u32);
}

// D3DVertexBuffer_Unlock 0x822DAC88 (pBuffer). CC2 0x82538218 is the same unnamed entry point.
REX_HOOK_RAW(sub_822DAC88) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_822DAC88(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::DirtyResource(resource);
}

// D3DDevice_CreateIndexBuffer 0x822DACA8 (Length, Usage, Format, Pool) returns the buffer.
REX_HOOK_RAW(sub_822DACA8) {
  __imp__sub_822DACA8(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::RegisterResource(ctx.r3.u32);
}

// D3DResource_Release 0x822DBCA0 returns the remaining reference count; only the final
// release retires its memory, so that is the one dirtied. CC2 read the header's ranges
// before the call and dirtied them after; that range helper is private to gpu_memory.cpp,
// so the count is read instead, while the header is still valid.
REX_HOOK_RAW(sub_822DBCA0) {
  const uint32_t resource = ctx.r3.u32;
  if (!onevs100::render::Enabled()) {
    __imp__sub_822DBCA0(ctx, base);
    return;
  }
  if (resource && ReferenceCount(resource) == 1) memory::DirtyResource(resource);
  __imp__sub_822DBCA0(ctx, base);
}

// D3DVertexBuffer_Lock 0x822DBD20 (pBuffer, Offset, Size, Flags).
REX_HOOK_RAW(sub_822DBD20) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_822DBD20(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::DirtyResource(resource);
}

// D3DIndexBuffer_Lock 0x822DBE50 (pBuffer, Offset, Size, Flags).
REX_HOOK_RAW(sub_822DBE50) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_822DBE50(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::DirtyResource(resource);
}

// D3D::LockSurface 0x822DEAF8 and D3D::Lock2DSurface 0x822DEBD0, first argument the texture.
REX_HOOK_RAW(sub_822DEAF8) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_822DEAF8(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::DirtyResource(resource);
}

REX_HOOK_RAW(sub_822DEBD0) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_822DEBD0(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::DirtyResource(resource);
}

// D3DDevice_CreateTexture 0x822DFAF0 (Width, Height, Depth, Levels, Usage, Format, Pool,
// Type) returns the texture.
REX_HOOK_RAW(sub_822DFAF0) {
  __imp__sub_822DFAF0(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::RegisterResource(ctx.r3.u32);
}

// XGOffsetBaseTextureAddress 0x824773F8 (pTexture, pBase, pMip) and XGOffsetResourceAddress
// 0x82477460 (pResource, pBase): the header now points at its data, so the range becomes
// owned.
REX_HOOK_RAW(sub_824773F8) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_824773F8(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::RegisterResource(resource);
}

REX_HOOK_RAW(sub_82477460) {
  const uint32_t resource = ctx.r3.u32;
  __imp__sub_82477460(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  memory::RegisterResource(resource);
}

// XGSetTextureHeader 0x82476D20 (Width, Height, Levels, Usage, Format, Pool, BaseOffset,
// MipOffset, Pitch, pTexture, pBaseSize, pMipSize); pTexture is the tenth argument, the
// stack slot at r1+0x58. A header built here already owns its data through BaseOffset and
// MipOffset with no XGOffset call to register it, so the range is registered here.
REX_HOOK_RAW(sub_82476D20) {
  const uint32_t texture = *REX_KERNEL_MEMORY()->TranslateVirtual<rex::be<uint32_t>*>(ctx.r1.u32 + 0x58);
  __imp__sub_82476D20(ctx, base);
  if (!onevs100::render::Enabled()) {
    return;
  }
  if (texture) memory::RegisterResource(texture);
}
