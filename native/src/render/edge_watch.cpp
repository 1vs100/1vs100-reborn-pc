// Edge observation: prints the guest registers of the title's D3D entry points.
//
// The capture layer needs the argument positions of each entry point, and this
// title's XDK differs from the one the renderer was ported from. Every hook here
// forwards unchanged; the log is the evidence. Set render_edge_log to print one
// line per entry point per second.

#include "onevs100_init.h"

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(render_edge_log, false, "GPU", "Log the guest registers of the D3D edge calls");

namespace onevs100::render::edge {

namespace {

enum Slot : int {
  kBeginVertices,
  kDrawVertices,
  kDrawIndexedVertices,
  kSetStreamSource,
  kSetIndices,
  kSetTexture,
  kSetVertexShader,
  kSetPixelShader,
  kSetViewport,
  kSetScissor,
  kClear,
  kResolve,
  kSwap,
  kIsBusy,
  kIsFencePending,
  kBlockUntilIdle,
  kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "BeginVertices", "DrawVertices", "DrawIndexedVertices", "SetStreamSource", "SetIndices",
    "SetTexture",    "SetVertexShader", "SetPixelShader", "SetViewport",     "SetScissorRect",
    "Clear",         "Resolve",    "Swap",
    "IsBusy",        "IsFencePending", "BlockUntilIdle",
};

std::atomic<uint32_t> g_counts[kSlotCount];
std::atomic<uint64_t> g_first_args[kSlotCount];
std::atomic<uint64_t> g_last_args[kSlotCount];

bool Enabled() {
  return REXCVAR_GET(render_edge_log);
}

void Count(Slot slot, PPCContext& ctx) {
  if (!Enabled()) {
    return;
  }
  g_counts[slot].fetch_add(1, std::memory_order_relaxed);
  const uint64_t args = (uint64_t(ctx.r4.u64 & 0xFFFFFFFFull) << 32) | (ctx.r5.u64 & 0xFFFFFFFFull);
  uint64_t expected = 0;
  if (g_first_args[slot].compare_exchange_strong(expected, args, std::memory_order_relaxed)) {
    // Recorded once, so the first call of each entry point shows its argument shape.
  }
  g_last_args[slot].store(args, std::memory_order_relaxed);
}

// Prints the per-second call counts, plus the first and last argument pair seen.
void Report() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point next{};
  const auto now = Clock::now();
  if (now < next) {
    return;
  }
  next = now + std::chrono::seconds(1);
  std::string line;
  for (int i = 0; i < kSlotCount; ++i) {
    const uint32_t count = g_counts[i].exchange(0, std::memory_order_relaxed);
    if (!count) {
      continue;
    }
    char part[128];
    std::snprintf(part, sizeof(part), "%s=%u r4:r5=%08X:%08X/%08X:%08X ", kSlotNames[i], count,
                  uint32_t(g_first_args[i].load() >> 32), uint32_t(g_first_args[i].load()),
                  uint32_t(g_last_args[i].load() >> 32), uint32_t(g_last_args[i].load()));
    line += part;
  }
  if (!line.empty()) {
    REXKRNL_INFO("EDGE {}", line);
  }
}

}  // namespace

void Observe(Slot slot, PPCContext& ctx) {
  Count(slot, ctx);
  Report();
}

}  // namespace onevs100::render::edge

using onevs100::render::edge::kBeginVertices;
using onevs100::render::edge::kClear;
using onevs100::render::edge::kDrawIndexedVertices;
using onevs100::render::edge::kDrawVertices;
using onevs100::render::edge::kResolve;
using onevs100::render::edge::kSetIndices;
using onevs100::render::edge::kSetPixelShader;
using onevs100::render::edge::kSetScissor;
using onevs100::render::edge::kSetStreamSource;
using onevs100::render::edge::kSetTexture;
using onevs100::render::edge::kSetVertexShader;
using onevs100::render::edge::kSetViewport;
using onevs100::render::edge::kSwap;
using onevs100::render::edge::Observe;

// D3DDevice_BeginVertices(pDevice, PrimitiveType, VertexCount, VertexStreamZeroStride).
REX_HOOK_RAW(sub_822F29A8) {
  Observe(onevs100::render::edge::kBeginVertices, ctx);
  __imp__sub_822F29A8(ctx, base);
}

