// onevs100 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include "local_profile.h"
#include "content_install.h"
#include "gameserver_redirect.h"
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
    // All development saves/cache/config stay beside this isolated build.
    paths.user_data_root = folder / "userdata";
    paths.cache_root = folder / "cache";
    paths.config_path = folder / "onevs100.toml";
  }
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnPostLoadXexImage() override {
    ApplyGameserverImagePatches(REX_KERNEL_STATE()->memory()->virtual_membase());
  }
  void OnPostSetup() override { InstallConfiguredContent(); }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
