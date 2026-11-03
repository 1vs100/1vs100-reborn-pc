// Player launcher: edits the gamertag, avatar and onevs100.toml next to the
// game, then starts it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "font_data.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kDefaultHost = "1vs100.fans";
constexpr const char* kDiscordUrl = "https://discord.gg/tV3ajhwFTf";
constexpr const char* kWebsiteUrl = "https://1vs100.fans";
constexpr const char* kAvatarEditorUrl = "https://360sona.com";
constexpr size_t kGamertagMax = 15;
// Avatar description sizes the game accepts (xna_avatar_format.h).
constexpr std::array<uintmax_t, 2> kAvatarSizes = {1020, 1000};
// Content families the game looks for (native/src/content_install.cpp and the
// observed 1vs100 packages: show/episode packs share 584A07DC, the launch
// content uses 584A07E4).
constexpr uint32_t kGameTitleId = 0x584A07D1;
constexpr uint32_t kGameMediaId = 0x20FFDD23;
constexpr uint32_t kLaunchTitleId = 0x584A07E4;
constexpr uint32_t kShowEpisodeTitleId = 0x584A07DC;
// STFS header fields (big-endian u32): magic "LIVE" at 0, content_type at
// 0x344, media_id at 0x354, title_id at 0x360.
constexpr uint32_t kStfsMagic = 0x4C495645;
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
  const fs::path base =
      base_path ? PathFromUtf8(base_path) : fs::current_path();
  const fs::path profiles = base / "userdata" / "profiles";
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
  AvatarWrongSize,
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
    case ErrorKind::AvatarWrongSize: return "Not an avatar description: " + path + " (" + error.detail + ")";
    case ErrorKind::AvatarUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::GameMissing: return "Game not found: " + path;
    case ErrorKind::LaunchFailed: return "Cannot start the game: " + error.detail;
  }
  return {};
}

struct Settings {
  std::string host;
  bool windowed = false;
  std::string content = "content";
  std::string avatarAssets;
  bool keyboard = false;
  bool mouse_look = false;
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
  if (auto assets = table["native_avatar_content"].value<std::string>()) settings.avatarAssets = *assets;
  if (auto keyboard = table["mnk_mode"].value<bool>()) settings.keyboard = *keyboard;
  if (auto mouse_look = table["mnk_mouse"].value<bool>()) settings.mouse_look = *mouse_look;
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
  table.insert_or_assign("gameserver_host", settings.host);
  table.insert_or_assign("fullscreen", !settings.windowed);
  table.insert_or_assign("mnk_mode", settings.keyboard);
  table.insert_or_assign("mnk_mouse", settings.mouse_look);
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

std::expected<void, Error> ImportAvatar(const Paths& paths, const fs::path& source) {
  std::error_code ec;
  const uintmax_t size = fs::file_size(source, ec);
  if (ec) return std::unexpected(Error{ErrorKind::AvatarUnreadable, source, ec.message()});
  if (std::find(kAvatarSizes.begin(), kAvatarSizes.end(), size) == kAvatarSizes.end())
    return std::unexpected(Error{ErrorKind::AvatarWrongSize, source, std::to_string(size) + " bytes"});
  fs::create_directories(paths.profiles, ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, paths.profiles, ec.message()});
  fs::copy_file(source, paths.avatar, fs::copy_options::overwrite_existing, ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, paths.avatar, ec.message()});
  return {};
}

enum class FileRole { Game, Launch, Show, Episode, ShowEpisode, Other, Invalid };

struct ContentFile {
  std::string name;
  FileRole role = FileRole::Invalid;
  bool present = false;
  bool valid = false;
  std::string note;
};

struct ManifestEntry {
  std::string kind;
  std::string name;
  uint64_t size = 0;
};

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

