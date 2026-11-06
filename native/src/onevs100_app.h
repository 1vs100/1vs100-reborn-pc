// onevs100 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <fmt/std.h>
#include <rex/platform.h>
#include <functional>
#include <optional>
#include "local_profile.h"
#include "content_install.h"
#include "gameserver_redirect.h"
#include "matchmaking_client.h"
#include "online_service.h"
#include "xma_kick.h"
#include "avatar_view/avatar_dialog.h"
#include "fatal_dialog.h"
#include "avatar_native/native_provider.h"
#include "guest_memory_ops.h"
#ifdef _WIN32
#include "death_marker.h"
#else
#include "fatal_signal_posix.h"
#endif
#include <rex/system/kernel_state.h>
#ifdef _WIN32
#include "audio_xaudio2.h"
#endif
#if ONEVS100_NATIVE_GPU
#include "render/gpu.h"
#include "render/gpu_overlay.h"
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/presenter.h>
#if !defined(_WIN32)
#include <SDL3/SDL_video.h>
#endif
#endif
#if REX_PLATFORM_WINRT
#include <winrt/windows.applicationmodel.h>
#include <winrt/windows.graphics.display.h>
#include <winrt/windows.ui.core.h>
#include "onboarding/gate.h"
#include "onboarding_host/platform.h"
#endif
#include <chrono>
#include <cstdlib>
#include <memory>
#include "render/display_settings.h"
const rex::PPCImageInfo& NativeImageConfig();

class Onevs100App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Onevs100App>(new Onevs100App(ctx, "onevs100",
        NativeImageConfig()));
  }

  // Override virtual hooks for customization:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    std::string service_error;
    if (!LoadOnlineService(&service_error)) {
      // OnConfigurePaths always runs first, but do not print "Check ."
      const std::filesystem::path config =
          config_path_.empty() ? std::filesystem::path("onevs100.toml") : config_path_;
      fatal_dialog::FatalError("1 vs 100", "Online service failed to start: " + service_error + ". Check " +
                                               config.string() + ".");
      std::exit(1);
    }
    InitializeLocalProfile();
    config.gpu_plugin = "xenos";
#ifdef _WIN32
    config.audio_factory = REX_AUDIO_BACKEND(xaudio2::XAudio2AudioSystem);
#endif
#if ONEVS100_NATIVE_GPU
    // The edge layer stands in for the GPU: no graphics system, no plugin, and
    // nothing reads the ring. The renderer starts on the UI thread below.
    if (rex::cvar::Query<bool>("native_renderer")) {
      config.graphics = nullptr;
      config.gpu_plugin.clear();
    }
#endif
  }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    auto folder = rex::filesystem::GetExecutableFolder();
    if (paths.game_data_root.empty()) paths.game_data_root = folder / "assets";
#if REX_PLATFORM_WINRT
    // The package install folder is read-only. A config shipped in the package
    // seeds the writable one on first run.
    const auto shipped_config = folder / "onevs100.toml";
    folder = rex::filesystem::GetUserFolder();
    std::error_code error;
    if (!std::filesystem::exists(folder / "onevs100.toml", error) &&
        std::filesystem::exists(shipped_config, error)) {
      std::filesystem::copy_file(shipped_config, folder / "onevs100.toml", error);
      if (error) {
        config_seed_error_ = error;
      }
    }
    // First run: get the game files onto the device (browser upload or USB)
    // before anything tries to use them. Skipped silently when the manifest
    // already verifies; if the user quits, the game reports the missing
    // files through the usual path below.
    auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
    onboarding::EnsureGameFiles(
        folder,
        {"1 vs 100", "v2.0.1149",
         std::to_string(version.Major) + "." + std::to_string(version.Minor) + "." +
             std::to_string(version.Build) + "." + std::to_string(version.Revision)},
        onboarding::MakeWinrtPlatform());
#endif
    // All development saves/cache/config stay beside this isolated build.
#ifdef _WIN32
    death_marker::SetDirectory(folder);
#if REX_PLATFORM_WINRT
    fatal_dialog::SetDirectory(folder);
#endif
#else
    posix_crash::Install(folder);
