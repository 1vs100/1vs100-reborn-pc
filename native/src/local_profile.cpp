#include "local_profile.h"
#include "local_avatar.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <windows.h>
#include <commdlg.h>
#include <rex/filesystem.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#pragma comment(lib, "advapi32.lib")

namespace {
std::string gamertag = "OfflinePlayer";
HWND nameEdit, errorLabel;
bool accepted = false;
// Avatar Editor save time when the profile window opened (-1: not yet).
long long editorStamp = -1;
std::filesystem::path selectedPicture;
std::string steamName;  // non-empty: the gamertag comes from Steam
bool NameChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}
bool ValidName(const std::string& value) {
  if (value.empty() || value.size() > 15 || value.front() == ' ' || value.back() == ' ') return false;
  for (unsigned char c : value) if (!NameChar(c)) return false;
  return true;
}

// Steam display name -> gamertag the game can show: accents folded to plain
// letters (best-fit ASCII), other characters dropped, spaces collapsed,
// at most 15 characters. Empty when nothing usable is left.
std::string GamertagFromSteamName(const std::string& utf8) {
  const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  if (wideLength <= 0) return {};
  std::wstring wide(wideLength, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), wideLength);
  const int asciiLength = WideCharToMultiByte(20127, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
  std::string ascii(asciiLength > 0 ? asciiLength : 0, '\0');
  if (asciiLength > 0) WideCharToMultiByte(20127, 0, wide.data(), wideLength, ascii.data(), asciiLength, nullptr, nullptr);
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
  wchar_t steamPath[MAX_PATH] = {};
  DWORD size = sizeof(steamPath);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                   nullptr, steamPath, &size) != ERROR_SUCCESS) return {};
  DWORD activeUser = 0;
  size = sizeof(activeUser);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
               RRF_RT_REG_DWORD, nullptr, &activeUser, &size);
  std::ifstream file(std::filesystem::path(steamPath) / "config" / "loginusers.vdf", std::ios::binary);
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
      if (_stricmp(token.c_str(), "PersonaName") == 0) users.back().persona = value;
      else if (_stricmp(token.c_str(), "MostRecent") == 0) users.back().mostRecent = value == "1";
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
LRESULT CALLBACK ProfileProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_COMMAND) {
    if (LOWORD(wparam) == 101) {
      wchar_t filename[MAX_PATH] = {};
      OPENFILENAMEW picker = {};
      picker.lStructSize = sizeof(picker); picker.hwndOwner = window;
      picker.lpstrFilter = L"PNG profile pictures\0*.png\0\0";
      picker.lpstrFile = filename; picker.nMaxFile = MAX_PATH;
      picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
      if (GetOpenFileNameW(&picker)) {
        std::ifstream image(std::filesystem::path(filename), std::ios::binary);
        unsigned char signature[8] = {};
        image.read(reinterpret_cast<char*>(signature), 8);
        const unsigned char png[8] = {137,80,78,71,13,10,26,10};
        if (!image || std::memcmp(signature,png,8)) {
          SetWindowTextW(errorLabel,L"Choose a valid PNG image."); return 0;
        }
        selectedPicture = filename;
        SetWindowTextW(errorLabel,L"Picture selected. Click Save profile to apply.");
      }
      return 0;
    }
    if (LOWORD(wparam) == 102) {
      if (OpenAvatarEditor(window))
        SetWindowTextW(errorLabel, L"Avatar Editor opened. Save your Avatar there, then click Save profile here.");
      return 0;
    }
    if (LOWORD(wparam) == IDOK) {
      char name[64] = {};
      GetWindowTextA(nameEdit, name, sizeof(name));
      if (!steamName.empty()) std::strncpy(name, steamName.c_str(), sizeof(name) - 1);
      if (!ValidName(name)) {
        SetWindowTextW(errorLabel, L"Use 1–15 letters, numbers, spaces, _ or -.");
        SetFocus(nameEdit);
        return 0;
      }
      const auto folder=rex::filesystem::GetExecutableFolder();
      try {
        const auto profiles=PlayerDataFolder() / "profiles";
        std::filesystem::create_directories(profiles);
        if (!selectedPicture.empty()) {
          const auto picture=folder / "assets" / "profile-pictures" / "local.png";
          std::filesystem::create_directories(picture.parent_path());
          if (!std::filesystem::exists(picture) || !std::filesystem::equivalent(selectedPicture,picture))
            std::filesystem::copy_file(selectedPicture,picture,std::filesystem::copy_options::overwrite_existing);
        }
        // A Steam name is not saved here: the typed gamertag stays as the
        // fallback for when Steam is not installed or signed in.
        if (steamName.empty()) {
          std::ofstream saved(profiles / "local-name.txt",std::ios::trunc);
          saved << name << '\n'; saved.flush();
          if (!saved) throw std::runtime_error("profile save failed");
        }
      } catch (const std::exception&) {
        SetWindowTextW(errorLabel,L"Could not save the profile. Please try again."); return 0;
      }
      gamertag = name;
      accepted = true;
      DestroyWindow(window);
      return 0;
    }
    if (LOWORD(wparam) == IDCANCEL) { DestroyWindow(window); return 0; }
  }
  if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
  return DefWindowProcW(window, message, wparam, lparam);
}
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
  // it from this PC and this Windows user; ONEVS100_PROFILE_SLOT tells apart a
  // second test copy on the same PC.
  static const uint64_t derivedXuid = [] {
    std::wstring seed;
    for (const wchar_t* name : {L"COMPUTERNAME", L"USERNAME", L"ONEVS100_PROFILE_SLOT"}) {
      wchar_t value[256] = {};
      GetEnvironmentVariableW(name, value, 256);
      seed += L"|"; seed += value;
    }
    uint32_t hash = 2166136261u;  // FNV-1a
    for (wchar_t c : seed) { hash ^= static_cast<uint32_t>(c); hash *= 16777619u; }
    return 0x0009000000000000ull | hash;
  }();
  return derivedXuid;
}
std::filesystem::path PlayerDataFolder() {
  // ONEVS100_PLAYER_DATA lets every mode on this PC share one profile, Avatar
  // and saved stats (the launcher sets it); otherwise userdata beside the game.
  wchar_t value[1024] = {};
  if (GetEnvironmentVariableW(L"ONEVS100_PLAYER_DATA", value, 1024) && value[0]) return std::filesystem::path(value);
  return rex::filesystem::GetExecutableFolder() / "userdata";
}
// Also true when the Avatar Editor saved since the last check (it runs as its
// own program, so its save is only seen through its file).
bool TakeLocalAvatarChanged() {
  const long long stamp = local_avatar::EditorSaveStamp();
  const bool changed = editorStamp >= 0 && stamp != editorStamp;
  editorStamp = stamp;
  return changed;
}
bool EditLocalProfile() {
  accepted=false; selectedPicture.clear();
  editorStamp = local_avatar::EditorSaveStamp();
  // Bounded automated startup tests do not create a user profile or open a dialog.
  if (wcsstr(GetCommandLineW(), L"--headless=true")) return false;
  WNDCLASSW cls = {};
  cls.lpfnWndProc = ProfileProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  cls.lpszClassName = L"OneVs100LocalProfile";
  RegisterClassW(&cls);
  HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"Edit offline profile",
      WS_CAPTION | WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN)-480)/2,
      (GetSystemMetrics(SM_CYSCREEN)-290)/2, 480, 290, nullptr, nullptr, cls.hInstance, nullptr);
  if (!window) return false;
  CreateWindowW(L"STATIC", steamName.empty() ? L"Choose a gamertag for your local player."
                                              : L"Your gamertag is your Steam name.", WS_CHILD|WS_VISIBLE,
      22, 20, 420, 24, window, nullptr, cls.hInstance, nullptr);
  nameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
      22, 53, 420, 28, window, reinterpret_cast<HMENU>(100), cls.hInstance, nullptr);
  SendMessageW(nameEdit, EM_SETLIMITTEXT, 15, 0);
  SetWindowTextA(nameEdit,gamertag.c_str());
  if (!steamName.empty()) EnableWindow(nameEdit, FALSE);
  errorLabel = CreateWindowW(L"STATIC", steamName.empty() ? L"Saved on this computer. No Xbox Live account required."
                                                          : L"To change it, change your Steam name. Avatar and picture are saved on this computer.",
      WS_CHILD|WS_VISIBLE,
      22, 91, 420, 38, window, nullptr, cls.hInstance, nullptr);
  CreateWindowW(L"BUTTON", L"Choose picture...", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
      22,135,160,30,window,reinterpret_cast<HMENU>(101),cls.hInstance,nullptr);
  CreateWindowW(L"BUTTON", L"Avatar Editor...", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
      192,135,160,30,window,reinterpret_cast<HMENU>(102),cls.hInstance,nullptr);
  CreateWindowW(L"BUTTON", L"Save profile", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
      182, 190, 130, 30, window, reinterpret_cast<HMENU>(IDOK), cls.hInstance, nullptr);
  CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
      326, 190, 116, 30, window, reinterpret_cast<HMENU>(IDCANCEL), cls.hInstance, nullptr);
  ShowWindow(window, SW_SHOW);
  // The first ShowWindow honors the launcher's hidden startup hint. The
  // explicitly requested profile dialog must become visible afterward.
  ShowWindow(window, SW_SHOWNORMAL);
  SetForegroundWindow(window);
  SetFocus(nameEdit);
  MSG message;
  while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0)
    if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  return accepted;
}
void InitializeLocalProfile() {
  std::ifstream saved(PlayerDataFolder() / "profiles" / "local-name.txt");
  std::string name;
  const bool requested = wcsstr(GetCommandLineW(), L"--edit_profile") != nullptr;
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
