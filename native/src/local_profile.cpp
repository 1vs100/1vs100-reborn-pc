#include "local_profile.h"
#include "local_profile_internal.h"
#include "local_avatar.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/filesystem.h>
#include <rex/platform.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <iterator>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <unistd.h>
#endif

namespace {
std::string gamertag = "OfflinePlayer";
// Avatar Editor save time when the profile window opened (-1: not yet).
long long editorStamp = -1;
std::string steamName;  // non-empty: the gamertag comes from Steam
bool NameChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}
bool EqualsIgnoreCase(const std::string& a, const char* b) {
  const size_t length = std::strlen(b);
  if (a.size() != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
std::string Environment(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

#ifdef _WIN32
std::filesystem::path SteamInstallFolder() {
  wchar_t steamPath[MAX_PATH] = {};
  DWORD size = sizeof(steamPath);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                   nullptr, steamPath, &size) != ERROR_SUCCESS) return {};
  return steamPath;
}
uint32_t SteamActiveAccountId() {
  DWORD activeUser = 0;
  DWORD size = sizeof(activeUser);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
               RRF_RT_REG_DWORD, nullptr, &activeUser, &size);
  return activeUser;
}
#else
std::filesystem::path SteamInstallFolder() {
  const std::string home = Environment("HOME");
  if (home.empty()) return {};
  std::error_code error;
  for (const char* relative : {".steam/steam", ".local/share/Steam"}) {
    const auto folder = std::filesystem::path(home) / relative;
    if (std::filesystem::is_regular_file(folder / "config" / "loginusers.vdf", error)) return folder;
  }
  return {};
}
uint32_t SteamActiveAccountId() { return 0; }
#endif

#ifdef _WIN32
// Windows best-fit mapping to US-ASCII (code page 20127).
std::string AsciiFold(const std::string& utf8) {
  const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  if (wideLength <= 0) return {};
  std::wstring wide(wideLength, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), wideLength);
  const int asciiLength = WideCharToMultiByte(20127, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
  std::string ascii(asciiLength > 0 ? asciiLength : 0, '\0');
  if (asciiLength > 0) WideCharToMultiByte(20127, 0, wide.data(), wideLength, ascii.data(), asciiLength, nullptr, nullptr);
  return ascii;
}
#else
// Latin-1 letters folded to their base letter; other non-ASCII code points dropped.
std::string AsciiFold(const std::string& utf8) {
  static const char kLatin1[] =
      "AAAAAAACEEEEIIII" "DNOOOOO*OUUUUYTs"
      "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
  std::string ascii;
  for (size_t i = 0; i < utf8.size();) {
    const unsigned char lead = utf8[i];
    uint32_t code = 0;
    size_t length = 1;
    if (lead < 0x80) code = lead;
    else if ((lead & 0xE0) == 0xC0) { code = lead & 0x1F; length = 2; }
    else if ((lead & 0xF0) == 0xE0) { code = lead & 0x0F; length = 3; }
    else if ((lead & 0xF8) == 0xF0) { code = lead & 0x07; length = 4; }
    else { ++i; continue; }
    if (i + length > utf8.size()) break;
    for (size_t k = 1; k < length; ++k) code = (code << 6) | (utf8[i + k] & 0x3F);
    i += length;
    if (code < 0x80) ascii += char(code);
    else if (code >= 0xC0 && code <= 0xFF && kLatin1[code - 0xC0] != '*' && kLatin1[code - 0xC0] != '/')
      ascii += kLatin1[code - 0xC0];
  }
  return ascii;
}
#endif

// Steam display name -> gamertag the game can show: accents folded to plain
// letters (best-fit ASCII), other characters dropped, spaces collapsed,
// at most 15 characters. Empty when nothing usable is left.
std::string GamertagFromSteamName(const std::string& utf8) {
  const std::string ascii = AsciiFold(utf8);
  std::string name;
  for (unsigned char c : ascii) {
    if (!NameChar(c)) continue;
    if (c == ' ' && (name.empty() || name.back() == ' ')) continue;
    name += char(c);
  }
  if (name.size() > 15) name.resize(15);
  while (!name.empty() && name.back() == ' ') name.pop_back();
  return ValidName(name) ? name : std::string();
}

