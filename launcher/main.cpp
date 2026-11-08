// Player launcher: edits the gamertag, avatar and onevs100.toml next to the
// game, then starts it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <misc/cpp/imgui_stdlib.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "content_name.h"
#include "font_data.h"
#include "xna_avatar_format.h"

namespace fs = std::filesystem;
namespace av = xe::kernel::xna::avatar;

namespace {

constexpr const char* kDefaultHost = "1vs100.fans";
constexpr const char* kDiscordUrl = "https://discord.gg/tV3ajhwFTf";
constexpr const char* kWebsiteUrl = "https://1vs100.fans";
constexpr const char* kDocsUrl = "https://docs.1vs100.fans";
constexpr const char* kGithubUrl = "https://github.com/1vs100";
constexpr const char* kAvatarEditorUrl = "https://360sona.com";
constexpr const char* kSonaGamertagUrl = "https://360sona.com/sona/v1/gamertag/";
constexpr uint32_t kPreviewWidth = 240, kPreviewHeight = 320;
constexpr float kTurnStep = 3.14159265f / 12.0f;  // 15 degrees per arrow press
// Keys the launcher edits through its own controls; the advanced list leaves them out.
constexpr std::array<std::string_view, 6> kManagedKeys = {"gameserver_host", "fullscreen", "mnk_mode", "mnk_mouse",
                                                          "content", "gpu_allow_invalid_fetch_constants"};
// The game builds its native renderer only where a DXC release exists.
#if defined(_WIN32) || (defined(__linux__) && defined(__x86_64__))
constexpr bool kHasNativeRenderer = true;
#else
constexpr bool kHasNativeRenderer = false;
#endif
constexpr const char* kDefaultContent = "content";
constexpr size_t kGamertagMax = 15;
// Content families the game looks for (native/src/content_install.cpp and the
// observed 1vs100 packages: show/episode packs share 584A07DC, the launch
// content uses 584A07E4).
constexpr uint32_t kGameTitleId = 0x584A07D1;
constexpr uint32_t kGameMediaId = 0x20FFDD23;
constexpr uint32_t kLaunchTitleId = 0x584A07E4;
constexpr uint32_t kShowEpisodeTitleId = 0x584A07DC;
constexpr uint32_t kAvatarTitleId = 0xFFFE07DF;
// STFS header fields (big-endian u32): magic "LIVE"/"PIRS"/"CON " at 0, content_type at
// 0x344, media_id at 0x354, title_id at 0x360.
constexpr uint32_t kStfsLive = 0x4C495645, kStfsPirs = 0x50495253, kStfsCon = 0x434F4E20;
constexpr size_t kStfsHeaderPeek = 0x400;
constexpr size_t kStfsContentTypeOff = 0x344;
constexpr size_t kStfsMediaIdOff = 0x354;
constexpr size_t kStfsTitleIdOff = 0x360;

#ifdef _WIN32
constexpr const char* kGameExecutable = "onevs100.exe";
#else
constexpr const char* kGameExecutable = "onevs100";
#endif

struct Paths {
  fs::path base;
  fs::path game;
  fs::path config;
  fs::path profiles;
  fs::path gamertag;
  fs::path avatar;
};

// SDL hands out UTF-8. Windows paths are UTF-16 and the Windows toolchain
// builds without char8_t, so convert through SDL_iconv there.
fs::path PathFromUtf8(const char* text) {
#ifdef _WIN32
  auto* wide = reinterpret_cast<wchar_t*>(SDL_iconv_string("UTF-16LE", "UTF-8", text, SDL_strlen(text) + 1));
  fs::path path(wide ? wide : L"");
  SDL_free(wide);
  return path;
#else
  return fs::path(text);
#endif
}

std::string PathText(const fs::path& path) {
#ifdef _WIN32
  const std::wstring& wide = path.native();
  char* utf8 = SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char*>(wide.c_str()),
                                (wide.size() + 1) * sizeof(wchar_t));
  std::string text(utf8 ? utf8 : "");
  SDL_free(utf8);
  return text;
#else
  return path.string();
#endif
}

Paths MakePaths() {
  // Do NOT SDL_free the result: this SDL version caches the base path and
  // frees it in SDL_Quit, so freeing here double-frees on shutdown.
  const char* base_path = SDL_GetBasePath();
  fs::path base = base_path ? PathFromUtf8(base_path) : fs::current_path();
#ifdef __APPLE__
  // In a bundle SDL_GetBasePath is Contents/Resources; the executables are in Contents/MacOS.
  const fs::path dir = base.has_filename() ? base : base.parent_path();
  if (dir.filename() == "Resources" && dir.parent_path().filename() == "Contents") {
    base = dir.parent_path() / "MacOS";
  }
#endif
  // Same rule as PlayerDataFolder in native/src/local_profile.cpp.
  const char* player_data = SDL_getenv("ONEVS100_PLAYER_DATA");
  const fs::path data = (player_data && *player_data) ? PathFromUtf8(player_data) : base / "userdata";
  const fs::path profiles = data / "profiles";
  return {base, base / kGameExecutable, base / "onevs100.toml", profiles, profiles / "local-name.txt",
          profiles / "avatar.bin"};
}

// Same rules as ValidName in native/src/local_profile.cpp.
bool GamertagChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
         c == '-';
}
bool ValidGamertag(std::string_view name) {
  if (name.empty() || name.size() > kGamertagMax || name.front() == ' ' || name.back() == ' ') return false;
  for (unsigned char c : name)
    if (!GamertagChar(c)) return false;
  return true;
}

// An Xbox gamertag as typed, without surrounding spaces.
class XboxGamertag {
 public:
  static std::optional<XboxGamertag> Parse(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    return XboxGamertag(std::string(text));
  }
  const std::string& text() const { return text_; }

 private:
  explicit XboxGamertag(std::string text) : text_(std::move(text)) {}
  std::string text_;
};

// The avatar manifest for a gamertag, as a .bin download.
std::string SonaManifestUrl(const XboxGamertag& gamertag) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string url = kSonaGamertagUrl;
  for (unsigned char c : gamertag.text()) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      url += static_cast<char>(c);
    } else {
      url += '%';
      url += kHex[c >> 4];
      url += kHex[c & 0xF];
    }
  }
  return url + "?client=1vs100rb&format=bin";
}

bool ValidHost(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  if (host.find("://") != std::string_view::npos) return false;
  for (char c : host) {
    if (c == ' ' || c == '\t' || c == '/' || c == ':' || c == '#' || c == '"' || c == '\'') return false;
  }
  return true;
}

// toml++ parse_file takes a narrow path, which breaks on non-ASCII Windows
// install paths. Read through the wide-aware fs::path instead.
std::expected<toml::table, std::string> ParseTomlFile(const fs::path& config) {
  std::ifstream in(config, std::ios::binary);
  if (!in) {
    std::error_code ec;
    (void)fs::exists(config, ec);
    return std::unexpected(ec ? ec.message() : std::string("cannot open file"));
  }
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try {
    return toml::parse(contents, PathText(config));
  } catch (const toml::parse_error& e) {
    return std::unexpected(std::string(e.description()));
  }
}

enum class ErrorKind {
  InvalidGamertag,
  InvalidHost,
  ConfigUnreadable,
  WriteFailed,
  AvatarUnreadable,
  GameMissing,
  LaunchFailed
};

struct Error {
  ErrorKind kind;
  fs::path path;
  std::string detail;
};

