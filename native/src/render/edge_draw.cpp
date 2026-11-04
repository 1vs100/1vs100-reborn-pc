// Edge layer: the draw entry points of the title's D3D runtime.
//
// Each hook builds the draw arguments from the guest registers, calls the runtime
// so its bookkeeping stays intact, and then hands the draw to the renderer. The
// runtime's immediate-mode draws arrive as Begin and End around a guest-written
// vertex buffer, so the End call finishes whatever Begin started.

#include "onevs100_init.h"

#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "render/gpu.h"
#include "render/gpu_cmdbuf.h"
#include "render/gpu_draw.h"

namespace {

namespace cmdbuf = onevs100::render::cmdbuf;
namespace draw = onevs100::render::draw;
namespace xenos = rex::graphics::xenos;

uint32_t Load32(uint32_t address) {
  const auto* value = REX_KERNEL_MEMORY()->TranslateVirtual<const rex::be<uint32_t>*>(address);
  return value ? uint32_t(*value) : 0;
}

}  // namespace

// D3DDevice_BeginVertices 0x822F29A8 (pDevice, PrimitiveType, VertexCount,
// VertexStreamZeroStride) returns the buffer the guest fills.
REX_HOOK_RAW(sub_822F29A8) {
  draw::DrawArgs a;
  a.device = ctx.r3.u32;
  a.type = xenos::PrimitiveType(ctx.r4.u32);
  a.count = ctx.r5.u32;
  a.stride = ctx.r6.u32;
  a.vertex_bytes = (a.count * a.stride) & ~3u;
  a.inline_data = true;
  __imp__sub_822F29A8(ctx, base);
  draw::BeginInline(ctx, base, a, ctx.r3.u32, 0);
}

// D3DDevice_BeginIndexedVertices 0x822F2E88 (pDevice, PrimitiveType,
// BaseVertexIndex, NumVertices, IndexCount, IndexFormat, VertexStreamZeroStride,
// ppIndexData, ppVertexData); the ninth argument is the stack slot at r1+0x54.
REX_HOOK_RAW(sub_822F2E88) {
  draw::DrawArgs a;
  a.device = ctx.r3.u32;
  a.type = xenos::PrimitiveType(ctx.r4.u32);
  a.base_vertex = int32_t(ctx.r5.u32);
  a.count = ctx.r7.u32;
  a.index32 = (ctx.r8.u32 & 4) != 0;
  a.stride = ctx.r9.u32;
  a.vertex_bytes = (ctx.r6.u32 * a.stride) & ~3u;
  a.indexed = true;
  a.inline_data = true;
  const uint32_t pp_index = ctx.r10.u32;
  const uint32_t pp_vertex = Load32(ctx.r1.u32 + 0x54);
  __imp__sub_822F2E88(ctx, base);
  const uint32_t index_data = pp_index ? Load32(pp_index) : 0;
  const uint32_t vertex_data = pp_vertex ? Load32(pp_vertex) : 0;
  draw::BeginInline(ctx, base, a, vertex_data, index_data);
}

// D3DDevice_EndVertices 0x822F2988, which EndIndexedVertices shares. The title
// may also reach the draw through an inlined end, so every later commit point
// finishes a pending record as well.
REX_HOOK_RAW(sub_822F2988) {
  __imp__sub_822F2988(ctx, base);
  draw::FinishPending();
}

// D3DDevice_DrawVerticesUP 0x822F2E40 and DrawIndexedVerticesUP 0x822F33A0 call
// Begin, copy the guest's data and finish, so the pending record is closed on
// both sides of the call.
REX_HOOK_RAW(sub_822F2E40) {
  draw::FinishPending();
  __imp__sub_822F2E40(ctx, base);
  draw::FinishPending();
}

REX_HOOK_RAW(sub_822F33A0) {
  draw::FinishPending();
  __imp__sub_822F33A0(ctx, base);
  draw::FinishPending();
}

// D3DDevice_DrawVertices 0x822F3440 (pDevice, PrimitiveType, StartVertex,
// VertexCount).
REX_HOOK_RAW(sub_822F3440) {
  draw::FinishPending();
  draw::DrawArgs a;
  a.device = ctx.r3.u32;
  a.type = xenos::PrimitiveType(ctx.r4.u32);
  a.start = ctx.r5.u32;
  a.base_vertex = int32_t(ctx.r5.u32);
  a.count = ctx.r6.u32;
  cmdbuf::NotePending(a.device);
  __imp__sub_822F3440(ctx, base);
  draw::Capture(ctx, base, a);
}

// D3DDevice_DrawIndexedVertices 0x822F3830 (pDevice, PrimitiveType,
// BaseVertexIndex, StartIndex, IndexCount).
REX_HOOK_RAW(sub_822F3830) {
  draw::FinishPending();
  draw::DrawArgs a;
  a.device = ctx.r3.u32;
  a.type = xenos::PrimitiveType(ctx.r4.u32);
  a.base_vertex = int32_t(ctx.r5.u32);
  a.start = ctx.r6.u32;
  a.count = ctx.r7.u32;
  a.indexed = true;
  cmdbuf::NotePending(a.device);
  __imp__sub_822F3830(ctx, base);
  draw::Capture(ctx, base, a);
}
