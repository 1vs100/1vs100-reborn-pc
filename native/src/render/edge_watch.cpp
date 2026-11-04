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

// Tile state D3DDevice_BeginTiling 0x822F5CF0 leaves in the device: the band
// count at 0x327C (0x822F5D0C `stw r5,0x327C(r3)`), the max tile extents at
// 0x342C/0x3430 (0x822F5DC8/0x822F5DBC), and the bound surfaces at
// 0x3264..0x3278 (0x822F5F2C..0x822F5F40: canonical RT0-or-depth, RT0..RT3,
// depth). EndTiling 0x822F6380 resolves band by band against that state and
// zeroes 0x3264 on the way out (0x822F66DC), but leaves the count behind, so
// the tracker map is what marks the tiled pass.
constexpr uint32_t kTileCount = 0x327C;      // band count (r5 of BeginTiling)
constexpr uint32_t kTileMaxX = 0x342C;       // max tile x2 = target width
constexpr uint32_t kTileMaxY = 0x3430;       // max tile y2 = target height
constexpr uint32_t kTileCanonical = 0x3264;  // RT0, or depth when RT0 is null
constexpr uint32_t kTileColors[4] = {0x3268, 0x326C, 0x3270, 0x3274};  // RT0..RT3
constexpr uint32_t kTileDepth = 0x3278;

uint32_t DeviceWord(uint32_t device, uint32_t offset) {
  const auto* slot =
      REX_KERNEL_MEMORY()->TranslateVirtual<const rex::be<uint32_t>*>(device + offset);
  return slot ? uint32_t(*slot) : 0;
}

// Registers the bands BeginTiling just saved with the frame's real size, so
// draws until EndTiling share one host target. Every read is null-checked;
// anything missing skips the registration and leaves counting untouched.
void RegisterTiling(uint32_t device) {
  const uint32_t count = DeviceWord(device, kTileCount);
  const uint32_t width = DeviceWord(device, kTileMaxX);
  const uint32_t height = DeviceWord(device, kTileMaxY);
  if (!count || !width || !height || width > 8192 || height > 8192) {
    return;
  }
  uint32_t colors[4];
  uint32_t color_count = 0;
  for (uint32_t slot : kTileColors) {
    if (const uint32_t surface = DeviceWord(device, slot)) {
      colors[color_count++] = surface;
    }
  }
  uint32_t depth = DeviceWord(device, kTileDepth);
  if (!depth && !DeviceWord(device, kTileColors[0])) {
    depth = DeviceWord(device, kTileCanonical);
  }
  if (!color_count && !depth) {
    return;
  }
  namespace tracker = onevs100::render::tracker;
  tracker::RegisterTiledTarget(width, height, depth ? &depth : nullptr, depth ? 1 : 0, colors,
                               color_count);
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
ONEVS100_WATCH(sub_822F6D18, kGpuBeginShaderConstantF4)
ONEVS100_WATCH(sub_822E3530, kSetTextureFetchConstant)
ONEVS100_WATCH(sub_822F7030, kGpuSetTextureFetchConstant)
// D3DDevice_BeginTiling(pDevice, Flags, Count, pTileRects, pClearColor,
// ClearZ, ClearStencil). Runs the runtime first so the tile state above is
// fresh, then registers the bands; draws between Begin and End resolve their
// surfaces through that map.
REX_HOOK_RAW(sub_822F5CF0) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822F5CF0(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kBeginTiling, ctx);
  __imp__sub_822F5CF0(ctx, base);
  onevs100::render::edge::RegisterTiling(ctx.r3.u32);
}

// D3DDevice_EndTiling(pDevice, ResolveFlags, pResolveRects, pDestTexture,
// pClearColor, ClearZ, ClearStencil, pParameters). The band resolves inside
// still need the map, so it is cleared after the call.
REX_HOOK_RAW(sub_822F6380) {
  if (!onevs100::render::Enabled()) {
    __imp__sub_822F6380(ctx, base);
    return;
  }
  Observe(onevs100::render::edge::kEndTiling, ctx);
  __imp__sub_822F6380(ctx, base);
  onevs100::render::tracker::ClearTiledTargets();
}
ONEVS100_WATCH(sub_822F6060, kBeginZPass)
ONEVS100_WATCH(sub_822F6838, kEndZPass)
ONEVS100_WATCH(sub_822F40F8, kDrawIndexedTessellatedVertices)