std::string Describe(const Error& error) {
  const std::string path = PathText(error.path);
  switch (error.kind) {
    case ErrorKind::InvalidGamertag:
      return "Enter a valid gamertag first (1-15 chars: letters, numbers, spaces, _ or -, no leading/trailing space).";
    case ErrorKind::InvalidHost:
      return "Enter a gameserver host first (hostname or IPv4, no spaces or ://).";
    case ErrorKind::ConfigUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::WriteFailed: return "Cannot write " + path + ": " + error.detail;
    case ErrorKind::AvatarUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::GameMissing: return "Game not found: " + path;
    case ErrorKind::LaunchFailed: return "Cannot start the game: " + error.detail;
  }
  return {};
}

// One onevs100.toml key outside the launcher's own controls.
struct ConfigEntry {
  enum class Kind { Flag, Integer, Number, Text };
  std::string key;
  Kind kind = Kind::Text;
  bool flag = false;
  int64_t integer = 0;
  double number = 0;
  std::string text;
};

std::optional<ConfigEntry> EntryFromNode(std::string key, const toml::node& node) {
  ConfigEntry entry;
  entry.key = std::move(key);
  if (auto flag = node.value_exact<bool>()) {
    entry.kind = ConfigEntry::Kind::Flag;
    entry.flag = *flag;
  } else if (auto integer = node.value_exact<int64_t>()) {
    entry.kind = ConfigEntry::Kind::Integer;
    entry.integer = *integer;
  } else if (auto number = node.value_exact<double>()) {
    entry.kind = ConfigEntry::Kind::Number;
    entry.number = *number;
  } else if (auto text = node.value_exact<std::string>()) {
    entry.kind = ConfigEntry::Kind::Text;
    entry.text = *text;
  } else {
    return std::nullopt;
  }
  return entry;
}

// strtod, not from_chars: older libc++ has no floating-point from_chars. Only
// plain decimals count, so text such as 0x10, inf or nan stays text.
bool ParseDecimal(std::string_view value, double* out) {
  if (value.empty() || value.find_first_not_of("0123456789.-eE") != std::string_view::npos) return false;
  const std::string text(value);
  char* end = nullptr;
  *out = std::strtod(text.c_str(), &end);
  return !text.empty() && end == text.c_str() + text.size();
}

// Typed the way a TOML value is: true/false, a whole number, a decimal number, else text.
ConfigEntry EntryFromText(std::string key, std::string_view value) {
  ConfigEntry entry;
  entry.key = std::move(key);
  int64_t integer = 0;
  double number = 0;
  const char* end = value.data() + value.size();
  if (value == "true" || value == "false") {
    entry.kind = ConfigEntry::Kind::Flag;
    entry.flag = value == "true";
  } else if (const auto parsed = std::from_chars(value.data(), end, integer);
             !value.empty() && parsed.ec == std::errc() && parsed.ptr == end) {
    entry.kind = ConfigEntry::Kind::Integer;
    entry.integer = integer;
  } else if (ParseDecimal(value, &number)) {
    entry.kind = ConfigEntry::Kind::Number;
    entry.number = number;
  } else {
    entry.kind = ConfigEntry::Kind::Text;
    entry.text = std::string(value);
  }
  return entry;
}

bool ValidConfigKey(std::string_view key) {
  if (key.empty()) return false;
  for (unsigned char c : key)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

bool ManagedKey(std::string_view key) {
  // Without the checkbox the key stays in the advanced list, for builds that enable the renderer.
  if (kHasNativeRenderer && key == "native_renderer") return true;
  return std::find(kManagedKeys.begin(), kManagedKeys.end(), key) != kManagedKeys.end();
}

struct Settings {
  std::string host;
  bool windowed = false;
  std::string content = "content";
  bool keyboard = false;
  bool mouse_look = false;
  bool native_renderer = false;
  // The game turns this on itself when the key is missing; off leaves the Press Start scene black on the
  // emulated renderer.
  bool allow_invalid_fetch_constants = true;
  std::vector<ConfigEntry> others;
  // Keys removed from `others` since loading, so saving drops them from the file.
  std::vector<std::string> removed;
};

// Missing keys take the game's own defaults, except the host, which the
// release presets to the community server.
std::expected<Settings, Error> LoadSettings(const fs::path& config) {
  Settings settings;
  settings.host = kDefaultHost;
  std::error_code ec;
  if (!fs::exists(config, ec)) return settings;
  auto parsed = ParseTomlFile(config);
  if (!parsed) return std::unexpected(Error{ErrorKind::ConfigUnreadable, config, parsed.error()});
  const toml::table& table = *parsed;
  if (auto host = table["gameserver_host"].value<std::string>()) settings.host = *host;
  if (auto fullscreen = table["fullscreen"].value<bool>()) settings.windowed = !*fullscreen;
  if (auto content = table["content"].value<std::string>()) settings.content = *content;
  if (auto keyboard = table["mnk_mode"].value<bool>()) settings.keyboard = *keyboard;
  if (auto mouse_look = table["mnk_mouse"].value<bool>()) settings.mouse_look = *mouse_look;
  if (auto native = table["native_renderer"].value<bool>(); native && kHasNativeRenderer)
    settings.native_renderer = *native;
  if (auto fetch = table["gpu_allow_invalid_fetch_constants"].value<bool>())
    settings.allow_invalid_fetch_constants = *fetch;
  for (const auto& [key, node] : table) {
    if (ManagedKey(key.str())) continue;
    if (auto entry = EntryFromNode(std::string(key.str()), node)) settings.others.push_back(std::move(*entry));
  }
  return settings;
}

std::expected<void, Error> SaveSettings(const fs::path& config, const Settings& settings) {
  if (!ValidHost(settings.host))
    return std::unexpected(Error{ErrorKind::InvalidHost, {}, {}});
  toml::table table;
  std::error_code ec;
  if (fs::exists(config, ec)) {
    auto parsed = ParseTomlFile(config);
    if (!parsed)
      return std::unexpected(Error{ErrorKind::ConfigUnreadable, config, parsed.error()});
    table = std::move(*parsed);
  }
  for (const auto& key : settings.removed) table.erase(key);
  for (const auto& entry : settings.others) {
    switch (entry.kind) {
      case ConfigEntry::Kind::Flag: table.insert_or_assign(entry.key, entry.flag); break;
      case ConfigEntry::Kind::Integer: table.insert_or_assign(entry.key, entry.integer); break;
      case ConfigEntry::Kind::Number: table.insert_or_assign(entry.key, entry.number); break;
      case ConfigEntry::Kind::Text: table.insert_or_assign(entry.key, entry.text); break;
    }
  }
  table.insert_or_assign("gameserver_host", settings.host);
  table.insert_or_assign("content", settings.content);
  table.insert_or_assign("fullscreen", !settings.windowed);
  table.insert_or_assign("mnk_mode", settings.keyboard);
  table.insert_or_assign("mnk_mouse", settings.mouse_look);
  if (kHasNativeRenderer) table.insert_or_assign("native_renderer", settings.native_renderer);
  table.insert_or_assign("gpu_allow_invalid_fetch_constants", settings.allow_invalid_fetch_constants);
  std::ofstream out(config, std::ios::trunc);
  out << table << '\n';
  out.flush();
  if (!out) return std::unexpected(Error{ErrorKind::WriteFailed, config, "write error"});
  return {};
}

std::optional<std::string> LoadGamertag(const fs::path& file) {
  std::ifstream in(file);
  std::string name;
  if (in && std::getline(in, name)) {
    if (!name.empty() && name.back() == '\r') name.pop_back();
    if (ValidGamertag(name)) return name;
  }
  return std::nullopt;
}

std::expected<void, Error> SaveGamertag(const Paths& paths, const std::string& name) {
  if (!ValidGamertag(name)) return std::unexpected(Error{ErrorKind::InvalidGamertag, {}, {}});
  std::error_code ec;
  fs::create_directories(paths.profiles, ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, paths.profiles, ec.message()});
  std::ofstream out(paths.gamertag, std::ios::trunc | std::ios::binary);
  out << name << '\n';
  out.flush();
  if (!out) return std::unexpected(Error{ErrorKind::WriteFailed, paths.gamertag, "write error"});
  return {};
}

