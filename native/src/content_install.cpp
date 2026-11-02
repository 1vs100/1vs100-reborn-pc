// content_install: Xbox 360 content packages (STFS files: the game package,
// show and episode packages) kept in their original form. A file, or a folder
// of package files. Packages are registered with the content manager and
// mounted when the title opens them.
#include "content_install.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <system_error>
#include <vector>

REXCVAR_DEFINE_STRING(content_install, "", "Content",
                      "Content package file, or folder of package files, to load at startup");

namespace {

constexpr uint32_t kGameTitleId = 0x584A07D1;

std::vector<std::filesystem::path> ConfiguredPackages() {
  std::filesystem::path source = REXCVAR_GET(content_install);
  if (source.empty()) return {};
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
  return packages;
}

}  // namespace

std::optional<std::filesystem::path> FindGamePackage() {
  for (const auto& package : ConfiguredPackages()) {
    auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (header && header->metadata.execution_info.title_id == kGameTitleId) return package;
  }
  return std::nullopt;
}

void InstallConfiguredContent() {
  const auto packages = ConfiguredPackages();
  if (packages.empty()) {
    REXKRNL_ERROR("content_install: no package files at {}",
                  std::string(REXCVAR_GET(content_install)));
    return;
  }
  auto* content = REX_KERNEL_STATE()->content_manager();
  for (const auto& package : packages) {
    const auto result = content->RegisterPackage(package);
    if (result) REXKRNL_ERROR("content_install: {} failed ({:08X})", package.string(), result);
    else REXKRNL_INFO("content_install: {} registered", package.filename().string());
  }
}
