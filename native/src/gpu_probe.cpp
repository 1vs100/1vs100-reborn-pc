// Diagnostic: per-frame workload at the guest D3D entry points.
//
// Counts the calls the title's renderer makes, so the native renderer has a
// measured target. Every counter is gated by diag_gpu_stats; the one-shot
// markers prove an entry point is reachable at all.
#include "onevs100_init.h"
#include <chrono>
#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_BOOL(diag_gpu_stats, false, "GPU", "DIAG: log per-second GPU workload counts");

namespace {

struct Counters {
  uint32_t presents;
  uint32_t draws;
  uint32_t vertices;
  uint32_t state_changes;
  uint32_t shader_changes;
  uint32_t textures;
  uint32_t resolves;
  uint32_t clears;
  uint32_t command_buffers;
};

Counters g_counts{};

void MarkOnce(bool* flag, const char* name) {
  if (*flag) {
    return;
  }
  *flag = true;
  REXKRNL_INFO("DIAG gpu first {}", name);
}

void LogWindow() {
  static auto window_start = std::chrono::steady_clock::now();
  ++g_counts.presents;
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start);
  if (elapsed < std::chrono::seconds(1)) {
    return;
  }
  const double seconds = elapsed.count() / 1000.0;
  const double frames = g_counts.presents / seconds;
  REXKRNL_INFO(
      "DIAG gpu fps={:.1f} frames={} draws={} vertices={} state={} shaders={} textures={} resolves={} "
      "clears={} cmdbufs={}",
      frames, g_counts.presents, g_counts.draws, g_counts.vertices, g_counts.state_changes,
      g_counts.shader_changes, g_counts.textures, g_counts.resolves, g_counts.clears,
      g_counts.command_buffers);
  g_counts = {};
  window_start = now;
}

inline bool Enabled() { return REXCVAR_GET(diag_gpu_stats); }

}  // namespace

// D3DDevice_Swap(pDevice, pFrontBuffer, pParameters): the per-frame present.
extern "C" REX_FUNC(sub_822F50C0) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_Swap");
  __imp__sub_822F50C0(ctx, base);
  if (Enabled()) {
    LogWindow();
  }
}

// D3DDevice_DrawIndexedVertices(pDevice, prim, base_vertex, start_index, index_count).
extern "C" REX_FUNC(sub_822F3830) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_DrawIndexedVertices");
  if (Enabled()) {
    ++g_counts.draws;
    g_counts.vertices += ctx.r7.u32;
  }
  __imp__sub_822F3830(ctx, base);
}

// D3DDevice_DrawVertices(pDevice, prim, start_vertex, vertex_count).
extern "C" REX_FUNC(sub_822F3440) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_DrawVertices");
  if (Enabled()) {
    ++g_counts.draws;
    g_counts.vertices += ctx.r6.u32;
  }
  __imp__sub_822F3440(ctx, base);
}

extern "C" REX_FUNC(sub_822F29A8) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_BeginVertices");
  if (Enabled()) {
    ++g_counts.draws;
  }
  __imp__sub_822F29A8(ctx, base);
}

extern "C" REX_FUNC(sub_822E2E10) {
  if (Enabled()) {
    ++g_counts.state_changes;
  }
  __imp__sub_822E2E10(ctx, base);
}

extern "C" REX_FUNC(sub_822E2FB8) {
  if (Enabled()) {
    ++g_counts.state_changes;
  }
  __imp__sub_822E2FB8(ctx, base);
}

extern "C" REX_FUNC(sub_822DFE90) {
  if (Enabled()) {
    ++g_counts.textures;
  }
  __imp__sub_822DFE90(ctx, base);
}

extern "C" REX_FUNC(sub_822EB3B8) {
  if (Enabled()) {
    ++g_counts.shader_changes;
  }
  __imp__sub_822EB3B8(ctx, base);
}

extern "C" REX_FUNC(sub_822EB5C0) {
  if (Enabled()) {
    ++g_counts.shader_changes;
  }
  __imp__sub_822EB5C0(ctx, base);
}

extern "C" REX_FUNC(sub_822E74D0) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_Resolve");
  if (Enabled()) {
    ++g_counts.resolves;
  }
  __imp__sub_822E74D0(ctx, base);
}

extern "C" REX_FUNC(sub_822E5C58) {
  if (Enabled()) {
    ++g_counts.clears;
  }
  __imp__sub_822E5C58(ctx, base);
}

extern "C" REX_FUNC(sub_822FBFF8) {
  static bool first = false;
  MarkOnce(&first, "D3DDevice_RunCommandBuffer");
  if (Enabled()) {
    ++g_counts.command_buffers;
  }
  __imp__sub_822FBFF8(ctx, base);
}