std::expected<void, Error> WriteBytes(const fs::path& target, const uint8_t* data, size_t size) {
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, target.parent_path(), ec.message()});
  std::ofstream out(target, std::ios::trunc | std::ios::binary);
  out.write(reinterpret_cast<const char*>(data), std::streamsize(size));
  out.flush();
  if (!out) return std::unexpected(Error{ErrorKind::WriteFailed, target, "write error"});
  return {};
}

std::expected<std::vector<uint8_t>, Error> ReadBytes(const fs::path& source) {
  std::ifstream in(source, std::ios::binary);
  if (!in) return std::unexpected(Error{ErrorKind::AvatarUnreadable, source, "cannot open file"});
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

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

struct ManifestEntry {
  std::string kind;
  std::string name;
  uint64_t size = 0;
};

// File names compare without case, as the runtime and Windows do.
bool SameFileName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

// Shared with the UWP onboarding (package/manifest.txt): "<sha256> <size>
// <kind> <path>". The launcher matches content entries by file name and
// size; the onboarding additionally verifies hashes.
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
    if (kind != "game" && kind != "launch" && kind != "show" && kind != "episode" &&
        kind != "avatar") {
      continue;
    }
    uint64_t size = 0;
    try {
      size = std::stoull(sizeText);
    } catch (...) {
      continue;
    }
    const size_t slash = path.find_last_of('/');
    entries.push_back({kind, slash == std::string::npos ? path : path.substr(slash + 1), size});
  }
  if (entries.empty()) return std::nullopt;
  return entries;
}

// What a folder holds, by listing alone: cheap enough to compare every few seconds.
struct FolderEntry {
  std::string name;
  uintmax_t size = 0;
  fs::file_time_type modified;
  bool operator==(const FolderEntry&) const = default;
};

// nullopt when the folder is missing.
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

struct FilesCheck {
  fs::path dir;
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
  std::vector<fs::path> avatarPacks;
  bool ok() const {
    if (!dirOk || !hasGame || !hasLaunch || !hasAvatar) return false;
    if (manifestOk) return showCount > 0 && episodeCount > 0;
    return showEpisodeCount > 0;
  }
};

uint32_t ReadBigEndian32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Minimal STFS header peek: enough to classify a package the way the game
// does (title id, media id, container type). Full parsing lives in the game.
std::optional<std::array<uint32_t, 3>> PeekStfsHeader(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  unsigned char head[kStfsHeaderPeek] = {};
  in.read(reinterpret_cast<char*>(head), sizeof(head));
  if (static_cast<size_t>(in.gcount()) < kStfsTitleIdOff + 4) return std::nullopt;
  const uint32_t magic = ReadBigEndian32(head);
  if (magic != kStfsLive && magic != kStfsPirs && magic != kStfsCon) return std::nullopt;
  return std::array<uint32_t, 3>{ReadBigEndian32(head + kStfsContentTypeOff),
                                 ReadBigEndian32(head + kStfsMediaIdOff),
                                 ReadBigEndian32(head + kStfsTitleIdOff)};
}

bool IsGameContainer(uint32_t type) {
  return type == 0x00001000 ||  // Xbox360Title
         type == 0x00004000 ||  // InstalledGame
         type == 0x00007000;    // GamesOnDemand
}

FileRole ClassifyPackage(uint32_t title, uint32_t media, uint32_t type) {
  // Same precedence as FindGamePackage in native/src/content_install.cpp.
  if (title == kGameTitleId && (media == kGameMediaId || IsGameContainer(type))) return FileRole::Game;
  if (title == kLaunchTitleId) return FileRole::Launch;
  if (title == kShowEpisodeTitleId) return FileRole::ShowEpisode;
  if (title == kAvatarTitleId) return FileRole::Avatar;
  return FileRole::Other;
}

// Same rules as FindAvatarPack in native/src/content_install.cpp.
ContentFile ClassifyFile(const fs::path& path) {
  ContentFile file;
  file.name = PathText(path.filename());
  file.present = true;
  if (auto header = PeekStfsHeader(path)) {
    file.valid = true;
    file.role = ClassifyPackage((*header)[2], (*header)[1], (*header)[0]);
  } else if (file.name == "AvatarAssetPack.toc") {
    file.valid = true;
    file.role = FileRole::Avatar;
  } else {
    file.note = "not a package";
  }
  return file;
}

fs::path ResolveContentDir(const Paths& paths, const std::string& setting) {
  const std::string raw = setting.empty() ? "content" : setting;
  fs::path dir = PathFromUtf8(raw.c_str());
  if (dir.is_relative()) dir = paths.base / dir;
  return dir;
}

struct App;
void RefreshFiles(App& app);

std::expected<void, Error> LaunchGame(const Paths& paths) {
  std::error_code ec;
  if (!fs::is_regular_file(paths.game, ec)) return std::unexpected(Error{ErrorKind::GameMissing, paths.game, {}});
  const std::string game = PathText(paths.game);
  const std::string base = PathText(paths.base);
  const char* args[] = {game.c_str(), nullptr};
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, base.c_str());
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  SDL_Process* process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!process) return std::unexpected(Error{ErrorKind::LaunchFailed, paths.game, SDL_GetError()});
  // Releases the handle only. A background process keeps running.
  SDL_DestroyProcess(process);
  return {};
}

struct Fonts {
  ImFont* body;
  ImFont* title;
};

// Palette of 1vs100.fans and docs.1vs100.fans (gameserver/site/src/styles.css, dark scheme).
ImVec4 Hex(uint32_t rgb, float alpha = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}
const ImVec4 kBackground = Hex(0x0d0b1f);
const ImVec4 kSurface = Hex(0x17142e);
const ImVec4 kSurface2 = Hex(0x211d3d);
const ImVec4 kSurface2Hover = Hex(0x2c2752);
const ImVec4 kText = Hex(0xf3f1ff);
const ImVec4 kText3 = Hex(0xf3f1ff, 0.52f);
const ImVec4 kAccent = Hex(0xb28cff);
const ImVec4 kError = Hex(0xff2d55);
const ImVec4 kCaution = Hex(0xffb020);
// The "show" gradient: blue, violet at 40%, magenta at 70%, red.
constexpr std::array<std::pair<float, uint32_t>, 4> kShowStops = {
    {{0.0f, 0x3d6bff}, {0.4f, 0x8b3dff}, {0.7f, 0xe23fd2}, {1.0f, 0xff2d55}}};

void ShowGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha = 1.0f) {
  const float width = max.x - min.x;
  for (size_t i = 0; i + 1 < kShowStops.size(); ++i) {
    const ImU32 left = ImGui::GetColorU32(Hex(kShowStops[i].second, alpha));
    const ImU32 right = ImGui::GetColorU32(Hex(kShowStops[i + 1].second, alpha));
    draw->AddRectFilledMultiColor(ImVec2(min.x + width * kShowStops[i].first, min.y),
                                  ImVec2(min.x + width * kShowStops[i + 1].first, max.y), left, right, right, left);
  }
}

void ApplyStyle(float scale) {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(24, 20);
  style.FramePadding = ImVec2(12, 8);
  style.ItemSpacing = ImVec2(10, 10);
  style.FrameRounding = 8;
  style.TabRounding = 8;
  style.GrabRounding = 8;
  style.ChildRounding = 12;
  style.WindowBorderSize = 0;
  style.FrameBorderSize = 0;
  style.ChildBorderSize = 1;
  style.TabBarBorderSize = 0;
  style.ScaleAllSizes(scale);

  ImVec4* c = style.Colors;
  c[ImGuiCol_WindowBg] = kBackground;
  c[ImGuiCol_ChildBg] = kSurface;
  c[ImGuiCol_PopupBg] = kSurface;
  c[ImGuiCol_Border] = Hex(0xffffff, 0.08f);
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kText3;
  c[ImGuiCol_FrameBg] = kSurface2;
  c[ImGuiCol_FrameBgHovered] = kSurface2Hover;
  c[ImGuiCol_FrameBgActive] = kSurface2Hover;
  c[ImGuiCol_Button] = kSurface2;
  c[ImGuiCol_ButtonHovered] = kSurface2Hover;
  c[ImGuiCol_ButtonActive] = kSurface2Hover;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_Tab] = kBackground;
  c[ImGuiCol_TabHovered] = kSurface2;
  c[ImGuiCol_TabSelected] = kSurface;
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TabDimmedSelected] = kSurface;
  c[ImGuiCol_TabDimmedSelectedOverline] = kAccent;
  c[ImGuiCol_TextSelectedBg] = Hex(0xb28cff, 0.35f);
  c[ImGuiCol_NavHighlight] = kAccent;
  c[ImGuiCol_ScrollbarBg] = kSurface;
  c[ImGuiCol_ScrollbarGrab] = kSurface2;
}

Fonts LoadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  auto* data = const_cast<unsigned char*>(kFontData);
  const int size = static_cast<int>(sizeof(kFontData));
  Fonts fonts{};
  fonts.body = io.Fonts->AddFontFromMemoryTTF(data, size, 18.0f * scale, &config);
  fonts.title = io.Fonts->AddFontFromMemoryTTF(data, size, 36.0f * scale, &config);
  return fonts;
}

int GamertagFilter(ImGuiInputTextCallbackData* data) {
  return GamertagChar(static_cast<unsigned char>(data->EventChar)) ? 0 : 1;
}

// The gradient marks it ready; a disabled one is flat gray so the two never look alike.
bool ShowButton(const char* label, ImVec2 size, bool enabled) {
  const bool pressed = ImGui::InvisibleButton(label, size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  // AddRectFilledMultiColor has no rounding, so clip the gradient to the
  // button rect instead of overpainting the surroundings.
  if (!enabled) {
    draw->AddRectFilled(min, max, ImGui::GetColorU32(kSurface2), ImGui::GetStyle().FrameRounding);
  } else {
    ImGui::PushClipRect(min, max, true);
    ShowGradient(draw, min, max, ImGui::IsItemActive() ? 0.8f : 1.0f);
    if (ImGui::IsItemHovered()) draw->AddRectFilled(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.12f)));
    ImGui::PopClipRect();
  }
  draw->AddRect(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.14f)), ImGui::GetStyle().FrameRounding);
  const ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
  draw->AddText(ImVec2(min.x + (size.x - text.x) * 0.5f, min.y + (max.y - min.y - text.y) * 0.5f),
                ImGui::GetColorU32(enabled ? kText : kText3), label);
  return pressed;
}

bool LinkText(const char* label, const char* url) {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsItemClicked()) {
    SDL_OpenURL(url);
    return true;
  }
  return false;
}

struct Status {
  std::string text;
  bool error = false;
};

struct App {
  Paths paths;
  // Buffers are deliberately larger than the validated limits so overlong
  // input reaches validation instead of being silently truncated by ImGui.
  std::array<char, 64> gamertag{};
  std::array<char, 512> host{};
  bool windowed = false;
  bool keyboard = false;
  bool mouse_look = false;
  bool native_renderer = false;
  bool allow_invalid_fetch_constants = true;
  Status status;
  SDL_Window* window = nullptr;
  // SDL's user event for file dialog results; none when SDL ran out of event types.
  std::optional<Uint32> dialog_event;
  FilesCheck files;
  std::chrono::steady_clock::time_point filesChecked;
  std::string contentSetting = "content";
  std::vector<ConfigEntry> others;
  std::vector<std::string> removed;
  bool advanced = false;
  std::array<char, 64> newKey{};
  std::array<char, 512> newValue{};
  // Avatar tab.
  SDL_Renderer* renderer = nullptr;
  std::future<std::unique_ptr<av::Catalog>> catalogLoad;
  std::unique_ptr<av::Catalog> catalog;
  bool catalogFailed = false;
  std::optional<av::Description> avatar;
  bool avatarUnsaved = false;
  av::Scene scene;
  float yaw = 0.0f;
  bool previewDirty = false;
  SDL_Texture* preview = nullptr;
  std::vector<uint8_t> previewPixels;
  std::array<char, 8192> code{};
  std::array<char, 128> xboxGamertag{};
  void Report(const Error& error) { status = {Describe(error), true}; }
  void Report(std::string text) { status = {std::move(text), false}; }
  void ReportError(std::string text) { status = {std::move(text), true}; }

  bool Busy() { return catalogLoad.valid(); }

  // The pack table is large, so it loads off the UI thread the first time the Avatar tab opens.
  void StartCatalog() {
    if (catalog || catalogFailed || catalogLoad.valid() || !files.hasAvatar) return;
    catalogLoad = std::async(std::launch::async, [packs = files.avatarPacks] { return av::LoadPackCatalog(packs); });
  }

  void PollCatalog() {
    if (!catalogLoad.valid() || catalogLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
      catalog = catalogLoad.get();
    } catch (const std::exception&) {
      catalog.reset();
    }
    if (!catalog || !catalog->loaded()) {
      catalog.reset();
      catalogFailed = true;
      ReportError("The avatar pack table did not load. See the Files tab.");
      return;
    }
    LoadStoredAvatar();
    // A code pasted while the pack was loading.
    if (code[0]) ImportCode();
  }

  void ImportCode() {
    const std::string_view text(code.data());
    const auto decoded = av::DecodeAvatarText(text);
    if (decoded && (decoded->size() == av::kManifestBytes || decoded->size() == av::kDescriptionBytes)) {
      ImportAvatar(*decoded);
    } else if (!text.empty()) {
      Report("That is not a complete avatar code yet.");
    }
  }

  void ShowAvatar(const av::Description& description, bool unsaved) {
    avatar = description;
    avatarUnsaved = unsaved;
    scene = av::BuildScene(*catalog, description);
    previewDirty = true;
  }