struct FilesCheck {
  fs::path dir;
  bool dirOk = false;
  bool manifestOk = false;
  std::vector<ContentFile> files;
  bool hasGame = false;
  bool hasLaunch = false;
  size_t showCount = 0;
  size_t episodeCount = 0;
  size_t showEpisodeCount = 0;
  std::string avatarAssetsSetting;
  fs::path avatarAssetsPath;
  bool avatarAssetsOk = false;
  bool ok() const {
    if (!dirOk || !hasGame || !hasLaunch || !avatarAssetsOk) return false;
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
  if (ReadBigEndian32(head) != kStfsMagic) return std::nullopt;
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
  return FileRole::Other;
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

bool ShowButton(const char* label, ImVec2 size) {
  const bool pressed = ImGui::InvisibleButton(label, size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  // AddRectFilledMultiColor has no rounding, so clip the gradient to the
  // button rect instead of overpainting the surroundings.
  ImGui::PushClipRect(min, max, true);
  ShowGradient(draw, min, max, ImGui::IsItemActive() ? 0.8f : 1.0f);
  if (ImGui::IsItemHovered()) draw->AddRectFilled(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.12f)));
  ImGui::PopClipRect();
  draw->AddRect(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.14f)), ImGui::GetStyle().FrameRounding);
  const ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
  draw->AddText(ImVec2(min.x + (size.x - text.x) * 0.5f, min.y + (max.y - min.y - text.y) * 0.5f),
                ImGui::GetColorU32(Hex(0xffffff)), label);
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
  Status status;
  SDL_Window* window = nullptr;
  Uint32 dialog_event = 0;
  FilesCheck files;
  int frames = 0;
  int filesFrame = -1000000;
  std::string contentSetting = "content";
  std::string avatarAssetsSetting;
  void Report(const Error& error) { status = {Describe(error), true}; }
  void Report(std::string text) { status = {std::move(text), false}; }

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
    avatarAssetsSetting = settings->avatarAssets;
    keyboard = settings->keyboard;
    mouse_look = settings->mouse_look;
    RefreshFiles(*this);
  }

  std::expected<void, Error> Save() {
    if (auto saved = SaveGamertag(paths, gamertag.data()); !saved) return saved;
    Settings out;
    out.host = host.data();
    out.windowed = windowed;
    out.content = contentSetting;
    out.avatarAssets = avatarAssetsSetting;
    out.keyboard = keyboard;
    out.mouse_look = mouse_look;
    return SaveSettings(paths.config, out);
  }

  bool AvatarPresent() const {
    std::error_code ec;
    return fs::is_regular_file(paths.avatar, ec);
  }
};

void RefreshFiles(App& app) {
  app.filesFrame = app.frames;
  FilesCheck check;
  check.dir = ResolveContentDir(app.paths, app.contentSetting);
  std::error_code ec;
  check.dirOk = fs::is_directory(check.dir, ec);
  if (check.dirOk) {
    auto manifest = LoadManifest(app.paths.base / "manifest.txt");
    check.manifestOk = manifest.has_value();
    if (manifest) {
      for (const auto& entry : *manifest) {
        if (entry.kind == "avatar") continue;
        ContentFile file;
        file.name = entry.name;
        file.role = entry.kind == "game"    ? FileRole::Game
                    : entry.kind == "launch" ? FileRole::Launch
                    : entry.kind == "show"   ? FileRole::Show
                                             : FileRole::Episode;
        const fs::path full = check.dir / PathFromUtf8(entry.name.c_str());
        std::error_code entryEc;
        file.present = fs::is_regular_file(full, entryEc);
        if (!file.present) {
          file.note = "not found";
        } else if (fs::file_size(full, entryEc) != entry.size) {
          file.note = "size mismatch (expected " + std::to_string(entry.size) + ")";
        } else if (!PeekStfsHeader(full)) {
          file.note = "not a package";
        } else {
          file.valid = true;
        }
        check.files.push_back(std::move(file));
      }
      // Files the manifest does not know about.
      for (const auto& entry : fs::directory_iterator(check.dir, ec)) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) continue;
        const std::string name = PathText(entry.path().filename());
        bool known = false;
        for (const auto& file : check.files)
          if (file.name == name) {
            known = true;
            break;
          }
        if (known) continue;
        ContentFile file;
        file.name = name;
        file.present = true;
        if (auto header = PeekStfsHeader(entry.path())) {
          file.valid = true;
          file.role = ClassifyPackage((*header)[2], (*header)[1], (*header)[0]);
        } else {
          file.note = "not a package";
        }
        check.files.push_back(std::move(file));
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
        ContentFile file;
        file.name = PathText(entry.path().filename());
        file.present = true;
        if (auto header = PeekStfsHeader(entry.path())) {
          file.valid = true;
          file.role = ClassifyPackage((*header)[2], (*header)[1], (*header)[0]);
        } else {
          file.note = "not a package";
        }
        check.files.push_back(std::move(file));
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
    }
    check.showEpisodeCount += check.showCount + check.episodeCount;
  }
  // Avatar assets: the native_avatar_content setting, else the environment
  // the game itself also reads.
  check.avatarAssetsSetting = app.avatarAssetsSetting;
  std::string raw = app.avatarAssetsSetting;
  if (raw.empty()) {
    if (const char* env = std::getenv("ONEVS100_NATIVE_AVATAR_CONTENT")) raw = env;
  }
  if (!raw.empty()) {
    fs::path p = PathFromUtf8(raw.c_str());
    if (p.is_relative()) p = app.paths.base / p;
    check.avatarAssetsPath = p;
    check.avatarAssetsOk = fs::is_regular_file(p, ec) || fs::is_directory(p, ec);
  }
  app.files = std::move(check);
}

