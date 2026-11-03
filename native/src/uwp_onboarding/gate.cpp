// Runs the onboarding flow on the calling (UI) thread: own SDL window,
// own renderer, torn down before returning so the game can set up its own.
#include "gate.h"
#include <SDL3/SDL.h>

#include "app.h"
#include "ui/draw.h"

namespace uwp_onboarding {

bool EnsureGameFiles(const std::filesystem::path& userRoot, onboarding::Header header) {
  // Fast path without any UI: everything already verified.
  {
    onboarding::App probe(userRoot);
    probe.CheckNow();
    if (probe.AllReady()) return true;
  }

  // XInput is absent in AppContainer; WGI is the only gamepad backend and SDL 3.4 disables it by default.
  SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("onboarding SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("1 vs 100 Setup", onboarding::ui::kWidth, onboarding::ui::kHeight,
                                   SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    SDL_Log("onboarding window creation failed: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    return false;
  }
  SDL_SetRenderLogicalPresentation(renderer, onboarding::ui::kWidth, onboarding::ui::kHeight,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderVSync(renderer, 1);

  // Matches the standalone flow: never destroyed (worker threads may still
  // reference it), the process keeps it until exit.
  auto* app = new onboarding::App(userRoot, std::move(header));
  app->Start();

  bool ready = false;
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
        break;
      }
      if (event.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(event.gdevice.which);
      SDL_ConvertEventToRenderCoordinates(renderer, &event);
      app->HandleEvent(event);
    }
    if (!running) break;
    app->Render(renderer);
    SDL_RenderPresent(renderer);
    if (app->IsDone()) {
      ready = true;
      running = false;
    }
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
  return ready;
}

}  // namespace uwp_onboarding
