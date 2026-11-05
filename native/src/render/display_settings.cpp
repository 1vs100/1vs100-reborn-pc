#include "render/display_settings.h"

#include <atomic>

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(render_scale, 100, "GPU", "Render target size as a percentage of the guest surface");
REXCVAR_DEFINE_INT32(render_shadow_scale, 100, "GPU", "Shadow surface size as a percentage");
REXCVAR_DEFINE_INT32(gpu_fps_cap, 60, "GPU", "Pace Swap to this many frames per second; 0 leaves the title unpaced");
REXCVAR_DEFINE_BOOL(render_fps_counter, false, "GPU", "Show the guest frame rate in the debug overlay at startup");

namespace onevs100::render::display {

namespace {
std::atomic<uint64_t> g_frames{0};
}  // namespace

int ResolutionScale() {
  return REXCVAR_GET(render_scale);
}

int ShadowScale() {
  return REXCVAR_GET(render_shadow_scale);
}

bool FpsCounter() {
  return REXCVAR_GET(render_fps_counter);
}

void CountFrame() {
  g_frames.fetch_add(1, std::memory_order_relaxed);
}

uint64_t FrameCount() {
  return g_frames.load(std::memory_order_relaxed);
}

float AspectRatio() {
  return 16.0f / 9.0f;
}

int FrameLimit() {
  return REXCVAR_GET(gpu_fps_cap);
}

}  // namespace onevs100::render::display
