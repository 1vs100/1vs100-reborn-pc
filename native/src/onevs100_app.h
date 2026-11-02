// onevs100 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <functional>
#include <optional>
#include "local_profile.h"
#include "content_install.h"
#include "gameserver_redirect.h"
#include "matchmaking_client.h"
#include "online_service.h"
#include <rex/system/kernel_state.h>
#include <cstdlib>
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
    if (!LoadOnlineService()) std::exit(1);
    InitializeLocalProfile();
    config.gpu_plugin = "xenos";
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
#endif
    // All development saves/cache/config stay beside this isolated build.
    paths.user_data_root = folder / "userdata";
    paths.cache_root = folder / "cache";
    paths.config_path = folder / "onevs100.toml";
  }
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)>) override {
    // Without an unpacked game folder, game: is the original game package.
    auto paths = defaults;
    std::error_code error;
    if (!std::filesystem::exists(paths.game_data_root, error)) {
      if (auto package = FindGamePackage()) paths.game_data_root = *package;
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
    // The title's texture fetch constants carry types the GPU backend calls
    // invalid; skipping them leaves the scene behind Press Start black. The GPU
    // plugin registers the setting during setup.
    constexpr const char* kFetchConstants = "gpu_allow_invalid_fetch_constants";
    if (rex::cvar::GetFlagSource(kFetchConstants) == rex::cvar::Source::kDefault &&
        !rex::cvar::SetFlagByName(kFetchConstants, "true"))
      REXKRNL_WARN("Could not enable {}", kFetchConstants);
    InstallConfiguredContent();
    if (CurrentOnlineService() == OnlineService::Gameserver) StartMatchmakingClient();
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  std::error_code config_seed_error_;
};
