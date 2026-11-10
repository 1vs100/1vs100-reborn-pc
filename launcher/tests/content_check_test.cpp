// CheckContent and LoadManifest on a content folder built from synthetic STFS
// headers, compared with fixtures/content_check.expected.txt.
//   content_check_test <fixtures folder> [--update]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "content_check.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kLive = 0x4C495645;
constexpr uint32_t kPirs = 0x50495253;
constexpr uint32_t kCon = 0x434F4E20;
constexpr uint32_t kInstalledGame = 0x00004000;
constexpr uint32_t kMarketplace = 0x00000002;
constexpr uint32_t kGameTitle = 0x584A07D1;
constexpr uint32_t kGameMedia = 0x20FFDD23;
constexpr uint32_t kLaunchTitle = 0x584A07E4;
constexpr uint32_t kShowEpisodeTitle = 0x584A07DC;
constexpr const char* kSha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// Content names as the game queries them, and short forms. The game sorts
// after every other name, so only the role precedence puts it first.
constexpr const char* kGameName = "ffffffffffffffffffffffffffffffffffffffff" "58";
constexpr const char* kGameShortName = "ffffffffffffffffffffffffffffffffffffffff";
constexpr const char* kLaunchName = "2222222222222222222222222222222222222222" "58";
constexpr const char* kLaunchShortName = "2222222222222222222222222222222222222222";
constexpr const char* kShowName = "3333333333333333333333333333333333333333" "58";
constexpr const char* kEpisodeName = "4444444444444444444444444444444444444444" "58";

struct Package {
  uint32_t magic;
  uint32_t content_type;
  uint32_t media_id;
  uint32_t title_id;
  size_t size;
};

void PutBigEndian32(std::vector<char>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<char>(value >> 24);
  bytes[offset + 1] = static_cast<char>(value >> 16);
  bytes[offset + 2] = static_cast<char>(value >> 8);
  bytes[offset + 3] = static_cast<char>(value);
}

void WriteBytes(const fs::path& file, const std::vector<char>& bytes) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WritePackage(const fs::path& file, const Package& package) {
  std::vector<char> bytes(package.size, '\0');
  PutBigEndian32(bytes, 0, package.magic);
  PutBigEndian32(bytes, 0x344, package.content_type);
  PutBigEndian32(bytes, 0x354, package.media_id);
  PutBigEndian32(bytes, 0x360, package.title_id);
  WriteBytes(file, bytes);
}

