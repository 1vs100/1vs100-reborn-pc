// Renderer selection. The renderer replaces the SDK GPU path entirely, so the
// setting is read during setup and again when the window exists.
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(native_renderer, false, "GPU",
                    "Render the title with its own D3D12 renderer instead of the SDK GPU path");

namespace onevs100::render {

bool Enabled() {
  return rex::cvar::Query<bool>("native_renderer");
}

}  // namespace onevs100::render
