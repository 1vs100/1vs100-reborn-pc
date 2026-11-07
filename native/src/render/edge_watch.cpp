// Edge observation: prints the guest registers of the title's D3D entry points.
//
// The capture layer needs the argument positions of each entry point, and this
// title's XDK differs from the one the renderer was ported from. Every hook here
// forwards unchanged; the log is the evidence. Set render_edge_log to print one
// line per entry point per second.

#include "onevs100_init.h"
#include "render/gpu_draw.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "render/gpu.h"
#include "render/gpu_tracker.h"

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
  kBeginShaderConstantF4,
  kGpuBeginShaderConstantF4,
  kSetTextureFetchConstant,
  kGpuSetTextureFetchConstant,
  kBeginTiling,
  kEndTiling,
  kBeginZPass,
  kEndZPass,
  kDrawIndexedTessellatedVertices,
  kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "BeginVertices", "DrawVertices", "DrawIndexedVertices", "SetStreamSource", "SetIndices",
    "SetTexture",    "SetVertexShader", "SetPixelShader", "SetViewport",     "SetScissorRect",
    "Clear",         "Resolve",    "Swap",
    "IsBusy",        "IsFencePending", "BlockUntilIdle",
    "BeginShaderConstantF4", "GpuBeginShaderConstantF4", "SetTextureFetchConstant",
    "GpuSetTextureFetchConstant", "BeginTiling", "EndTiling", "BeginZPass", "EndZPass",
    "DrawIndexedTessellatedVertices",
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

constexpr uint32_t kRenderTargets = 0x3148;  // D3DDevice::render_targets, RT0..RT3
constexpr uint32_t kDepthStencil = 0x3158;   // D3DDevice::depth_stencil

uint32_t DeviceWord(uint32_t device, uint32_t offset) {
  if (!onevs100::render::ValidGuest(device + offset)) {
    return 0;
  }
  const auto* slot =
      REX_KERNEL_MEMORY()->TranslateVirtual<const rex::be<uint32_t>*>(device + offset);
  return slot ? uint32_t(*slot) : 0;
}

// Registers the bound targets of a BeginTiling call as bands of the frame the tile rects cover. It runs before the
// runtime, whose own clear of the bands must already reach the full-size host surfaces.
void RegisterTiling(uint32_t device, uint32_t count, uint32_t rects) {
  if (!count || count > 64 || !onevs100::render::ValidGuest(rects) ||
      !onevs100::render::ValidGuest(rects + count * 16 - 1)) {
    return;
  }
  const auto* r = REX_KERNEL_MEMORY()->TranslateVirtual<const rex::be<int32_t>*>(rects);
  int32_t width = 0, height = 0;
  for (uint32_t i = 0; i < count; ++i) {
    width = std::max(width, int32_t(r[i * 4 + 2]));
    height = std::max(height, int32_t(r[i * 4 + 3]));
  }
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
    return;
  }
  uint32_t colors[4];
  uint32_t color_count = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (const uint32_t surface = DeviceWord(device, kRenderTargets + i * 4)) {
      colors[color_count++] = surface;
    }
  }
  uint32_t depth = DeviceWord(device, kDepthStencil);
  onevs100::render::tracker::RegisterTiledTarget(uint32_t(width), uint32_t(height), depth ? &depth : nullptr,
                                                 depth ? 1 : 0, colors, color_count);
}

}  // namespace

void Observe(Slot slot, PPCContext& ctx) {
  // Every D3D edge call passes here; the clock read in Report is too costly to make without the log.
  if (!Enabled()) return;
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

// D3DDevice_SetIndices(pDevice, pIndexData, BaseVertexIndex).
REX_HOOK_RAW(sub_822E2FB8) {
  // Without the renderer the plugin owns the GPU state and there is nothing to observe.
  if (!onevs100::render::Enabled()) {
    __imp__sub_822E2FB8(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetIndices, ctx);
  __imp__sub_822E2FB8(ctx, base);
}

// D3DDevice_SetScissorRect(pDevice, pRect).
REX_HOOK_RAW(sub_822E2C88) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822E2C88(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetScissor, ctx);
  __imp__sub_822E2C88(ctx, base);
}

// D3DDevice_Resolve(pDevice, Flags, pSourceRect, pDestTexture, pDestPoint, DestLevel, DestSliceOrFace,
// D3DDevice_SetStreamSource(pDevice, StreamNumber, pStreamData, OffsetInBytes, Stride).
REX_HOOK_RAW(sub_822E2E10) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822E2E10(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetStreamSource, ctx);
  __imp__sub_822E2E10(ctx, base);
}

