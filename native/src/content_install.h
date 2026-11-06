#pragma once
#include <filesystem>
#include <optional>
#include <vector>

// Makes the packages named by the content setting, if any, available
// to the title without extracting them.
void InstallConfiguredContent();

// The title's own package among the content packages, used as game:
// when no unpacked game folder is present.
std::optional<std::filesystem::path> FindGamePackage();

// Candidates for the avatar pack table among the content files:
// AvatarAssetPack.toc itself, or the dashboard avatar package (title FFFE07DF)
// that holds it. A stale copy of one may sit next to a good copy of the other,
// so the loader tries them all.
std::vector<std::filesystem::path> FindAvatarPacks();

// What the content setting resolved to, for "no game files" diagnostics.
struct ContentScan {
  std::filesystem::path source;
  bool dirExists = false;
  size_t files = 0;
};
ContentScan ScanContentPackages();
