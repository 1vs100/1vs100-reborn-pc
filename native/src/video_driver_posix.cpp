#include <cstdlib>

// The game window needs SDL's X11 driver; under Wayland it runs through XWayland.
// Set before main so it precedes SDL initialization. SDL_VIDEO_DRIVER overrides it.
namespace {
const bool kX11Default = [] { return setenv("SDL_VIDEO_DRIVER", "x11", 0) == 0; }();
}