// D3DDevice_SetViewport(pDevice, pViewport).
REX_HOOK_RAW(sub_822E3948) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822E3948(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetViewport, ctx);
  __imp__sub_822E3948(ctx, base);
}

// D3DDevice_SetTexture(pDevice, Sampler, pTexture).
REX_HOOK_RAW(sub_822DFE90) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822DFE90(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetTexture, ctx);
  __imp__sub_822DFE90(ctx, base);
}

// D3DDevice_SetVertexShader(pDevice, pShader).
REX_HOOK_RAW(sub_822EB5C0) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822EB5C0(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetVertexShader, ctx);
  __imp__sub_822EB5C0(ctx, base);
}

// D3DDevice_SetPixelShader(pDevice, pShader).
REX_HOOK_RAW(sub_822EB3B8) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822EB3B8(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kSetPixelShader, ctx);
  __imp__sub_822EB3B8(ctx, base);
}

// D3DDevice_IsBusy(pDevice).
REX_HOOK_RAW(sub_822D97B8) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822D97B8(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kIsBusy, ctx);
  __imp__sub_822D97B8(ctx, base);
}

// D3DDevice_IsFencePending(pDevice, FenceValue).
REX_HOOK_RAW(sub_822D9850) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822D9850(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kIsFencePending, ctx);
  __imp__sub_822D9850(ctx, base);
}

// D3DDevice_BlockUntilIdle(pDevice).
REX_HOOK_RAW(sub_822D9AB0) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822D9AB0(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kBlockUntilIdle, ctx);
  __imp__sub_822D9AB0(ctx, base);
}

// Entry points that write state the draw path does not read: the Gpu variants send through the
// ring, and tiling and z-pass calls change the pass layout. Counted so a run says whether the
// title uses them at all.
#define ONEVS100_WATCH(addr, slot)                       \
  REX_HOOK_RAW(addr) {                                   \
    if (!onevs100::render::Enabled()) {                  \
      __imp__##addr(ctx, base);                          \
      return;                                            \
    }                                                    \
    Observe(onevs100::render::edge::slot, ctx);          \
    __imp__##addr(ctx, base);                            \
  }

ONEVS100_WATCH(sub_822EAD60, kBeginShaderConstantF4)
// D3DDevice_GpuBeginShaderConstantF4 0x822F6D18 (pDevice, PixelShader, StartRegister, Vector4fCount) reserves a
// constant load in the ring and returns the data the title fills.
REX_HOOK_RAW(sub_822F6D18) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822F6D18(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kGpuBeginShaderConstantF4, ctx);
  const uint32_t device = ctx.r3.u32, pixel = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
  __imp__sub_822F6D18(ctx, base);
  if (ctx.r3.u32) onevs100::render::draw::NoteGpuConstants(device, pixel != 0, start, count, ctx.r3.u32);
}
ONEVS100_WATCH(sub_822E3530, kSetTextureFetchConstant)
ONEVS100_WATCH(sub_822F7030, kGpuSetTextureFetchConstant)
// D3DDevice_BeginTiling 0x822F5CF0 (pDevice, Flags, Count, pTileRects, pClearColor, ClearZ, ClearStencil).
REX_HOOK_RAW(sub_822F5CF0) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822F5CF0(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kBeginTiling, ctx);
  onevs100::render::edge::RegisterTiling(ctx.r3.u32, ctx.r5.u32, ctx.r6.u32);
  __imp__sub_822F5CF0(ctx, base);
}
ONEVS100_WATCH(sub_822F6380, kEndTiling)
ONEVS100_WATCH(sub_822F6060, kBeginZPass)
ONEVS100_WATCH(sub_822F6838, kEndZPass)
ONEVS100_WATCH(sub_822F40F8, kDrawIndexedTessellatedVertices)