#endif
    paths.user_data_root = folder / "userdata";
    paths.cache_root = folder / "cache";
    paths.config_path = folder / "onevs100.toml";
    config_path_ = paths.config_path;
  }
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)>) override {
    // Without an unpacked game folder, game: is the original game package.
    auto paths = defaults;
    std::error_code error;
    const bool haveGame = std::filesystem::is_regular_file(paths.game_data_root, error) ||
                          std::filesystem::exists(paths.game_data_root / "default.xex", error);
    if (!haveGame) {
      if (auto package = FindGamePackage()) {
        paths.game_data_root = *package;
      } else {
        // The SDK only reports the missing assets/ fallback afterwards, so
        // say here what was actually searched and what to do about it.
        const auto scan = ScanContentPackages();
        const auto folder = rex::filesystem::GetExecutableFolder();
        std::string reason;
        const std::string at =
            scan.source.empty() ? (folder / "content").string() : scan.source.string();
        if (!scan.dirExists) {
          REXKRNL_ERROR("onevs100: no game files found: content folder '{}' does not exist",
                        scan.source);
          reason = "content folder '" + at + "' does not exist";
        } else if (scan.files == 0) {
          REXKRNL_ERROR("onevs100: no game files found: '{}' is empty", scan.source);
          reason = "'" + scan.source.string() + "' is empty";
        } else {
          REXKRNL_ERROR("onevs100: no game files found: '{}' holds {} file(s), none is the game package",
                        scan.source, scan.files);
          reason = "'" + scan.source.string() + "' holds " + std::to_string(scan.files) +
                   " file(s), none is the game package";
        }
        // The scan resolves the setting against the user folder on UWP and the
        // executable folder elsewhere; the hint names that same place.
#if REX_PLATFORM_WINRT
        const std::string into =
            "into '" + at + "' (files directly inside, no subfolders)";
#else
        const std::string into = "into '" + at +
                                 "' (files directly inside, no subfolders), or unpack default.xex and its media into '" +
                                 (folder / "assets").string() + "'";
#endif
        REXKRNL_ERROR("onevs100: copy the untouched game package plus show/episode packages {}", into);
        fatal_dialog::FatalError("1 vs 100",
                                 "No game files found: " + reason +
                                     ". Copy the untouched game package plus show/episode packages " + into + ".");
        std::exit(1);
      }
    }
    return paths;
  }
  void OnPostInitLogging() override {
    // Logging starts after OnConfigurePaths.
    if (config_seed_error_) {
      REXLOG_WARN("Could not copy the shipped config to LocalState: {}",
                  config_seed_error_.message());
    }
  }
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnPostLoadXexImage() override {
    ApplyGameserverImagePatches(REX_KERNEL_STATE()->memory()->virtual_membase());
  }
  void OnPostSetup() override {
    onevs100::ConfigureGuestRuntime();
    // The title's texture fetch constants carry types the GPU backend calls
    // invalid; skipping them leaves the scene behind Press Start black. The GPU
    // plugin registers the setting during setup.
    constexpr const char* kFetchConstants = "gpu_allow_invalid_fetch_constants";
    if (rex::cvar::GetFlagSource(kFetchConstants) == rex::cvar::Source::kDefault &&
        !rex::cvar::SetFlagByName(kFetchConstants, "true"))
      REXKRNL_WARN("Could not enable {}", kFetchConstants);
    InstallConfiguredContent();
    if (CurrentOnlineService() == OnlineService::Gameserver) StartMatchmakingClient();
    avatar_view::StartUploadServer();
    // Missing pack degrades to stock avatars; log and continue with no dialog.
    // Recoverable warnings stay out of the fatal dialog path by design.
    if (!NativeAvatarEnabled()) {
      REXKRNL_WARN("content: no avatar pack found; running without avatars");
    }
  }
  // The guest rate shows in the SDK's debug overlay, which both renderers composite.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    SetGuestFrameStats(GuestFrameStats);
    avatar_dialog_ = std::make_unique<avatar_view::AvatarDialog>(drawer, immediate_drawer());
#if REX_PLATFORM_WINRT
    // A packaged crash leaves its report for the next launch: no dialog
    // runs on the faulting thread. The box owns itself and closes on tap.
    if (auto pending = fatal_dialog::TakePending()) {
      rex::ui::ImGuiDialog::ShowMessageBox(drawer, "1 vs 100", *pending);
    }
