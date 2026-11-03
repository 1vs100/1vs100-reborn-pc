// Xbox 360 content packages (STFS files: the game package, show and episode
// packages) kept in their original form. A file, or a folder of package files. Packages are registered with the content manager and
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

REXCVAR_DEFINE_STRING(content, "content", "Content",
                      "Content package file, or folder of package files, to load at startup. "
                      "Relative paths resolve against the executable folder");

namespace {

constexpr uint32_t kGameTitleId = 0x584A07D1;

std::vector<std::filesystem::path> ConfiguredPackages() {
  std::filesystem::path source = REXCVAR_GET(content);
  if (source.empty()) return {};
  if (source.is_relative()) source = rex::filesystem::GetExecutableFolder() / source;
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

ContentScan ScanContentPackages() {
  std::filesystem::path source = REXCVAR_GET(content);
  if (source.empty()) return {source, false, 0};
  if (source.is_relative()) source = rex::filesystem::GetExecutableFolder() / source;
  std::error_code error;
  if (!std::filesystem::is_directory(source, error)) return {source, false, 0};
  size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(source, error)) {
    std::error_code entryError;
    if (entry.is_regular_file(entryError)) ++files;
  }
  return {source, true, files};
}

std::optional<std::filesystem::path> FindGamePackage() {
  // Show/episode packs share the title id, so match the container too:
  // only full-game containers can boot (they hold default.xex).
  using rex::system::XContentType;
  struct Candidate {
    std::filesystem::path path;
    XContentType type;
  };
  std::vector<Candidate> games;
  for (const auto& package : ConfiguredPackages()) {
    auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (!header || header->metadata.execution_info.title_id != kGameTitleId) continue;
    games.push_back({package, header->metadata.content_type});
    REXKRNL_INFO("content: candidate {} (type {:08X})", package.filename().string(),
                 static_cast<uint32_t>(games.back().type));
  }
  if (games.empty()) return std::nullopt;
  for (const auto& game : games) {
    if (game.type == XContentType::kXbox360Title || game.type == XContentType::kInstalledGame ||
        game.type == XContentType::kGamesOnDemand) {
      if (games.size() > 1)
        REXKRNL_WARN("content: {} title matches, using game container {}", games.size(),
                     game.path.filename().string());
      return game.path;
    }
  }
  REXKRNL_WARN("content: {} title matches, none is a game container; using {}", games.size(),
               games.front().path.filename().string());
  return games.front().path;
}

void InstallConfiguredContent() {
  const std::string source = REXCVAR_GET(content);
  if (source.empty()) return;
  const auto packages = ConfiguredPackages();
  if (packages.empty()) {
    REXKRNL_ERROR("content: no package files at {}", source);
    return;
  }
  auto* content = REX_KERNEL_STATE()->content_manager();
  size_t registered = 0;
  for (const auto& package : packages) {
    if (!rex::filesystem::StfsContainerDevice::ReadPackageHeader(package)) continue;
    const auto result = content->RegisterPackage(package);
    if (result) {
      REXKRNL_ERROR("content: {} failed ({:08X})", package.string(), result);
    } else {
      REXKRNL_INFO("content: {} registered", package.filename().string());
      ++registered;
    }
  }
  if (!registered) REXKRNL_WARN("content: no content packages in {}", source);
}