// The signed-in Steam user's display name, read from Steam's own files so it
// works offline and without Steam running: config\loginusers.vdf, the account
// Steam reports as active (ActiveProcess\ActiveUser), else the most recent.
// ONEVS100_USE_STEAM_NAME=0 turns this off.
std::string ReadSteamDisplayName() {
  const char* setting = std::getenv("ONEVS100_USE_STEAM_NAME");
  if (setting && std::strcmp(setting, "0") == 0) return {};
  const std::filesystem::path steamPath = SteamInstallFolder();
  if (steamPath.empty()) return {};
  const uint32_t activeUser = SteamActiveAccountId();
  std::ifstream file(steamPath / "config" / "loginusers.vdf", std::ios::binary);
  if (!file) return {};
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  // Tokens: quoted strings and braces (VDF text format).
  std::vector<std::string> tokens;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '{' || c == '}') { tokens.emplace_back(1, c); continue; }
    if (c != '"') continue;
    std::string value;
    for (++i; i < text.size() && text[i] != '"'; ++i) {
      if (text[i] == '\\' && i + 1 < text.size()) ++i;
      value += text[i];
    }
    tokens.push_back(value);
  }
  struct SteamUser { std::string id, persona; bool mostRecent = false; };
  std::vector<SteamUser> users;
  int depth = 0;
  std::string pendingKey;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string& token = tokens[i];
    if (token == "{") { if (depth == 1) users.push_back({pendingKey}); ++depth; continue; }
    if (token == "}") { --depth; continue; }
    if (depth == 1) { pendingKey = token; continue; }
    if (depth == 2 && !users.empty() && i + 1 < tokens.size() && tokens[i + 1] != "{" && tokens[i + 1] != "}") {
      const std::string& value = tokens[++i];
      if (EqualsIgnoreCase(token, "PersonaName")) users.back().persona = value;
      else if (EqualsIgnoreCase(token, "MostRecent")) users.back().mostRecent = value == "1";
    }
  }
  const SteamUser* chosen = nullptr;
  if (activeUser) {
    const std::string activeId = std::to_string(76561197960265728ull + activeUser);
    for (const auto& user : users) if (user.id == activeId) chosen = &user;
  }
  if (!chosen) for (const auto& user : users) if (user.mostRecent) chosen = &user;
  if (!chosen && users.size() == 1) chosen = &users.front();
  return chosen ? chosen->persona : std::string();
}
}

