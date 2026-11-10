#include "content_check.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>
#include <system_error>

#include "content_name.h"
#include "files.h"

namespace fs = std::filesystem;

namespace {

// Content families the game looks for (native/src/content_install.cpp and the
// observed 1vs100 packages: show/episode packs share 584A07DC, the launch
// content uses 584A07E4).
constexpr TitleId kGameTitleId{0x584A07D1};
constexpr MediaId kGameMediaId{0x20FFDD23};
constexpr TitleId kLaunchTitleId{0x584A07E4};
constexpr TitleId kShowEpisodeTitleId{0x584A07DC};
constexpr TitleId kAvatarTitleId{0xFFFE07DF};
// STFS header fields (big-endian u32): magic "LIVE"/"PIRS"/"CON " at 0, content_type at
// 0x344, media_id at 0x354, title_id at 0x360.
constexpr uint32_t kStfsLive = 0x4C495645, kStfsPirs = 0x50495253, kStfsCon = 0x434F4E20;
constexpr size_t kStfsHeaderPeek = 0x400;
constexpr size_t kStfsContentTypeOff = 0x344;
constexpr size_t kStfsMediaIdOff = 0x354;
constexpr size_t kStfsTitleIdOff = 0x360;

// File names compare without case, as the runtime and Windows do.
bool SameFileName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

std::optional<ManifestKind> ParseKind(std::string_view kind) {
  if (kind == "game") return ManifestKind::Game;
  if (kind == "launch") return ManifestKind::Launch;
  if (kind == "show") return ManifestKind::Show;
  if (kind == "episode") return ManifestKind::Episode;
  if (kind == "avatar") return ManifestKind::Avatar;
  return std::nullopt;
}

FileRole RoleOf(ManifestKind kind) {
  switch (kind) {
    case ManifestKind::Game: return FileRole::Game;
    case ManifestKind::Launch: return FileRole::Launch;
    case ManifestKind::Show: return FileRole::Show;
    case ManifestKind::Episode: return FileRole::Episode;
    case ManifestKind::Avatar: return FileRole::Avatar;
  }
  return FileRole::Invalid;
}

uint32_t ReadBigEndian32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool IsGameContainer(ContentType type) {
  return type.value == 0x00001000 ||  // Xbox360Title
         type.value == 0x00004000 ||  // InstalledGame
         type.value == 0x00007000;    // GamesOnDemand
}

FileRole ClassifyPackage(const StfsHeader& header) {
  // Same precedence as FindGamePackage in native/src/content_install.cpp.
  if (header.title_id == kGameTitleId && (header.media_id == kGameMediaId || IsGameContainer(header.content_type)))
    return FileRole::Game;
  if (header.title_id == kLaunchTitleId) return FileRole::Launch;
  if (header.title_id == kShowEpisodeTitleId) return FileRole::ShowEpisode;
  if (header.title_id == kAvatarTitleId) return FileRole::Avatar;
  return FileRole::Other;
}

// Same rules as FindAvatarPack in native/src/content_install.cpp.
ContentFile ClassifyFile(const fs::path& path) {
  ContentFile file;
  file.name = PathText(path.filename());
  file.present = true;
  if (auto header = PeekStfsHeader(path)) {
    file.valid = true;
    file.role = ClassifyPackage(*header);
  } else if (file.name == "AvatarAssetPack.toc") {
    file.valid = true;
    file.role = FileRole::Avatar;
  } else {
    file.note = "not a package";
  }
  return file;
}

}  // namespace

std::optional<StfsHeader> PeekStfsHeader(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  unsigned char head[kStfsHeaderPeek] = {};
  in.read(reinterpret_cast<char*>(head), sizeof(head));
  if (static_cast<size_t>(in.gcount()) < kStfsTitleIdOff + 4) return std::nullopt;
  const uint32_t magic = ReadBigEndian32(head);
  if (magic != kStfsLive && magic != kStfsPirs && magic != kStfsCon) return std::nullopt;
  return StfsHeader{.content_type = ContentType{ReadBigEndian32(head + kStfsContentTypeOff)},
                    .media_id = MediaId{ReadBigEndian32(head + kStfsMediaIdOff)},
                    .title_id = TitleId{ReadBigEndian32(head + kStfsTitleIdOff)}};
}