void SDLCALL OnAvatarChosen(void* userdata, const char* const* files, int) {
  auto* app = static_cast<App*>(userdata);
  SDL_Event event{};
  event.type = app->dialog_event;
  // A null list is an error, an empty list a cancel.
  event.user.code = files ? 0 : 1;
  event.user.data1 = (files && files[0]) ? SDL_strdup(files[0]) : nullptr;
  SDL_PushEvent(&event);
}

void HandleAvatarChosen(App& app, const SDL_UserEvent& event) {
  if (event.code) {
    const char* sdl_error = SDL_GetError();
    app.status = {std::string("File dialog failed.") +
                      ((sdl_error && *sdl_error) ? std::string(" ") + sdl_error : std::string()),
                  true};
    return;
  }
  auto* chosen = static_cast<char*>(event.data1);
  if (!chosen) return;
  const fs::path source = PathFromUtf8(chosen);
  SDL_free(chosen);
  if (auto imported = ImportAvatar(app.paths, source); !imported) {
    app.Report(imported.error());
    return;
  }
  app.Report("Avatar saved.");
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

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("AVATAR");
  if (app.AvatarPresent())
    ImGui::TextUnformatted("Custom avatar set.");
  else
    ImGui::TextUnformatted("No avatar file. The game creates one for you.");
  ImGui::BeginDisabled(app.dialog_event == static_cast<Uint32>(-1));
  if (ImGui::Button("Choose .bin file")) {
    static const SDL_DialogFileFilter filters[] = {{"Avatar description", "bin"}};
    SDL_ShowOpenFileDialog(OnAvatarChosen, &app, app.window, filters, 1, nullptr, false);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!app.AvatarPresent());
  if (ImGui::Button("Remove")) {
    std::error_code ec;
    fs::remove(app.paths.avatar, ec);
    if (ec)
      app.Report(Error{ErrorKind::WriteFailed, app.paths.avatar, ec.message()});
    else
      app.Report("Avatar removed.");
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Make one at 360sona.com")) SDL_OpenURL(kAvatarEditorUrl);
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

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("CONTROLS");
  ImGui::Checkbox("Keyboard controls (WASD + arrows, Space = A)", &app.keyboard);
  ImGui::BeginDisabled(!app.keyboard);
  ImGui::Checkbox("Mouse aims the right stick", &app.mouse_look);
  ImGui::EndDisabled();
}

void StatusIcon(bool ok) {
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 center(at.x + size * 0.5f, at.y + size * 0.5f);
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 ring = ImGui::GetColorU32(ok ? ImVec4(0.20f, 0.78f, 0.35f, 1.0f) : kError);
  const ImU32 mark = ImGui::GetColorU32(Hex(0xffffff));
  draw->AddCircle(center, size * 0.5f, ring, 20, size * 0.12f);
  if (ok) {
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
    case FileRole::Other: return "Other content";
    case FileRole::Invalid: return "Not a package";
  }
  return "";
}

void FileRow(bool ok, const char* label, const std::string& detail) {
  StatusIcon(ok);
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
    FileRow(false, RoleLabel(file.role), file.name + " - missing");
  } else if (!file.valid) {
    FileRow(false, RoleLabel(file.role),
            file.note.empty() ? file.name : file.name + " - " + file.note);
  } else {
    FileRow(true, RoleLabel(file.role), file.name);
  }
}

