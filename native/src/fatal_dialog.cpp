#include "fatal_dialog.h"

#include <rex/logging/api.h>
#include <rex/platform.h>

#if !REX_PLATFORM_WINRT
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_messagebox.h>
#endif

#include <cstdio>

namespace fatal_dialog {

void FatalError(const char* title, const std::string& detail) {
  // Terminate lands here and then aborts into OnAbort; show once per process.
  static bool shown = false;
  if (shown) return;
  shown = true;
  rex::FlushLogging();
  const std::string text = TruncateDetail(detail);
  const char* heading = (title != nullptr && *title != '\0') ? title : "1 vs 100";
#if REX_PLATFORM_WINRT
  // The pending-marker modal lands in ticket 3; log only for now.
  (void)heading;
  (void)text;
#else
  if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, heading, text.c_str(), nullptr);
  } else {
    std::fputs(text.c_str(), stderr);
    std::fputc('\n', stderr);
  }
#endif
}

}  // namespace fatal_dialog
