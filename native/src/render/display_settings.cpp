#include "render/display_settings.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(render_scale, 100, "GPU", "Render target size as a percentage of the guest surface");
REXCVAR_DEFINE_INT32(render_shadow_scale, 100, "GPU", "Shadow surface size as a percentage");
REXCVAR_DEFINE_BOOL(render_fps_counter, false, "GPU", "Draw the frame counter over the presented image");

namespace onevs100::render::display {

int ResolutionScale() {
  return REXCVAR_GET(render_scale);
}

int ShadowScale() {
  return REXCVAR_GET(render_shadow_scale);
}

bool FpsCounter() {
  return REXCVAR_GET(render_fps_counter);
}

float AspectRatio() {
  return 16.0f / 9.0f;
}

}  // namespace onevs100::render::display