// D3DDevice_SetIndices(pDevice, pIndexData, BaseVertexIndex).
REX_HOOK_RAW(sub_822E2FB8) {
  Observe(onevs100::render::edge::kSetIndices, ctx);
  __imp__sub_822E2FB8(ctx, base);
}

// D3DDevice_SetScissorRect(pDevice, pRect).
REX_HOOK_RAW(sub_822E2C88) {
  Observe(onevs100::render::edge::kSetScissor, ctx);
  __imp__sub_822E2C88(ctx, base);
}

// D3DDevice_ClearF(pDevice, Flags, pRect, pColor, Z, Stencil).
REX_HOOK_RAW(sub_822E5C58) {
  Observe(onevs100::render::edge::kClear, ctx);
  __imp__sub_822E5C58(ctx, base);
}

// D3DDevice_Resolve(pDevice, Flags, pSourceRect, pDestTexture, pDestPoint, DestLevel, DestSliceOrFace,
// pClearColor, ClearZ, ClearStencil, pParameters).
REX_HOOK_RAW(sub_822E74D0) {
  Observe(onevs100::render::edge::kResolve, ctx);
  __imp__sub_822E74D0(ctx, base);
}

// D3DDevice_Swap(pDevice, pFrontBuffer, pParameters).
REX_HOOK_RAW(sub_822F50C0) {
  Observe(onevs100::render::edge::kSwap, ctx);
  __imp__sub_822F50C0(ctx, base);
}

// D3DDevice_DrawIndexedVertices(pDevice, PrimitiveType, BaseVertexIndex, StartIndex, IndexCount).
REX_HOOK_RAW(sub_822F3830) {
  Observe(onevs100::render::edge::kDrawIndexedVertices, ctx);
  __imp__sub_822F3830(ctx, base);
}

// D3DDevice_DrawVertices(pDevice, PrimitiveType, StartVertex, VertexCount).
REX_HOOK_RAW(sub_822F3440) {
  Observe(onevs100::render::edge::kDrawVertices, ctx);
  __imp__sub_822F3440(ctx, base);
}

// D3DDevice_SetStreamSource(pDevice, StreamNumber, pStreamData, OffsetInBytes, Stride).
REX_HOOK_RAW(sub_822E2E10) {
  Observe(onevs100::render::edge::kSetStreamSource, ctx);
  __imp__sub_822E2E10(ctx, base);
}

// D3DDevice_SetViewport(pDevice, pViewport).
REX_HOOK_RAW(sub_822E3948) {
  Observe(onevs100::render::edge::kSetViewport, ctx);
  __imp__sub_822E3948(ctx, base);
}

// D3DDevice_SetTexture(pDevice, Sampler, pTexture).
REX_HOOK_RAW(sub_822DFE90) {
  Observe(onevs100::render::edge::kSetTexture, ctx);
  __imp__sub_822DFE90(ctx, base);
}

// D3DDevice_SetVertexShader(pDevice, pShader).
REX_HOOK_RAW(sub_822EB5C0) {
  Observe(onevs100::render::edge::kSetVertexShader, ctx);
  __imp__sub_822EB5C0(ctx, base);
}

// D3DDevice_SetPixelShader(pDevice, pShader).
REX_HOOK_RAW(sub_822EB3B8) {
  Observe(onevs100::render::edge::kSetPixelShader, ctx);
  __imp__sub_822EB3B8(ctx, base);
}

// D3DDevice_IsBusy(pDevice).
REX_HOOK_RAW(sub_822D97B8) {
  Observe(onevs100::render::edge::kIsBusy, ctx);
  __imp__sub_822D97B8(ctx, base);
}

// D3DDevice_IsFencePending(pDevice, FenceValue).
REX_HOOK_RAW(sub_822D9850) {
  Observe(onevs100::render::edge::kIsFencePending, ctx);
  __imp__sub_822D9850(ctx, base);
}

// D3DDevice_BlockUntilIdle(pDevice).
REX_HOOK_RAW(sub_822D9AB0) {
  Observe(onevs100::render::edge::kBlockUntilIdle, ctx);
  __imp__sub_822D9AB0(ctx, base);
}