std::optional<std::vector<ManifestEntry>> LoadManifest(const fs::path& file) {
  std::ifstream in(file);
  if (!in) return std::nullopt;
  std::vector<ManifestEntry> entries;
  std::string line;
  auto trim_cr = [](std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
  };
  while (std::getline(in, line)) {
    trim_cr(line);
    if (line.empty() || line.front() == '#') continue;
    const size_t a = line.find(' ');
    const size_t b = a == std::string::npos ? a : line.find(' ', a + 1);
    const size_t c = b == std::string::npos ? b : line.find(' ', b + 1);
    if (c == std::string::npos) continue;
    const std::string sha = line.substr(0, a);
    const std::string sizeText = line.substr(a + 1, b - a - 1);
    const std::string kind = line.substr(b + 1, c - b - 1);
    const std::string path = line.substr(c + 1);
    if (sha.size() != 64 || sizeText.empty() || path.empty() || path.front() == '/') continue;
    bool hex = true, digits = true;
    for (char ch : sha)
      if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) hex = false;
    for (char ch : sizeText)
      if (!(ch >= '0' && ch <= '9')) digits = false;
    if (!hex || !digits) continue;
    const auto parsed_kind = ParseKind(kind);
    if (!parsed_kind) continue;
    uint64_t size = 0;
    try {
      size = std::stoull(sizeText);
    } catch (...) {
      continue;
    }
    const size_t slash = path.find_last_of('/');
    entries.push_back({*parsed_kind, slash == std::string::npos ? path : path.substr(slash + 1), size});
  }
  if (entries.empty()) return std::nullopt;
  return entries;
}

std::optional<std::vector<FolderEntry>> ListFolder(const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return std::nullopt;
  std::vector<FolderEntry> entries;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec)) continue;
    entries.push_back({PathText(entry.path().filename()), entry.file_size(entry_ec), entry.last_write_time(entry_ec)});
  }
  std::sort(entries.begin(), entries.end(), [](const FolderEntry& a, const FolderEntry& b) { return a.name < b.name; });
  return entries;
}

FilesCheck CheckContent(const fs::path& dir, const fs::path& manifest_file) {
  FilesCheck check;
  check.dir = dir;
  check.listing = ListFolder(check.dir);
  std::error_code ec;
  check.dirOk = fs::is_directory(check.dir, ec);
  if (check.dirOk) {
    auto manifest = LoadManifest(manifest_file);
    check.manifestOk = manifest.has_value();
    if (manifest) {
      for (const auto& entry : *manifest) {
        ContentFile file;
        file.name = entry.name;
        file.role = RoleOf(entry.kind);
        fs::path full = check.dir / PathFromUtf8(entry.name.c_str());
        std::error_code entryEc;
        file.present = fs::is_regular_file(full, entryEc);
        const auto shortName = ShortContentName(entry.name);
        if (!file.present && shortName) {
          const fs::path shortPath = check.dir / PathFromUtf8(std::string(*shortName).c_str());
          if (fs::is_regular_file(shortPath, entryEc)) {
            full = shortPath;
            file.name = *shortName;
            file.present = true;
          }
        }
        if (!file.present) {
          file.note = "not found";
        } else if (fs::file_size(full, entryEc) != entry.size) {
          file.note = "wrong size";
        } else if (file.role != FileRole::Avatar && !PeekStfsHeader(full)) {
          file.note = "not a package";
        } else {
          file.valid = true;
        }
        check.files.push_back(std::move(file));
      }
      // Files the manifest does not know about.
      const size_t manifestFiles = check.files.size();
      for (const auto& entry : fs::directory_iterator(check.dir, ec)) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) continue;
        const std::string name = PathText(entry.path().filename());
        bool known = false;
        std::optional<ContentFile> duplicate;
        for (size_t i = 0; i < check.files.size(); ++i) {
          const ContentFile& file = check.files[i];
          if (SameFileName(file.name, name)) {
            known = true;
            break;
          }
          const auto shortName = ShortContentName(file.name);
          if (i < manifestFiles && shortName && SameFileName(*shortName, name))
            duplicate = ContentFile{name, file.role, true, false, "duplicate of " + file.name + ", not used", true};
        }
        if (known) continue;
        if (duplicate) {
          check.files.push_back(std::move(*duplicate));
          continue;
        }
        check.files.push_back(ClassifyFile(entry.path()));
      }
      std::sort(check.files.begin(), check.files.end(), [](const ContentFile& a, const ContentFile& b) {
        if ((a.role == FileRole::Game) != (b.role == FileRole::Game))
          return a.role == FileRole::Game;
        if ((a.role == FileRole::Launch) != (b.role == FileRole::Launch))
          return a.role == FileRole::Launch;
        return a.name < b.name;
      });
    } else {
      for (const auto& entry : fs::directory_iterator(check.dir, ec)) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) continue;
        check.files.push_back(ClassifyFile(entry.path()));
      }
      std::sort(check.files.begin(), check.files.end(),
                [](const ContentFile& a, const ContentFile& b) { return a.name < b.name; });
    }
    for (const auto& file : check.files) {
      if (!file.valid) continue;
      if (file.role == FileRole::Game) check.hasGame = true;
      if (file.role == FileRole::Launch) check.hasLaunch = true;
      if (file.role == FileRole::Show) ++check.showCount;
      if (file.role == FileRole::Episode) ++check.episodeCount;
      if (file.role == FileRole::ShowEpisode) ++check.showEpisodeCount;
      if (file.role == FileRole::Avatar) {
        check.hasAvatar = true;
        check.avatarPacks.push_back(check.dir / PathFromUtf8(file.name.c_str()));
      }
    }
    check.showEpisodeCount += check.showCount + check.episodeCount;
  }
  return check;
}