  void LoadStoredAvatar() {
    avatar.reset();
    avatarUnsaved = false;
    std::error_code ec;
    if (!fs::is_regular_file(paths.avatar, ec)) return;
    auto bytes = ReadBytes(paths.avatar);
    av::Description description;
    if (!bytes || !av::ParseAnyDescription(catalog.get(), bytes->data(), bytes->size(), &description)) {
      ReportError("The saved avatar cannot be read. Open or paste a new one.");
      return;
    }
    ShowAvatar(description, false);
  }

  // A manifest or description file, or its text form.
  void ImportAvatar(std::vector<uint8_t> bytes) {
    if (!catalog) {
      ReportError("The avatar pack table is not loaded yet. See the Files tab.");
      return;
    }
    const auto fits = [](size_t size) { return size == av::kManifestBytes || size == av::kDescriptionBytes; };
    if (!fits(bytes.size())) {
      auto decoded = av::DecodeAvatarText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      if (!decoded) {
        ReportError("That is neither an avatar file nor an avatar code.");
        return;
      }
      bytes = std::move(*decoded);
    }
    av::Description description;
    if (!fits(bytes.size()) || !av::ParseAnyDescription(catalog.get(), bytes.data(), bytes.size(), &description)) {
      ReportError("That is not an avatar.");
      return;
    }
    // Marketplace items this PC lacks become similar standard items, as in the game.
    const size_t similar = av::SubstituteMissingItems(*catalog, &description);
    ShowAvatar(description, true);
    Report(similar ? std::to_string(similar) +
                         " item(s) you do not have became similar standard items. Launch saves this avatar."
                   : std::string("Launch saves this avatar."));
  }

  std::expected<void, Error> SaveAvatar() {
    if (!avatar || !avatarUnsaved) return {};
    const auto bytes = av::SerializeDescription(*avatar);
    if (auto written = WriteBytes(paths.avatar, bytes.data(), bytes.size()); !written) return written;
    avatarUnsaved = false;
    return {};
  }

  // The game needs one to start.
  bool HasAvatar() const {
    std::error_code ec;
    return avatar.has_value() || fs::is_regular_file(paths.avatar, ec);
  }

  void ExportAvatar(const fs::path& target) {
    // The manifest does not record the owner, so no profile id is needed.
    const auto bytes = av::SerializeManifest(catalog.get(), *avatar, 0);
    if (auto written = WriteBytes(target, bytes.data(), bytes.size()); !written) {
      Report(written.error());
      return;
    }
    Report("Exported to " + PathText(target) + ".");
  }

  void UpdatePreview() {
    if (!previewDirty || !avatar || !renderer) return;
    previewDirty = false;
    if (!preview) {
      preview = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kPreviewWidth,
                                  kPreviewHeight);
      if (!preview) return;
    }
    av::RenderIdle(*catalog, scene, kPreviewWidth, kPreviewHeight, yaw, &previewPixels);
    SDL_UpdateTexture(preview, nullptr, previewPixels.data(), kPreviewWidth * 4);
  }

  void Load() {
    if (auto name = LoadGamertag(paths.gamertag)) SDL_strlcpy(gamertag.data(), name->c_str(), gamertag.size());
    auto settings = LoadSettings(paths.config);
    if (!settings) {
      SDL_strlcpy(host.data(), kDefaultHost, host.size());
      Report(settings.error());
      RefreshFiles(*this);
      return;
    }
    SDL_strlcpy(host.data(), settings->host.c_str(), host.size());
    windowed = settings->windowed;
    contentSetting = settings->content;
    keyboard = settings->keyboard;
    mouse_look = settings->mouse_look;
    native_renderer = settings->native_renderer;
    allow_invalid_fetch_constants = settings->allow_invalid_fetch_constants;
    others = std::move(settings->others);
    RefreshFiles(*this);
  }

  void SetContentFolder(std::string folder) {
    contentSetting = std::move(folder);
    RefreshFiles(*this);
    Report("Launch saves the content folder.");
  }

  std::expected<void, Error> Save() {
    if (auto saved = SaveGamertag(paths, gamertag.data()); !saved) return saved;
    if (auto saved = SaveAvatar(); !saved) return saved;
    Settings out;
    out.host = host.data();
    out.windowed = windowed;
    out.content = contentSetting;
    out.keyboard = keyboard;
    out.mouse_look = mouse_look;
    out.native_renderer = native_renderer;
    out.allow_invalid_fetch_constants = allow_invalid_fetch_constants;
    out.others = others;
    out.removed = removed;
    if (auto saved = SaveSettings(paths.config, out); !saved) return saved;
    removed.clear();
    return {};
  }
};

