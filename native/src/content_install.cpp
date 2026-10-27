// content_install: Xbox 360 content packages (STFS files: show and episode
// packages, title updates) to install into the runtime's content store at
// startup. A file, or a folder whose files are all installed.
#include "content_install.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <filesystem>
#include <system_error>
#include <vector>

REXCVAR_DEFINE_STRING(content_install, "", "Content",
                      "Content package file, or folder of package files, to install at startup");

void InstallConfiguredContent() {
  std::filesystem::path source = REXCVAR_GET(content_install);
  if (source.empty()) return;
#if REX_PLATFORM_WINRT
  // The working directory of a UWP app is a system folder.
  if (source.is_relative()) source = rex::filesystem::GetExecutableFolder() / source;
#endif
  std::error_code error;
  std::vector<std::filesystem::path> packages;
  if (std::filesystem::is_directory(source, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(source, error))
      if (entry.is_regular_file(error)) packages.push_back(entry.path());
  } else if (std::filesystem::is_regular_file(source, error)) {
    packages.push_back(source);
  }
  if (packages.empty()) {
    REXKRNL_ERROR("content_install: no package files at {}", source.string());
    return;
  }
  auto* content = REX_KERNEL_STATE()->content_manager();
  for (const auto& package : packages) {
    const auto result = content->InstallContent(package);
    if (result) REXKRNL_ERROR("content_install: {} failed ({:08X})", package.string(), result);
    else REXKRNL_INFO("content_install: {} installed", package.filename().string());
  }
}