void WriteText(const fs::path& file, const std::string& text) {
  WriteBytes(file, std::vector<char>(text.begin(), text.end()));
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const char* RoleName(FileRole role) {
  switch (role) {
    case FileRole::Game: return "game";
    case FileRole::Launch: return "launch";
    case FileRole::Show: return "show";
    case FileRole::Episode: return "episode";
    case FileRole::ShowEpisode: return "show_episode";
    case FileRole::Avatar: return "avatar";
    case FileRole::Other: return "other";
    case FileRole::Invalid: return "invalid";
  }
  return "?";
}

const char* KindName(ManifestKind kind) {
  switch (kind) {
    case ManifestKind::Game: return "game";
    case ManifestKind::Launch: return "launch";
    case ManifestKind::Show: return "show";
    case ManifestKind::Episode: return "episode";
    case ManifestKind::Avatar: return "avatar";
  }
  return "?";
}

const char* Flag(bool value) { return value ? "yes" : "no"; }

// Every field, one per line, so a change shows as a readable diff.
std::string Describe(const FilesCheck& check) {
  std::string text;
  text += std::string("dir_ok ") + Flag(check.dirOk) + "\n";
  text += std::string("manifest_ok ") + Flag(check.manifestOk) + "\n";
  text += std::string("ok ") + Flag(check.ok()) + "\n";
  text += std::string("has_game ") + Flag(check.hasGame) + "\n";
  text += std::string("has_launch ") + Flag(check.hasLaunch) + "\n";
  text += "show_count " + std::to_string(check.showCount) + "\n";
  text += "episode_count " + std::to_string(check.episodeCount) + "\n";
  text += "show_episode_count " + std::to_string(check.showEpisodeCount) + "\n";
  text += std::string("has_avatar ") + Flag(check.hasAvatar) + "\n";
  text += "listing " + (check.listing ? std::to_string(check.listing->size()) + " files" : std::string("none")) + "\n";
  for (const auto& file : check.files) {
    text += "file " + file.name + " role=" + RoleName(file.role) + " present=" + Flag(file.present) +
            " valid=" + Flag(file.valid) + " duplicate=" + Flag(file.duplicate) + " note=\"" + file.note + "\"\n";
  }
  for (const auto& pack : check.avatarPacks) text += "avatar_pack " + pack.filename().string() + "\n";
  return text;
}

std::string DescribeManifest(const std::optional<std::vector<ManifestEntry>>& entries) {
  if (!entries) return "none\n";
  std::string text;
  for (const auto& entry : *entries) {
    text += std::string("entry ") + KindName(entry.kind) + " " + entry.name + " " + std::to_string(entry.size) + "\n";
  }
  return text;
}

std::string ManifestLine(size_t size, const char* kind, const std::string& path) {
  return std::string(kSha) + " " + std::to_string(size) + " " + kind + " " + path + "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const fs::path fixtures = argv[1];
  const bool update = argc > 2 && std::strcmp(argv[2], "--update") == 0;

  // A name of its own, so parallel runs do not delete each other's files.
  const fs::path root = fs::temp_directory_path() / ("onevs100_content_check_test_" + std::to_string(std::random_device{}()));
  fs::remove_all(root);
  const fs::path content = root / "content";
  fs::create_directories(content);

  // The game is present under its full name and found by its container type
  // alone, plus a short-named copy the game ignores.
  WritePackage(content / kGameName, {kLive, kInstalledGame, 0, kGameTitle, 0x500});
  WritePackage(content / kGameShortName, {kLive, kInstalledGame, 0, kGameTitle, 0x500});
  // The launch content only under its short name.
  WritePackage(content / kLaunchShortName, {kPirs, kMarketplace, 0, kLaunchTitle, 0x400});
  // The show pack has the wrong size; the episode pack has the right size but is no package.
  WritePackage(content / kShowName, {kLive, kMarketplace, 0, kShowEpisodeTitle, 0x401});
  WriteText(content / kEpisodeName, std::string(0x400, 'x'));
  WriteText(content / "AvatarAssetPack.toc", "table");
  // Not in the manifest: the game found by its media id alone, a game-title
  // package that is neither, a CON package of another title, and a file that is no package.
  WritePackage(content / "game_media.pkg", {kLive, kMarketplace, kGameMedia, kGameTitle, 0x400});
  WritePackage(content / "game_neither.pkg", {kLive, kMarketplace, 0, kGameTitle, 0x400});
  WritePackage(content / "other.pkg", {kCon, kMarketplace, 0, 0x4D5307E6, 0x400});
  WriteText(content / "readme.txt", "notes");

  // Every manifest file in place.
  const fs::path complete = root / "complete";
  fs::create_directories(complete);
  WritePackage(complete / kGameName, {kLive, kInstalledGame, 0, kGameTitle, 0x500});
  WritePackage(complete / kLaunchName, {kPirs, kMarketplace, 0, kLaunchTitle, 0x400});
  WritePackage(complete / kShowName, {kLive, kMarketplace, 0, kShowEpisodeTitle, 0x400});
  WritePackage(complete / kEpisodeName, {kLive, kMarketplace, 0, kShowEpisodeTitle, 0x400});
  WriteText(complete / "AvatarAssetPack.toc", "table");

  const fs::path manifest = root / "manifest.txt";
  WriteText(manifest, "# comment\r\n" + ManifestLine(0x500, "game", std::string("Content/") + kGameName) +
                          ManifestLine(0x400, "launch", kLaunchName) + ManifestLine(0x400, "show", kShowName) +
                          ManifestLine(0x400, "episode", kEpisodeName) +
                          ManifestLine(5, "avatar", "AvatarAssetPack.toc") +
                          // Skipped: unknown kind, short hash, size not a number, absolute path, too few fields.
                          ManifestLine(1, "music", "song.bin") + "abcd 1 game short.bin\n" +
                          std::string(kSha) + " 12x game bad_size.bin\n" + ManifestLine(1, "game", "/abs.bin") +
                          std::string(kSha) + " 1 game\n");

  std::string actual;
  actual += "== manifest\n" + DescribeManifest(LoadManifest(manifest));
  actual += "== with manifest\n" + Describe(CheckContent(content, manifest));
  actual += "== without manifest\n" + Describe(CheckContent(content, root / "missing_manifest.txt"));
  actual += "== complete with manifest\n" + Describe(CheckContent(complete, manifest));
  actual += "== missing folder\n" + Describe(CheckContent(root / "missing", manifest));
  fs::remove_all(root);

  const fs::path expected_file = fixtures / "content_check.expected.txt";
  if (update) {
    WriteText(expected_file, actual);
    std::puts("updated content_check.expected.txt");
    return 0;
  }
  const std::string expected = ReadText(expected_file);
  if (actual != expected) {
    std::fputs("FAIL: CheckContent differs from content_check.expected.txt; actual:\n", stdout);
    std::fputs(actual.c_str(), stdout);
    return 1;
  }
  std::puts("PASS");
  return 0;
}