#endif
    if (onevs100::render::display::FpsCounter()) {
      fps_overlay_ = std::make_unique<rex::ui::DebugOverlayDialog>(drawer, GuestFrameStats);
    }
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  bool SetupPresentation() override {
    if (!rex::ReXApp::SetupPresentation()) {
      return false;
    }
#if ONEVS100_NATIVE_GPU
    if (rex::cvar::Query<bool>("native_renderer")) {
      // One overlay frame queued per present.
      onevs100::render::SetOverlayFrameCallback([this] {
        if (overlay_queued_.exchange(true)) return;
        if (!app_context().CallInUIThread([this] {
              overlay_queued_ = false;
              DrawOverlay();
            })) {
          overlay_queued_ = false;
        }
      });
      StartNativeRenderer();
    }
#endif
    return true;
  }
#if ONEVS100_NATIVE_GPU
  // Reached only with the native renderer: ImGui draws through it.
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    return onevs100::render::CreateOverlayDrawer();
  }
#endif

  void OnPreLaunchModule() override { rex::audio::StartXmaKicker(); }
  void OnShutdown() override {
#if ONEVS100_NATIVE_GPU
    onevs100::render::SetOverlayFrameCallback(nullptr);
#endif
    fps_overlay_.reset();
    avatar_dialog_.reset();
    rex::audio::StopXmaKicker();
  }
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  // Swaps per second over half-second windows.
  static rex::ui::FrameStats GuestFrameStats() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point start = Clock::now();
    static uint64_t start_frames = 0;
    static rex::ui::FrameStats stats;
    const auto now = Clock::now();
    const uint64_t frames = onevs100::render::display::FrameCount();
    const double elapsed = std::chrono::duration<double>(now - start).count();
    if (elapsed >= 0.5 && frames > start_frames) {
      stats.fps = double(frames - start_frames) / elapsed;
      stats.frame_time_ms = 1000.0 / stats.fps;
      start = now;
      start_frames = frames;
    }
    stats.frame_count = frames;
    return stats;
  }

  std::unique_ptr<rex::ui::DebugOverlayDialog> fps_overlay_;
  std::unique_ptr<avatar_view::AvatarDialog> avatar_dialog_;

#if ONEVS100_NATIVE_GPU
  std::atomic<bool> overlay_queued_{false};

  void DrawOverlay() {
    if (!imgui_drawer() || !window()) return;
    rex::ui::AppUIDrawContext context(window()->GetActualPhysicalWidth(), window()->GetActualPhysicalHeight());
    onevs100::render::BeginOverlayFrame();
    imgui_drawer()->Draw(context);
    onevs100::render::EndOverlayFrame();
  }
#endif

#if ONEVS100_NATIVE_GPU
  // The swap chain needs the window handle on the thread that owns the window:
  // a packaged app cannot query its CoreWindow from the render thread.
  void StartNativeRenderer() {
    plume::RenderWindow handle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
#if REX_PLATFORM_WINRT
    auto core_window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
    if (!core_window) {
      REXLOG_ERROR("native renderer: no CoreWindow on the UI thread");
      return;
    }
    const auto bounds = core_window.Bounds();
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f) {
      REXLOG_ERROR("native renderer: the CoreWindow has no size");
      return;
    }
    float scale = 1.0f;
    if (auto display = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView()) {
      scale = display.LogicalDpi() / 96.0f;
    }
    handle = static_cast<plume::RenderWindow>(winrt::get_abi(core_window));
    width = static_cast<uint32_t>(bounds.Width * scale);
    height = static_cast<uint32_t>(bounds.Height * scale);
#elif defined(_WIN32)
    if (!window()) {
      REXLOG_ERROR("native renderer: no window");
      return;
    }
    handle = static_cast<plume::RenderWindow>(window()->GetNativeWindowHandle());
#else
    // plume creates the Vulkan surface through SDL; the title has one window.
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (windows && count > 0) handle = windows[0];
    SDL_free(windows);
    if (!handle) {
      REXLOG_ERROR("native renderer: no window");
      return;
    }
#endif
    if (!onevs100::render::Initialize(handle, width, height)) {
      REXLOG_ERROR("native renderer: the renderer did not start");
    }
  }
#endif

  std::filesystem::path config_path_;
  std::error_code config_seed_error_;
};
