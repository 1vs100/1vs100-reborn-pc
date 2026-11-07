// Xbox 360 content packages (STFS files: the game package, show and episode
// packages) kept in their original form. A file, or a folder of package files. Packages are registered with the content manager and
// mounted when the title opens them.
#include "content_install.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <fmt/std.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <algorithm>
#include <system_error>
#include <vector>

REXCVAR_DEFINE_STRING(content, "content", "Content",
                      "Content package file, or folder of package files, to load at startup. "
                      "Relative paths resolve against the executable folder");

namespace {

constexpr uint32_t kGameTitleId = 0x584A07D1;
constexpr uint32_t kGameMediaId = 0x20FFDD23;
constexpr uint32_t kAvatarTitleId = 0xFFFE07DF;

std::vector<std::filesystem::path> ConfiguredPackages() {
  // The setting is UTF-8; a plain path() conversion would use the Windows code page.
  std::filesystem::path source = rex::to_path(REXCVAR_GET(content));
  if (source.empty()) return {};
#if REX_PLATFORM_WINRT
  // The install folder is read-only; user content lives in LocalState.
  if (source.is_relative()) source = rex::filesystem::GetUserFolder() / source;
#else
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

ContentScan ScanContentPackages() {
  std::filesystem::path source = rex::to_path(REXCVAR_GET(content));
  if (source.empty()) return {source, false, 0};
#if REX_PLATFORM_WINRT
  if (source.is_relative()) source = rex::filesystem::GetUserFolder() / source;
#else
  if (source.is_relative()) source = rex::filesystem::GetExecutableFolder() / source;
#endif
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
  // Show/episode packs share the title id, so the retail media id picks the
  // actual game. Other dumps/regions fall back to game containers, then to
  // the first title match.
  using rex::system::XContentType;
  struct Candidate {
    std::filesystem::path path;
    XContentType type;
    uint32_t mediaId;
  };
  std::vector<Candidate> games;
  for (const auto& package : ConfiguredPackages()) {
    auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (!header || header->metadata.execution_info.title_id != kGameTitleId) continue;
    games.push_back({package, header->metadata.content_type,
                     header->metadata.execution_info.media_id});
    REXKRNL_INFO("content: candidate {} (type {:08X}, media {:08X})", package.filename(),
                 static_cast<uint32_t>(games.back().type), games.back().mediaId);
  }
  if (games.empty()) return std::nullopt;
  for (const auto& game : games) {
    if (game.mediaId == kGameMediaId) {
      if (games.size() > 1)
        REXKRNL_INFO("content: {} title matches, using media match {}", games.size(),
                     game.path.filename());
      return game.path;
    }
  }
  for (const auto& game : games) {
    if (game.type == XContentType::kXbox360Title || game.type == XContentType::kInstalledGame ||
        game.type == XContentType::kGamesOnDemand) {
      REXKRNL_WARN("content: no media {:08X} match, using game container {}", kGameMediaId,
                   game.path.filename());
      return game.path;
    }
  }
  REXKRNL_WARN("content: {} title matches, none is a game container; using {}", games.size(),
               games.front().path.filename());
  return games.front().path;
}

std::vector<std::filesystem::path> FindAvatarPacks() {
  std::vector<std::filesystem::path> packs;
  for (const auto& file : ConfiguredPackages()) {
    if (file.filename() == "AvatarAssetPack.toc") {
      packs.push_back(file);
      continue;
    }
    auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(file);
    if (header && header->metadata.execution_info.title_id == kAvatarTitleId) packs.push_back(file);
  }
  if (packs.empty()) REXKRNL_ERROR("content: no AvatarAssetPack.toc or avatar package in {}", REXCVAR_GET(content));
  return packs;
}

void InstallConfiguredContent() {
  const std::string source = REXCVAR_GET(content);
  if (source.empty()) return;
  auto packages = ConfiguredPackages();
  if (packages.empty()) {
    REXKRNL_ERROR("content: no package files at {}", source);
    return;
  }
  // The first of "<id>58" and "<id>" registers and the other is refused, so
  // longer names go first and the full name wins, as the launcher reports.
  std::stable_sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
    return a.filename().native().size() > b.filename().native().size();
  });
  using rex::X_RESULT;  // X_ERROR_* expand to an unqualified X_RESULT cast.
  auto* content = REX_KERNEL_STATE()->content_manager();
  size_t registered = 0;
  for (const auto& package : packages) {
    if (!rex::filesystem::StfsContainerDevice::ReadPackageHeader(package)) continue;
    const auto result = content->RegisterPackage(package);
    if (result == X_ERROR_ALREADY_EXISTS) {
      REXKRNL_INFO("content: {} not used, a package with the same content id is registered", package.filename());
    } else if (result) {
      REXKRNL_ERROR("content: {} failed ({:08X})", package, result);
    } else {
      REXKRNL_INFO("content: {} registered", package.filename());
      ++registered;
    }
  }
  if (!registered) REXKRNL_WARN("content: no content packages in {}", source);
}