bool ValidName(const std::string& value) {
  if (value.empty() || value.size() > 15 || value.front() == ' ' || value.back() == ' ') return false;
  for (unsigned char c : value) if (!NameChar(c)) return false;
  return true;
}
const std::string& SteamGamertag() { return steamName; }
void NoteAvatarEditorStamp() { editorStamp = local_avatar::EditorSaveStamp(); }
bool SaveLocalProfile(const std::string& name, const std::filesystem::path& picture) {
  if (!ValidName(name)) return false;
  std::error_code error;
  const auto profiles = PlayerDataFolder() / "profiles";
  std::filesystem::create_directories(profiles, error);
  if (error) return false;
  if (!picture.empty()) {
    const auto target = rex::filesystem::GetExecutableFolder() / "assets" / "profile-pictures" / "local.png";
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    if (!std::filesystem::exists(target, error) || !std::filesystem::equivalent(picture, target, error))
      std::filesystem::copy_file(picture, target, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return false;
  }
  // A Steam name is not saved here: the typed gamertag stays as the
  // fallback for when Steam is not installed or signed in.
  if (steamName.empty()) {
    std::ofstream saved(profiles / "local-name.txt", std::ios::trunc);
    saved << name << '\n';
    saved.flush();
    if (!saved) return false;
  }
  gamertag = name;
  return true;
}
bool CommandLineHas(const char* argument) {
#ifdef _WIN32
  const std::wstring wide(argument, argument + std::strlen(argument));
  return wcsstr(GetCommandLineW(), wide.c_str()) != nullptr;
#else
  std::ifstream file("/proc/self/cmdline", std::ios::binary);
  for (std::string arg; std::getline(file, arg, '\0');)
    if (arg.find(argument) != std::string::npos) return true;
  return false;
#endif
}

const std::string& LocalGamertag() { return gamertag; }
uint64_t LocalXuid() {
  static const uint64_t overrideXuid = [] {
    const char* text = std::getenv("ONEVS100_PROFILE_XUID");
    if (!text || !*text) return uint64_t(0);
    char* end = nullptr;
    const uint64_t value = std::strtoull(text, &end, 16);
    // 0x00C0000000000000 bits make the title refuse the profile.
    if (!value || (end && *end) || (value & 0x00C0000000000000ull)) return uint64_t(0);
    return value;
  }();
  if (overrideXuid) return overrideXuid;
  // Every copy of the game needs its own identity, stable across launches and
  // modes (offline, hosting, joining), so saved stats follow the player. Derive
  // it from this PC and this OS user; ONEVS100_PROFILE_SLOT tells apart a
  // second test copy on the same PC.
  static const uint64_t derivedXuid = [] {
    uint32_t hash = 2166136261u;  // FNV-1a
#ifdef _WIN32
    std::wstring seed;
    for (const wchar_t* name : {L"COMPUTERNAME", L"USERNAME", L"ONEVS100_PROFILE_SLOT"}) {
      wchar_t value[256] = {};
      GetEnvironmentVariableW(name, value, 256);
      seed += L"|"; seed += value;
    }
    for (wchar_t c : seed) { hash ^= static_cast<uint32_t>(c); hash *= 16777619u; }
#else
    char host[256] = {};
    gethostname(host, sizeof(host) - 1);
    const std::string seed = std::string("|") + host + "|" + Environment("USER") + "|" + Environment("ONEVS100_PROFILE_SLOT");
    for (unsigned char c : seed) { hash ^= c; hash *= 16777619u; }
#endif
    return 0x0009000000000000ull | hash;
  }();
  return derivedXuid;
}
std::filesystem::path PlayerDataFolder() {
  // ONEVS100_PLAYER_DATA lets every mode on this PC share one profile, Avatar
  // and saved stats (the launcher sets it); otherwise userdata beside the game.
#ifdef _WIN32
  wchar_t value[1024] = {};
  if (GetEnvironmentVariableW(L"ONEVS100_PLAYER_DATA", value, 1024) && value[0]) return std::filesystem::path(value);
#else
  if (const std::string value = Environment("ONEVS100_PLAYER_DATA"); !value.empty()) return value;
#endif
#if REX_PLATFORM_WINRT
  // The package install folder is read-only.
  return rex::filesystem::GetUserFolder() / "userdata";
#else
  return rex::filesystem::GetExecutableFolder() / "userdata";
#endif
}
// Also true when the Avatar Editor saved since the last check (it runs as its
// own program, so its save is only seen through its file).
bool TakeLocalAvatarChanged() {
  const long long stamp = local_avatar::EditorSaveStamp();
  const bool changed = editorStamp >= 0 && stamp != editorStamp;
  editorStamp = stamp;
  return changed;
}
void InitializeLocalProfile() {
  std::ifstream saved(PlayerDataFolder() / "profiles" / "local-name.txt");
  std::string name;
  const bool requested = CommandLineHas("--edit_profile");
  const std::string fromSteam = ReadSteamDisplayName();
  steamName = GamertagFromSteamName(fromSteam);
  if (!steamName.empty()) {
    // No name prompt; the profile editor still opens on request for the Avatar.
    gamertag = steamName;
    REXKRNL_WARN("Profile: gamertag from Steam name \"{}\" -> \"{}\"", fromSteam, steamName);
    if (requested) EditLocalProfile();
    return;
  }
  if (!fromSteam.empty()) REXKRNL_WARN("Profile: Steam name \"{}\" has no usable characters; using the saved gamertag", fromSteam);
  if (saved && std::getline(saved,name) && ValidName(name)) {gamertag=name; if (!requested) return;}
  EditLocalProfile();
}
