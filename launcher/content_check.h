// Checks the content folder against package/manifest.txt the way the game
// looks for its packages, for the launcher's Files tab.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class FileRole { Game, Launch, Show, Episode, ShowEpisode, Avatar, Other, Invalid };

struct ContentFile {
  std::string name;
  FileRole role = FileRole::Invalid;
  bool present = false;
  bool valid = false;
  std::string note;
  // A short-named twin of a manifest file; the game uses the full name.
  bool duplicate = false;
};

enum class ManifestKind { Game, Launch, Show, Episode, Avatar };

struct ManifestEntry {
  ManifestKind kind = ManifestKind::Game;
  std::string name;
  uint64_t size = 0;
};

// Shared with the UWP onboarding (package/manifest.txt): "<sha256> <size>
// <kind> <path>". The launcher matches content entries by file name and
// size; the onboarding additionally verifies hashes. Malformed lines are
// skipped. Nothing when the file is missing or holds no entry.
std::optional<std::vector<ManifestEntry>> LoadManifest(const std::filesystem::path& file);

struct TitleId {
  uint32_t value = 0;
  bool operator==(const TitleId&) const = default;
};

struct MediaId {
  uint32_t value = 0;
  bool operator==(const MediaId&) const = default;
};

struct ContentType {
  uint32_t value = 0;
  bool operator==(const ContentType&) const = default;
};

// The STFS header fields the game classifies a package by. Full parsing
// lives in the game.
struct StfsHeader {
  ContentType content_type;
  MediaId media_id;
  TitleId title_id;
};

// Nothing when the file is not an STFS package.
std::optional<StfsHeader> PeekStfsHeader(const std::filesystem::path& file);

// What a folder holds, by listing alone: cheap enough to compare every few seconds.
struct FolderEntry {
  std::string name;
  uintmax_t size = 0;
  std::filesystem::file_time_type modified;
  bool operator==(const FolderEntry&) const = default;
};

// Nothing when the folder is missing.
std::optional<std::vector<FolderEntry>> ListFolder(const std::filesystem::path& dir);

struct FilesCheck {
  std::filesystem::path dir;
  std::optional<std::vector<FolderEntry>> listing;
  bool dirOk = false;
  bool manifestOk = false;
  std::vector<ContentFile> files;
  bool hasGame = false;
  bool hasLaunch = false;
  size_t showCount = 0;
  size_t episodeCount = 0;
  size_t showEpisodeCount = 0;
  bool hasAvatar = false;
  // Every avatar file that looks right; the catalog tries them in order.
  std::vector<std::filesystem::path> avatarPacks;
  bool ok() const {
    if (!dirOk || !hasGame || !hasLaunch || !hasAvatar) return false;
    if (manifestOk) return showCount > 0 && episodeCount > 0;
    return showEpisodeCount > 0;
  }
};

// Lists dir and reads the header of each file in it. manifest_file is
// package/manifest.txt.
FilesCheck CheckContent(const std::filesystem::path& dir, const std::filesystem::path& manifest_file);