void RefreshFiles(App& app) {
  app.filesChecked = std::chrono::steady_clock::now();
  FilesCheck check;
  check.dir = ResolveContentDir(app.paths, app.contentSetting);
  check.listing = ListFolder(check.dir);
  std::error_code ec;
  check.dirOk = fs::is_directory(check.dir, ec);
  if (check.dirOk) {
    auto manifest = LoadManifest(app.paths.base / "manifest.txt");
    check.manifestOk = manifest.has_value();
    if (manifest) {
      for (const auto& entry : *manifest) {
        ContentFile file;
        file.name = entry.name;
        file.role = entry.kind == "game"    ? FileRole::Game
                    : entry.kind == "launch" ? FileRole::Launch
                    : entry.kind == "show"   ? FileRole::Show
                    : entry.kind == "avatar" ? FileRole::Avatar
                                             : FileRole::Episode;
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
  // A load that failed may work with what is there now, such as a package that finished copying.
  if (!app.catalog && check.avatarPacks != app.files.avatarPacks) app.catalogFailed = false;
  app.files = std::move(check);
}

enum class DialogKind : intptr_t { OpenAvatar, ExportAvatar, ContentFolder };

struct DialogRequest {
  App* app;
  DialogKind kind;
};

void SDLCALL OnFileChosen(void* userdata, const char* const* files, int) {
  auto* request = static_cast<DialogRequest*>(userdata);
  SDL_Event event{};
  event.type = *request->app->dialog_event;
  // A null list is an error, an empty list a cancel.
  event.user.code = files ? 0 : 1;
  event.user.data1 = (files && files[0]) ? SDL_strdup(files[0]) : nullptr;
  event.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(request->kind));
  SDL_PushEvent(&event);
}

DialogRequest open_request{nullptr, DialogKind::OpenAvatar};
DialogRequest export_request{nullptr, DialogKind::ExportAvatar};
DialogRequest folder_request{nullptr, DialogKind::ContentFolder};

void HandleFileChosen(App& app, const SDL_UserEvent& event) {
  if (event.code) {
    const char* sdl_error = SDL_GetError();
    app.ReportError(std::string("File dialog failed.") +
                    ((sdl_error && *sdl_error) ? std::string(" ") + sdl_error : std::string()));
    return;
  }
  auto* chosen = static_cast<char*>(event.data1);
  if (!chosen) return;
  const fs::path path = PathFromUtf8(chosen);
  SDL_free(chosen);
  const auto kind = static_cast<DialogKind>(reinterpret_cast<intptr_t>(event.data2));
  if (kind == DialogKind::ExportAvatar) {
    if (app.avatar) app.ExportAvatar(path);
    return;
  }
  if (kind == DialogKind::ContentFolder) {
    app.SetContentFolder(PathText(path));
    return;
  }
  auto bytes = ReadBytes(path);
  if (!bytes) {
    app.Report(bytes.error());
    return;
  }
  app.ImportAvatar(std::move(*bytes));
}

void SectionLabel(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

void DrawPlayerTab(App& app) {
  SectionLabel("GAMERTAG");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##gamertag", "Your name in the game", app.gamertag.data(), app.gamertag.size(),
                           ImGuiInputTextFlags_CallbackCharFilter, GamertagFilter);
  const std::string_view name(app.gamertag.data());
  if (!name.empty() && !ValidGamertag(name)) {
    ImGui::TextColored(kError, "1-15 chars: letters, numbers, spaces, _ or -; no leading/trailing space.");
  } else {
    SectionLabel("1-15 letters, numbers, spaces, _ or -");
  }
}

void Spinner(ImVec2 center) {
  const float radius = ImGui::GetFontSize();
  const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PathArcTo(center, radius, start, start + 4.5f, 24);
  draw->PathStroke(ImGui::GetColorU32(kAccent), 0, radius * 0.2f);
}

void DrawAvatarTab(App& app) {
  app.StartCatalog();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float preview_width = kPreviewWidth * ImGui::GetFontSize() / 18.0f;
  const float preview_height = preview_width * kPreviewHeight / kPreviewWidth;
  const float left_width = ImGui::GetContentRegionAvail().x - preview_width - style.ItemSpacing.x * 3;

  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + left_width);
  static constexpr const char* kSteps[] = {
      "Create an account at 360sona.com and sign in.",
      "In the editor, click My Sonas, select your avatar and click Import / Export.",
      "Copy the 360sona avatar text and paste it below. Or click Save the description (.bin) and open that "
      "file here. Or enter your Xbox gamertag below, press Download and open the saved file.",
      "Check the preview, then press Launch.",
  };
  for (size_t i = 0; i < std::size(kSteps); ++i) {
    ImGui::Text("%zu.", i + 1);
    ImGui::SameLine();
    ImGui::TextUnformatted(kSteps[i]);
  }
  ImGui::PopTextWrapPos();
  if (ImGui::Button("Open 360sona.com")) SDL_OpenURL(kAvatarEditorUrl);

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("SONA CODE");
  const bool ready = app.catalog != nullptr;
  // A complete code shows at once, whether typed, pasted with Ctrl+V or with the button.
  bool changed = ImGui::InputTextMultiline("##code", app.code.data(), app.code.size(),
                                           ImVec2(left_width, ImGui::GetTextLineHeight() * 4));
  ImGui::BeginDisabled(!ready);
  if (ImGui::Button("Paste")) {
    char* clipboard = SDL_GetClipboardText();
    SDL_strlcpy(app.code.data(), clipboard ? clipboard : "", app.code.size());
    SDL_free(clipboard);
    changed = true;
  }
  ImGui::EndDisabled();
  if (changed && ready) app.ImportCode();
  ImGui::SameLine();
  ImGui::BeginDisabled(!ready || !app.dialog_event);
  if (ImGui::Button("Open file...")) {
    static const SDL_DialogFileFilter filters[] = {{"Avatar manifest or sona code", "bin;txt"}};
    open_request.app = &app;
    SDL_ShowOpenFileDialog(OnFileChosen, &open_request, app.window, filters, 1, nullptr, false);
  }
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("XBOX GAMERTAG");
  const float download_width = ImGui::CalcTextSize("Download").x + style.FramePadding.x * 2;
  ImGui::SetNextItemWidth(left_width - download_width - style.ItemSpacing.x);
  ImGui::InputTextWithHint("##xboxgamertag", "Your Xbox gamertag", app.xboxGamertag.data(), app.xboxGamertag.size());
  ImGui::SameLine();
  const auto xbox_gamertag = XboxGamertag::Parse(app.xboxGamertag.data());
  ImGui::BeginDisabled(!xbox_gamertag);
  if (ImGui::Button("Download")) {
    if (SDL_OpenURL(SonaManifestUrl(*xbox_gamertag).c_str())) {
      app.Report("Save the file from the browser, then open it with Open file...");
    } else {
      const char* sdl_error = SDL_GetError();
      app.ReportError(std::string("Cannot open the browser.") +
                      ((sdl_error && *sdl_error) ? std::string(" ") + sdl_error : std::string()));
    }
  }
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("YOUR AVATAR");
  ImGui::BeginDisabled(!app.avatar || !app.dialog_event);
  if (ImGui::Button("Export...")) {
    static const SDL_DialogFileFilter filters[] = {{"Avatar manifest", "bin"}};
    export_request.app = &app;
    const std::string location = PathText(app.paths.base / "avatarmanifest.bin");
    SDL_ShowSaveFileDialog(OnFileChosen, &export_request, app.window, filters, 1, location.c_str());
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (!app.files.hasAvatar) {
    ImGui::TextColored(kError, "The avatar pack table is missing. See the Files tab.");
  } else if (app.Busy()) {
    ImGui::TextUnformatted("Loading the avatar pack...");
  } else if (!app.avatar) {
    ImGui::TextColored(kError, "No avatar yet. Paste a code or open a file.");
  } else if (app.avatarUnsaved) {
    ImGui::TextColored(kAccent, "New. Launch saves it.");
  } else {
    ImGui::TextUnformatted("Saved.");
  }
  ImGui::EndGroup();

  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  ImGui::BeginGroup();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + preview_width, at.y + preview_height),
                                            ImGui::GetColorU32(Hex(0x262930)), style.FrameRounding);
  app.UpdatePreview();
  if (app.avatar && app.preview) {
    ImGui::Image(reinterpret_cast<ImTextureID>(app.preview), ImVec2(preview_width, preview_height));
    // Dragging across the preview turns the avatar too.
    if (ImGui::IsItemActive() || (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
      const float delta = ImGui::GetIO().MouseDelta.x;
      if (delta != 0.0f) {
        app.yaw += delta * 0.02f;
        app.previewDirty = true;
      }
    }
  } else {
    ImGui::Dummy(ImVec2(preview_width, preview_height));
    if (app.Busy()) Spinner(ImVec2(at.x + preview_width * 0.5f, at.y + preview_height * 0.5f));
  }
  const float arrow = ImGui::GetFrameHeight();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (preview_width - arrow * 2 - style.ItemSpacing.x) * 0.5f);
  ImGui::BeginDisabled(!app.avatar);
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
    app.yaw -= kTurnStep;
    app.previewDirty = true;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
    app.yaw += kTurnStep;
    app.previewDirty = true;
  }
  ImGui::PopItemFlag();
  ImGui::EndDisabled();
  ImGui::EndGroup();
}