void DrawFilesTab(App& app) {
  // Refresh while visible so newly copied files show up without a restart.
  if (app.frames - app.filesFrame >= 30) RefreshFiles(app);
  const FilesCheck& check = app.files;
  SectionLabel("CONTENT FOLDER");
  ImGui::TextWrapped("%s", PathText(check.dir).c_str());
  if (!check.dirOk) {
    FileRow(false, "Folder missing", "create it next to the launcher and add the game packages");
  } else if (check.files.empty()) {
    FileRow(false, "No files", "copy the untouched game, show and episode packages here");
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
  if (check.dirOk && !hasRole(FileRole::Game)) FileRow(false, "Game package", "not found");
  drawRole(FileRole::Launch);
  if (check.dirOk && !hasRole(FileRole::Launch)) FileRow(false, "Launch content", "not found");
  if (check.manifestOk) {
    char count[64] = {};
    SDL_snprintf(count, sizeof(count), "%zu found", check.showCount);
    if (check.showCount > 0)
      FileRow(true, "Show packs", count);
    else
      FileRow(false, "Show packs", "none found");
    drawRole(FileRole::Show);
    SDL_snprintf(count, sizeof(count), "%zu found", check.episodeCount);
    if (check.episodeCount > 0)
      FileRow(true, "Episode packs", count);
    else
      FileRow(false, "Episode packs", "none found");
    drawRole(FileRole::Episode);
  } else {
    char packCount[64] = {};
    SDL_snprintf(packCount, sizeof(packCount), "%zu found", check.showEpisodeCount);
    if (check.showEpisodeCount > 0)
      FileRow(true, "Show / episode packs", packCount);
    else
      FileRow(false, "Show / episode packs", "none found");
    drawRole(FileRole::ShowEpisode);
  }
  drawRole(FileRole::Other);
  drawRole(FileRole::Invalid);
  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("AVATAR DATA");
  if (check.avatarAssetsOk) {
    FileRow(true, "Avatar assets", PathText(check.avatarAssetsPath));
  } else if (check.avatarAssetsSetting.empty()) {
    FileRow(false, "Avatar assets", "set native_avatar_content in onevs100.toml");
  } else {
    FileRow(false, "Avatar assets", "not found: " + check.avatarAssetsSetting);
  }
  ImGui::Dummy(ImVec2(0, 6));
  if (ImGui::Button("Rescan")) RefreshFiles(app);
}

// Returns true when the launcher should close.
bool DrawFrame(App& app, const Fonts& fonts) {
  bool close = false;
  ++app.frames;
  // Keep the Files check fresh while the launcher sits open.
  if (app.frames - app.filesFrame >= 120) RefreshFiles(app);
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
  const float footer = launch_height + line * 4 + style.ItemSpacing.y * 4;
  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Player")) {
      ImGui::BeginChild("##player", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawPlayerTab(app);
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

  ImGui::BeginDisabled(!app.files.ok());
  if (ShowButton("LAUNCH", ImVec2(ImGui::GetContentRegionAvail().x, launch_height))) {
    if (auto saved = app.Save(); !saved) {
      app.Report(saved.error());
    } else if (auto launched = LaunchGame(app.paths); !launched) {
      app.Report(launched.error());
    } else {
      close = true;
    }
  }
  ImGui::EndDisabled();

  if (app.files.ok()) {
    SectionLabel("Gamertag and settings are saved automatically when you press Launch.");
  } else {
    ImGui::TextColored(kError, "Missing files - see the Files tab before launching.");
  }

  LinkText("1vs100.fans", kWebsiteUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("Discord", kDiscordUrl);
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
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) > 0
                          ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())
                          : 1.0f;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("1 vs 100", static_cast<int>(600 * scale), static_cast<int>(600 * scale), 0,
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
  app.dialog_event = SDL_RegisterEvents(1);
  if (app.dialog_event == static_cast<Uint32>(-1)) {
    SDL_Log("No user event for the avatar file dialog; the picker is disabled.");
    app.status = {"Avatar file picker unavailable in this build.", true};
  }
  app.Load();

  bool running = true;
  while (running) {
    SDL_Event event;
    // Idle until input arrives; the UI has no animation.
    if (SDL_WaitEventTimeout(&event, 500)) {
      do {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) running = false;
        if (app.dialog_event != static_cast<Uint32>(-1) && event.type == app.dialog_event)
          HandleAvatarChosen(app, event.user);
      } while (SDL_PollEvent(&event));
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) continue;

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

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