// Every other key in onevs100.toml. Saved with the rest when you press Launch.
void DrawAdvancedSettings(App& app) {
  ImGui::TextWrapped("Other keys in onevs100.toml. They are saved when you press Launch.");
  LinkText("What each setting does", kDocsUrl);
  const ImGuiStyle& style = ImGui::GetStyle();
  const float remove_width = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2;
  if (ImGui::BeginTable("##others", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed, remove_width);
    std::optional<size_t> remove;
    for (size_t i = 0; i < app.others.size(); ++i) {
      ConfigEntry& entry = app.others[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(entry.key.c_str());
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      switch (entry.kind) {
        case ConfigEntry::Kind::Flag: ImGui::Checkbox("##value", &entry.flag); break;
        case ConfigEntry::Kind::Integer: {
          ImGui::InputScalar("##value", ImGuiDataType_S64, &entry.integer);
          break;
        }
        case ConfigEntry::Kind::Number: ImGui::InputDouble("##value", &entry.number); break;
        case ConfigEntry::Kind::Text:
          ImGui::InputText("##value", &entry.text);
          break;
      }
      ImGui::TableNextColumn();
      if (ImGui::Button("Remove")) remove = i;
      ImGui::PopID();
    }
    if (remove) {
      app.removed.push_back(app.others[*remove].key);
      app.others.erase(app.others.begin() + static_cast<std::ptrdiff_t>(*remove));
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newkey", "name", app.newKey.data(), app.newKey.size(),
                             ImGuiInputTextFlags_CharsNoBlank);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newvalue", "value", app.newValue.data(), app.newValue.size());
    ImGui::TableNextColumn();
    const std::string_view key(app.newKey.data());
    const bool taken = ManagedKey(key) || std::any_of(app.others.begin(), app.others.end(),
                                                      [&](const ConfigEntry& entry) { return entry.key == key; });
    ImGui::BeginDisabled(!ValidConfigKey(key) || taken);
    if (ImGui::Button("Add", ImVec2(remove_width, 0))) {
      app.others.push_back(EntryFromText(std::string(key), app.newValue.data()));
      std::erase(app.removed, std::string(key));
      app.newKey.fill(0);
      app.newValue.fill(0);
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
  }
  if (ManagedKey(app.newKey.data())) SectionLabel("That setting has its own control above.");
}

void DrawSettingsTab(App& app) {
  SectionLabel("GAMESERVER HOST");
  const float reset_width = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(-(reset_width + ImGui::GetStyle().ItemSpacing.x));
  ImGui::InputTextWithHint("##host", kDefaultHost, app.host.data(), app.host.size(),
                           ImGuiInputTextFlags_CharsNoBlank);
  ImGui::SameLine();
  ImGui::BeginDisabled(std::strcmp(app.host.data(), kDefaultHost) == 0);
  if (ImGui::Button("Reset")) SDL_strlcpy(app.host.data(), kDefaultHost, app.host.size());
  ImGui::EndDisabled();
  SectionLabel("Default: 1vs100.fans. Hostname or IPv4, no spaces, : or /.");
  if (!std::string_view(app.host.data()).empty() && !ValidHost(app.host.data()))
    ImGui::TextColored(kError, "That host will not resolve: use a plain hostname or IPv4 address.");

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("DISPLAY");
  ImGui::Checkbox("Windowed mode", &app.windowed);
  if (kHasNativeRenderer) ImGui::Checkbox("Native renderer", &app.native_renderer);
  ImGui::Checkbox("Allow invalid fetch constants", &app.allow_invalid_fetch_constants);

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("CONTROLS");
  ImGui::Checkbox("Keyboard controls (WASD + arrows, Space = A)", &app.keyboard);
  ImGui::BeginDisabled(!app.keyboard);
  ImGui::Checkbox("Mouse aims the right stick", &app.mouse_look);
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Checkbox("Show all settings", &app.advanced);
  if (app.advanced) DrawAdvancedSettings(app);
}

// Green: as expected. Yellow: present by name, but not what the manifest or the game expects. Red: missing.
enum class Indicator { Ok, Caution, Missing };

void StatusIcon(Indicator indicator) {
  const bool ok = indicator == Indicator::Ok;
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 center(at.x + size * 0.5f, at.y + size * 0.5f);
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 ring = ImGui::GetColorU32(ok                                ? ImVec4(0.20f, 0.78f, 0.35f, 1.0f)
                                        : indicator == Indicator::Caution ? kCaution
                                                                          : kError);
  const ImU32 mark = ImGui::GetColorU32(Hex(0xffffff));
  draw->AddCircle(center, size * 0.5f, ring, 20, size * 0.12f);
  if (indicator == Indicator::Caution) {
    draw->AddLine(ImVec2(center.x, center.y - size * 0.24f), ImVec2(center.x, center.y + size * 0.06f), mark,
                  size * 0.11f);
    draw->AddCircleFilled(ImVec2(center.x, center.y + size * 0.22f), size * 0.06f, mark);
  } else if (ok) {
    const ImVec2 p1(center.x - size * 0.20f, center.y);
    const ImVec2 p2(center.x - size * 0.02f, center.y + size * 0.18f);
    const ImVec2 p3(center.x + size * 0.24f, center.y - size * 0.18f);
    draw->AddLine(p1, p2, mark, size * 0.10f);
    draw->AddLine(p2, p3, mark, size * 0.10f);
  } else {
    const float d = size * 0.18f;
    draw->AddLine(ImVec2(center.x - d, center.y - d), ImVec2(center.x + d, center.y + d), mark,
                  size * 0.10f);
    draw->AddLine(ImVec2(center.x - d, center.y + d), ImVec2(center.x + d, center.y - d), mark,
                  size * 0.10f);
  }
}

const char* RoleLabel(FileRole role) {
  switch (role) {
    case FileRole::Game: return "Game package";
    case FileRole::Launch: return "Launch content";
    case FileRole::Show: return "Show pack";
    case FileRole::Episode: return "Episode pack";
    case FileRole::ShowEpisode: return "Show / episode pack";
    case FileRole::Avatar: return "Avatar pack table";
    case FileRole::Other: return "Other content";
    case FileRole::Invalid: return "Not a package";
  }
  return "";
}

void FileRow(Indicator indicator, const char* label, const std::string& detail) {
  StatusIcon(indicator);
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
  if (!detail.empty()) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(detail.c_str());
    ImGui::PopStyleColor();
  }
}

void ContentRow(const ContentFile& file) {
  if (!file.present) {
    FileRow(Indicator::Missing, RoleLabel(file.role),
            file.name + (file.role == FileRole::Avatar ? " - missing, or add the other avatar file" : " - missing"));
  } else if (!file.valid) {
    FileRow(Indicator::Caution, RoleLabel(file.role), file.name + " - " + file.note);
  } else {
    FileRow(Indicator::Ok, RoleLabel(file.role), file.name);
  }
}

// Files copied in while the launcher is in front show up within a few seconds.
// Package headers are read again only when the folder listing changed.
void CheckFiles(App& app) {
  const auto now = std::chrono::steady_clock::now();
  if (now - app.filesChecked < std::chrono::seconds(2)) return;
  app.filesChecked = now;
  if (ListFolder(app.files.dir) != app.files.listing) RefreshFiles(app);
}

void DrawFilesTab(App& app) {
  const FilesCheck& check = app.files;
  ImGui::TextWrapped("Copy the packages from your Xbox 360 into the content folder: the game, its launch content, the "
                     "show and episode packs, and the avatar pack (FFFE07DF00000002 or AvatarAssetPack.toc). Put "
                     "them straight in the folder, untouched and without subfolders. This list updates by itself, "
                     "and Launch unlocks once every row is green.");
  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("CONTENT FOLDER");
  ImGui::TextWrapped("%s", PathText(check.dir).c_str());
  ImGui::BeginDisabled(!app.dialog_event);
  if (ImGui::Button("Set folder...")) {
    folder_request.app = &app;
    const std::string location = PathText(check.dir);
    SDL_ShowOpenFolderDialog(OnFileChosen, &folder_request, app.window, location.c_str(), false);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(app.contentSetting == kDefaultContent);
  if (ImGui::Button("Reset")) app.SetContentFolder(kDefaultContent);
  ImGui::EndDisabled();
  ImGui::SameLine();
  SectionLabel("Default: the content folder next to the launcher.");
  if (!check.dirOk) {
    FileRow(Indicator::Missing, "Folder missing", "create it next to the launcher and add the game packages");
  } else if (check.files.empty()) {
    FileRow(Indicator::Missing, "No files", "copy the untouched game, show and episode packages here");
  }
  auto drawRole = [&](FileRole role) {
    for (const auto& file : check.files) {
      if (file.role == role) ContentRow(file);
    }
  };
  auto hasRole = [&](FileRole role) {
    for (const auto& file : check.files) {
      if (file.role == role) return true;
    }
    return false;
  };
  drawRole(FileRole::Game);
  if (check.dirOk && !hasRole(FileRole::Game)) FileRow(Indicator::Missing, "Game package", "not found");
  drawRole(FileRole::Launch);
  if (check.dirOk && !hasRole(FileRole::Launch)) FileRow(Indicator::Missing, "Launch content", "not found");
  // One pack is enough to play, so a partial set is a caution, not an error.
  auto packSummary = [&](const char* label, FileRole role, size_t found) {
    const size_t expected = static_cast<size_t>(
        std::count_if(check.files.begin(), check.files.end(), [&](const ContentFile& file) { return file.role == role && !file.duplicate; }));
    const std::string detail = std::to_string(found) + " of " + std::to_string(expected) + " found";
    FileRow(found == expected ? Indicator::Ok : found ? Indicator::Caution : Indicator::Missing, label, detail);
    drawRole(role);
  };
  if (check.manifestOk) {
    packSummary("Show packs", FileRole::Show, check.showCount);
    packSummary("Episode packs", FileRole::Episode, check.episodeCount);
  } else {
    char packCount[64] = {};
    SDL_snprintf(packCount, sizeof(packCount), "%zu found", check.showEpisodeCount);
    if (check.showEpisodeCount > 0)
      FileRow(Indicator::Ok, "Show / episode packs", packCount);
    else
      FileRow(Indicator::Missing, "Show / episode packs", "none found");
    drawRole(FileRole::ShowEpisode);
  }
  drawRole(FileRole::Other);
  drawRole(FileRole::Invalid);
  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("AVATAR DATA");
  // Either the pack table or the package holding it is enough, so once one is there the other is not missing.
  for (const auto& file : check.files) {
    if (file.role == FileRole::Avatar && (file.present || !check.hasAvatar)) ContentRow(file);
  }
  if (check.dirOk && !hasRole(FileRole::Avatar))
    FileRow(Indicator::Missing, "Avatar pack table", "add AvatarAssetPack.toc or the FFFE07DF00000002 package");
}

// Returns true when the launcher should close.
bool DrawFrame(App& app, const Fonts& fonts) {
  bool close = false;
  app.PollCatalog();
  CheckFiles(app);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::PushFont(fonts.title);
  ImGui::TextUnformatted("1 vs 100");
  ImGui::PopFont();
  SectionLabel("PC launcher");
  {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ShowGradient(ImGui::GetWindowDrawList(), at, ImVec2(at.x + width, at.y + 3));
    ImGui::Dummy(ImVec2(width, 3));
  }
  ImGui::Dummy(ImVec2(0, 4));

  const ImGuiStyle& style = ImGui::GetStyle();
  const float line = ImGui::GetTextLineHeightWithSpacing();
  const float launch_height = ImGui::GetFrameHeight() * 1.6f;
  const float footer = launch_height + line * 5 + style.ItemSpacing.y * 5;
  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Player")) {
      ImGui::BeginChild("##player", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawPlayerTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Avatar")) {
      ImGui::BeginChild("##avatar", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawAvatarTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings")) {
      ImGui::BeginChild("##settings", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawSettingsTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Files")) {
      ImGui::BeginChild("##files", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawFilesTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  // Two lines are reserved for the status so the layout does not shift.
  const float status_top = ImGui::GetCursorPosY();
  ImGui::PushStyleColor(ImGuiCol_Text, app.status.error ? kError
                                                        : style.Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("%s", app.status.text.c_str());
  ImGui::PopStyleColor();
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), status_top + line * 2));

  const bool can_launch = app.files.ok() && app.HasAvatar();
  ImGui::BeginDisabled(!can_launch);
  if (ShowButton("LAUNCH", ImVec2(ImGui::GetContentRegionAvail().x, launch_height), can_launch)) {
    if (auto saved = app.Save(); !saved) {
      app.Report(saved.error());
    } else if (auto launched = LaunchGame(app.paths); !launched) {
      app.Report(launched.error());
    } else {
      close = true;
    }
  }
  ImGui::EndDisabled();

  if (!app.files.ok()) {
    ImGui::TextColored(kError, "Missing files. See the Files tab before launching.");
  } else if (!app.HasAvatar()) {
    ImGui::TextColored(kError, "Choose an avatar on the Avatar tab before launching.");
  } else {
    SectionLabel("Launch saves your gamertag, avatar and settings.");
  }

  LinkText("1vs100.fans", kWebsiteUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("Documentation", kDocsUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("GitHub", kGithubUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("Discord", kDiscordUrl);
  ImGui::AlignTextToFramePadding();
  SectionLabel("1 vs 100 Reborn, build " ONEVS100_BUILD_VERSION);
  {
    const float closeWidth = ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
    if (ImGui::Button("Close")) close = true;
  }
  ImGui::End();
  return close;
}

}  // namespace

int main(int, char**) {
  SDL_SetHint(SDL_HINT_APP_NAME, "1 vs 100 Launcher");
  // The click that focuses the window also presses what is under the pointer.
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) > 0
                          ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())
                          : 1.0f;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("1 vs 100", static_cast<int>(800 * scale), static_cast<int>(900 * scale), 0,
                                   &window, &renderer)) {
    SDL_Log("Window creation failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ApplyStyle(scale);
  const Fonts fonts = LoadFonts(scale);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  App app;
  app.paths = MakePaths();
  app.window = window;
  app.renderer = renderer;
  // SDL_RegisterEvents returns 0 when no event types are left.
  if (const Uint32 type = SDL_RegisterEvents(1)) app.dialog_event = type;
  if (!app.dialog_event) {
    SDL_Log("No user event for the file dialogs; they are disabled.");
    app.status = {"File dialogs are unavailable in this build.", true};
  }
  app.Load();

  bool running = true;
  while (running) {
    SDL_Event event;
    // Idle until input arrives; the UI has no animation. Poll faster while the avatar pack loads.
    if (SDL_WaitEventTimeout(&event, app.Busy() ? 50 : 500)) {
      do {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) running = false;
        // Coming back from a file manager is when new files usually appear.
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) RefreshFiles(app);
        if (app.dialog_event && event.type == *app.dialog_event)
          HandleFileChosen(app, event.user);
      } while (SDL_PollEvent(&event));
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) continue;

    // ImGui applies one press or release per frame, so a quick click needs a
    // second frame to land before the loop goes idle again.
    for (int pass = 0; pass < 2 && running; ++pass) {
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      if (DrawFrame(app, fonts)) running = false;
      ImGui::Render();
      SDL_SetRenderDrawColor(renderer, 0x0d, 0x0b, 0x1f, 255);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      SDL_RenderPresent(renderer);
    }
  }

  if (app.preview) SDL_DestroyTexture(app.preview);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
