// 1 vs 100 PC launcher: Play / Display / Controls / Show / Online / Debug tabs.
// A plain Win32 front end over outputs\online-tools\Launch-NativeRoomTest.ps1,
// which prepares the game folder and starts the native build. Settings are
// saved in launcher-settings.ini beside this executable.
//
// Build: work\launcher\build_launcher.cmd (clang-cl, no extra SDKs).
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
HINSTANCE instance;
HWND window, tabs, statusText;
HFONT font, titleFont;
std::wstring root, iniPath;

enum Id {
  kTabs = 100,
  kPlayOffline = 200, kHostOnline, kJoinOnline, kTestLocal, kEditProfile, kOpenLogs, kAvatarEditor, kImportAvatarItems, kSendLogs,
  kFullscreen = 300, kWindowSize, kVsync, kRenderScale, kMonitor,
  kKeyboard = 400, kInputBackend, kSensitivity, kDualSenseDirect, kMousePanel,
  kQuestions = 500, kPreShow, kShowTitle, kBots, kShowMode, kLiveTitle, kBotCount,
  kHostId = 600, kMySteamId, kCopyId,
  kSponsor = 700, kSkin, kHostTool, kDebugInput, kFakeLiveHost, kSubtitles, kSponsorDance, kFakeReals, kNoMods,
  kExportContent, kOpenMods,
};
struct Page { std::vector<HWND> controls; };
constexpr int kPageCount = 6;
Page pages[kPageCount];

std::wstring ExeFolder() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring folder(path);
  return folder.substr(0, folder.find_last_of(L"\\/"));
}
// The launcher lives in the workspace root or in work\launcher; find the root
// by looking for the launcher script.
std::wstring FindRoot() {
  std::wstring folder = ExeFolder();
  for (int i = 0; i < 4; ++i) {
    std::wstring script = folder + L"\\outputs\\online-tools\\Launch-NativeRoomTest.ps1";
    if (GetFileAttributesW(script.c_str()) != INVALID_FILE_ATTRIBUTES) return folder;
    auto slash = folder.find_last_of(L"\\/");
    if (slash == std::wstring::npos) break;
    folder = folder.substr(0, slash);
  }
  return ExeFolder();
}

std::wstring ReadSetting(const wchar_t* key, const wchar_t* fallback) {
  wchar_t value[512] = {};
  GetPrivateProfileStringW(L"launcher", key, fallback, value, 512, iniPath.c_str());
  return value;
}
void WriteSetting(const wchar_t* key, const std::wstring& value) {
  WritePrivateProfileStringW(L"launcher", key, value.c_str(), iniPath.c_str());
}

std::wstring Text(int id) {
  wchar_t value[512] = {};
  GetWindowTextW(GetDlgItem(window, id), value, 512);
  return value;
}
bool Checked(int id) { return SendDlgItemMessageW(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
std::wstring ComboText(int id) {
  HWND combo = GetDlgItem(window, id);
  int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (index < 0) return Text(id);
  wchar_t value[128] = {};
  SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
  return value;
}
void SetStatus(const std::wstring& text) { SetWindowTextW(statusText, text.c_str()); UpdateWindow(statusText); }

// Steam's running-user record gives this PC's Steam account (SteamID64).
std::wstring MySteamId() {
  DWORD account = 0, size = sizeof(account);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
                   RRF_RT_REG_DWORD, nullptr, &account, &size) != ERROR_SUCCESS || !account)
    return L"";
  return std::to_wstring(76561197960265728ull + account);
}

HWND Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
  HWND control = CreateWindowExW(0, cls, text, WS_CHILD | style, x, y, w, h, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  pages[page].controls.push_back(control);
  return control;
}
HWND Label(int page, const wchar_t* text, int x, int y, int w, int h = 22) {
  return Add(page, L"STATIC", text, 0, x, y, w, h);
}
HWND Button(int page, const wchar_t* text, int id, int x, int y, int w = 220, int h = 34) {
  return Add(page, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x, y, w, h, id);
}
HWND Check(int page, const wchar_t* text, int id, int x, int y, int w = 300) {
  return Add(page, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w, 24, id);
}
HWND Edit(int page, int id, int x, int y, int w, const std::wstring& value, DWORD extra = 0) {
  HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                              x, y, w, 26, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  pages[page].controls.push_back(edit);
  return edit;
}
HWND Combo(int page, int id, int x, int y, int w, std::initializer_list<const wchar_t*> items, const std::wstring& current) {
  HWND combo = Add(page, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, 200, id);
  int selected = 0, index = 0;
  for (auto* item : items) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    if (current == item) selected = index;
    ++index;
  }
  SendMessageW(combo, CB_SETCURSEL, selected, 0);
  return combo;
}
void SetCheck(int id, bool on) { SendDlgItemMessageW(window, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

void ShowPage(int page) {
  for (int i = 0; i < kPageCount; ++i)
    for (HWND control : pages[i].controls) ShowWindow(control, i == page ? SW_SHOW : SW_HIDE);
}

void SaveSettings() {
  WriteSetting(L"fullscreen", Checked(kFullscreen) ? L"1" : L"0");
  WriteSetting(L"window_size", ComboText(kWindowSize));
  WriteSetting(L"vsync", Checked(kVsync) ? L"1" : L"0");
  WriteSetting(L"render_scale", ComboText(kRenderScale));
  WriteSetting(L"monitor", Text(kMonitor));
  WriteSetting(L"keyboard", Checked(kKeyboard) ? L"1" : L"0");
  WriteSetting(L"input_backend", ComboText(kInputBackend));
  WriteSetting(L"mouse_sensitivity", Text(kSensitivity));
  WriteSetting(L"dualsense_direct", Checked(kDualSenseDirect) ? L"1" : L"0");
  WriteSetting(L"mouse_panel", Checked(kMousePanel) ? L"1" : L"0");
  WriteSetting(L"questions", ComboText(kQuestions));
  WriteSetting(L"preshow_seconds", Text(kPreShow));
  WriteSetting(L"show_title", Text(kShowTitle));
  WriteSetting(L"live_title", Text(kLiveTitle));
  WriteSetting(L"bots", Checked(kBots) ? L"1" : L"0");
  WriteSetting(L"bot_count", Text(kBotCount));
  WriteSetting(L"show_mode", ComboText(kShowMode));
  WriteSetting(L"host_steam_id", Text(kHostId));
  WriteSetting(L"sponsor", ComboText(kSponsor));
  WriteSetting(L"show_skin", ComboText(kSkin));
  WriteSetting(L"host_tool", ComboText(kHostTool));
  WriteSetting(L"debug_input", Checked(kDebugInput) ? L"1" : L"0");
  WriteSetting(L"fake_live_host", Checked(kFakeLiveHost) ? L"1" : L"0");
  WriteSetting(L"subtitles", Checked(kSubtitles) ? L"1" : L"0");
  WriteSetting(L"sponsor_dance", Checked(kSponsorDance) ? L"1" : L"0");
  WriteSetting(L"fake_reals", Checked(kFakeReals) ? L"1" : L"0");
  WriteSetting(L"no_mods", Checked(kNoMods) ? L"1" : L"0");
}

// Keep what reaches PowerShell/Lua to a safe, short character set.
std::wstring SafeTitle(const std::wstring& text) {
  std::wstring out;
  for (wchar_t c : text) {
    if (out.size() >= 24) break;
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
        c == L' ' || c == L'!' || c == L'?' || c == L'.' || c == L',' || c == L'-')
      out += c;
  }
  return out;
}
int Number(const std::wstring& text, int fallback, int low, int high) {
  wchar_t* end = nullptr;
  long value = wcstol(text.c_str(), &end, 10);
  if (text.empty() || (end && *end)) return fallback;
  return static_cast<int>(value < low ? low : value > high ? high : value);
}

std::wstring GameArgs(bool editProfile) {
  std::wstring args;
  args += L"--fullscreen=" + std::wstring(Checked(kFullscreen) ? L"true" : L"false");
  std::wstring size = ComboText(kWindowSize);
  auto x = size.find(L'x');
  if (x != std::wstring::npos) {
    args += L" --window_width=" + size.substr(0, x) + L" --window_height=" + size.substr(x + 1);
  }
  args += L" --vsync=" + std::wstring(Checked(kVsync) ? L"true" : L"false");
  args += L" --resolution_scale=" + std::to_wstring(Number(ComboText(kRenderScale).substr(0, 1), 1, 1, 3));
  int monitor = Number(Text(kMonitor), 0, 0, 8);
  if (monitor > 0) args += L" --monitor=" + std::to_wstring(monitor);
  args += L" --mnk_mode=" + std::wstring(Checked(kKeyboard) ? L"true" : L"false");
  std::wstring backend = ComboText(kInputBackend);
  if (backend == L"xinput" || backend == L"sdl") args += L" --input_backend=" + backend;
  std::wstring sensitivity = Text(kSensitivity);
  bool numeric = !sensitivity.empty() && sensitivity.size() < 8;
  for (wchar_t c : sensitivity) numeric = numeric && ((c >= L'0' && c <= L'9') || c == L'.');
  if (numeric) args += L" --mnk_sensitivity=" + sensitivity;
  if (editProfile) args += L" --edit_profile";
  return args;
}

// Debug tab choices as launch-script arguments (fixed words only).
std::wstring DebugArgs() {
  std::wstring args;
  args += ComboText(kSponsor).find(L"Sprint") == 0 ? L" -Sponsor Sprint" : L" -Sponsor Off";
  std::wstring skin = ComboText(kSkin);
  const wchar_t* skinArg = L"Standard";
  if (skin.find(L"Holiday") == 0) skinArg = L"Holiday";
  else if (skin.find(L"The Big Game") == 0) skinArg = L"TheBigGame";
  else if (skin.find(L"Xbox LIVE") == 0) skinArg = L"XboxLIVE";
  else if (skin.find(L"ColinCorp") == 0) skinArg = L"ColinCorp";
  else if (skin.find(L"Sprint") == 0) skinArg = L"Sprint";
  args += std::wstring(L" -Skin ") + skinArg;
  std::wstring tool = ComboText(kHostTool);
  args += tool.find(L"Host") == 0 ? L" -HostTool Host" : tool.find(L"Producer") == 0 ? L" -HostTool Producer" : L" -HostTool Off";
  if (Checked(kDebugInput)) args += L" -DebugInput";
  if (Checked(kFakeLiveHost)) args += L" -FakeLiveHost";
  if (Checked(kSubtitles)) args += L" -Subtitles";
  if (Checked(kSponsorDance)) args += L" -SponsorDance";
  if (Checked(kFakeReals)) args += L" -FakeReals";
  if (Checked(kNoMods)) args += L" -NoMods";
  return args;
}

// Runs a PowerShell tool script and waits for it. Returns its exit code (or -1).
int RunTool(const std::wstring& scriptAndArgs, DWORD timeout) {
  std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + scriptAndArgs;
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(0);
  if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process))
    return -1;
  CloseHandle(process.hThread);
  DWORD wait = WaitForSingleObject(process.hProcess, timeout);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0 ? static_cast<int>(code) : -1;
}

// The Avatar Editor recompilation (work\avatar-editor, built by
// build_avatar_editor.cmd). It saves avatar_manifest.bin to
// player-data\avatar-editor, which the game reads as the local Avatar.
void OpenAvatarEditor() {
  const std::wstring folder = root + L"\\work\\avatar-editor\\b";
  const std::wstring exe = folder + L"\\avatareditor.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    SetStatus(L"The Avatar Editor is not built yet: run work\\avatar-editor\\build_avatar_editor.cmd.");
    return;
  }
  const std::wstring data = root + L"\\player-data\\avatar-editor";
  CreateDirectoryW((root + L"\\player-data").c_str(), nullptr);
  CreateDirectoryW(data.c_str(), nullptr);
  std::wstring command = L"\"" + exe + L"\" --avatar_data_dir=\"" + data + L"\"";
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(0);
  if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process)) {
    SetStatus(L"Could not start the Avatar Editor.");
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  SetStatus(L"Avatar Editor started. Save your Avatar there, then start a show.");
}

// First line of a small UTF-8 text file a tool script wrote ("" if missing).
std::wstring ReadLine(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return L"";
  char bytes[2048] = {};
  DWORD read = 0;
  ReadFile(file, bytes, sizeof(bytes) - 1, &read, nullptr);
  CloseHandle(file);
  const char* text = bytes;
  if (read >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF) { text += 3; read -= 3; }
  wchar_t wide[2048] = {};
  MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(read), wide, 2047);
  std::wstring line = wide;
  const auto end = line.find_first_of(L"\r\n");
  return end == std::wstring::npos ? line : line.substr(0, end);
}

// Zips the newest game logs onto the Desktop (Collect-Logs.ps1) and shows the
// file in Explorer, ready to send with a bug report.
void SendLogs() {
  const std::wstring script = root + L"\\outputs\\online-tools\\Collect-Logs.ps1";
  if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
    SetStatus(L"The log tool is missing (outputs\\online-tools\\Collect-Logs.ps1).");
    return;
  }
  SetStatus(L"Collecting the newest logs...");
  RunTool(L"\"" + script + L"\"", 300000);
  const std::wstring result = ReadLine(root + L"\\outputs\\diagnostics\\last-logs.txt");
  if (result.rfind(L"ok|", 0) == 0) {
    const auto bar = result.find(L'|', 3);
    const std::wstring zip = result.substr(3, bar == std::wstring::npos ? std::wstring::npos : bar - 3);
    const std::wstring info = bar == std::wstring::npos ? L"" : result.substr(bar + 1);
    const std::wstring name = zip.substr(zip.find_last_of(L"\\/") + 1);
    SetStatus(L"Saved " + name + L" (" + info + L"). Send that file with what happened and roughly when.");
    ShellExecuteW(window, L"open", L"explorer.exe", (L"/select,\"" + zip + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
  } else if (result.rfind(L"error|", 0) == 0) {
    SetStatus(result.substr(6));
  } else {
    SetStatus(L"Could not collect the logs. Use Open logs folder and send the newest native-room-*.log.");
  }
}

// Folder picker for the Avatar item import. Empty when cancelled.
std::wstring PickFolder(const wchar_t* title) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  BROWSEINFOW info = {};
  info.hwndOwner = window;
  info.lpszTitle = title;
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE list = SHBrowseForFolderW(&info);
  if (!list) return L"";
  wchar_t path[MAX_PATH] = {};
  const bool ok = SHGetPathFromIDListW(list, path) != FALSE;
  CoTaskMemFree(list);
  return ok ? path : L"";
}

// Copies the player's own Avatar items (from a copy of their console's Content
// folder, a USB drive, or saved packages) into work\avatar-editor\closet, which
// the Avatar Editor and the game both read.
void ImportAvatarItems() {
  const std::wstring script = root + L"\\work\\avatar-editor\\Import-AvatarCloset.ps1";
  if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
    SetStatus(L"The Avatar item import tool is missing (work\\avatar-editor\\Import-AvatarCloset.ps1).");
    return;
  }
  std::wstring folder = PickFolder(L"Choose a folder with your Xbox 360 Avatar items, for example a copy of "
                                   L"your console's Content folder. Everything inside it is searched.");
  if (folder.empty()) return;
  // "D:\" would end the quoted argument with an escaped quote.
  if (folder.back() == L'\\') folder += L'.';
  SetStatus(L"Importing Avatar items from " + folder + L" ...");
  DeleteFileW((root + L"\\work\\avatar-editor\\closet\\last-import.txt").c_str());
  const int code = RunTool(L"\"" + script + L"\" -Source \"" + folder + L"\"", 1800000);
  std::wstring summary = ReadLine(root + L"\\work\\avatar-editor\\closet\\last-import.txt");
  if (code == 0 && !summary.empty()) SetStatus(summary + L" Start a new show to use them.");
  else SetStatus(summary.empty() ? L"The import did not finish. See outputs\\diagnostics\\import-avatar-closet-*.log."
                                 : summary);
}

bool Launch(const std::wstring& mode, bool editProfile = false) {
  SaveSettings();
  std::wstring script = root + L"\\outputs\\online-tools\\Launch-NativeRoomTest.ps1";
  std::wstring questions = ComboText(kQuestions);
  int questionCount = questions.find(L"Full") == 0 ? 0 : Number(questions.substr(0, questions.find(L' ')), 0, 0, 37);
  std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\"" +
      L" -Mode " + mode +
      L" -Questions " + std::to_wstring(questionCount) +
      L" -PreShowSeconds " + std::to_wstring(Number(Text(kPreShow), 60, 10, 240)) +
      L" -ShowTitle \"" + SafeTitle(Text(kShowTitle)) + L"\"" +
      L" -LiveTitle \"" + SafeTitle(Text(kLiveTitle)) + L"\"" +
      (Checked(kBots) ? L" -BotCount " + std::to_wstring(Number(Text(kBotCount), 99, 1, 10000)) : std::wstring(L" -NoBots")) +
      (ComboText(kShowMode).find(L"Live") == 0 ? L" -ShowMode Live" : L" -ShowMode ExtendedPlay") +
      (Checked(kDualSenseDirect) ? L" -DualSenseDirect" : L"") +
      (Checked(kMousePanel) ? L" -MousePanel" : L"") +
      DebugArgs() +
      L" -GameArgs \"" + GameArgs(editProfile) + L"\"";
  if (mode == L"SteamJoin") {
    std::wstring id = Text(kHostId);
    if (!id.empty()) {
      bool ok = id.size() >= 15 && id.size() <= 20;
      for (wchar_t c : id) ok = ok && c >= L'0' && c <= L'9';
      if (!ok) { SetStatus(L"The host Steam ID on the Online tab should be 17 digits (or leave it empty)."); return false; }
      command += L" -HostSteamId " + id;
    }
  }
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(0);
  if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process)) {
    SetStatus(L"Could not start PowerShell to launch the game.");
    return false;
  }
  CloseHandle(process.hThread);
  // The script returns once the game process has started.
  DWORD wait = WaitForSingleObject(process.hProcess, 120000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  if (wait != WAIT_OBJECT_0 || code != 0) {
    SetStatus(L"The game did not start. See outputs\\diagnostics for the latest log.");
    return false;
  }
  return true;
}

void Build() {
  RECT client;
  GetClientRect(window, &client);
  tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, 64,
                         client.right - 20, client.bottom - 110, window,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabs)), instance, nullptr);
  SendMessageW(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  const wchar_t* names[] = {L"Play", L"Display", L"Controls", L"Show", L"Online", L"Debug"};
  for (int i = 0; i < kPageCount; ++i) {
    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(names[i]);
    TabCtrl_InsertItem(tabs, i, &item);
  }
  const int x = 34, y = 112;
  // Play
  Button(0, L"Play offline", kPlayOffline, x, y);
  Label(0, L"Solo show with offline players (how many: Show tab).", x + 240, y + 8, 400);
  Button(0, L"Host a party (Steam)", kHostOnline, x, y + 50);
  Label(0, L"Up to 4 players. Steam friends can join you.", x + 240, y + 58, 400);
  Button(0, L"Join a friend's party", kJoinOnline, x, y + 100);
  Label(0, L"Finds a Steam friend hosting, or uses the Online tab ID.", x + 240, y + 108, 400);
  Button(0, L"Test: two copies on this PC", kTestLocal, x, y + 150);
  Label(0, L"Starts a host and a second player (Player2).", x + 240, y + 158, 360);
  Button(0, L"Avatar Editor", kAvatarEditor, x, y + 200);
  Label(0, L"Change your Avatar, with your imported closet. The game uses\n"
           L"your saved Avatar the next time Avatars load.", x + 240, y + 200, 420, 36);
  Button(0, L"Edit profile", kEditProfile, x, y + 250);
  Label(0, L"Gamertag and picture for this PC.", x + 240, y + 258, 360);
  Button(0, L"Open logs folder", kOpenLogs, x, y + 300, 220, 30);
  Button(0, L"Import Avatar items...", kImportAvatarItems, x + 240, y + 300, 220, 30);
  Button(0, L"Send logs", kSendLogs, x + 480, y + 300, 170, 30);
  // Display
  Check(1, L"Fullscreen", kFullscreen, x, y);
  Label(1, L"Window size", x, y + 44, 160);
  Combo(1, kWindowSize, x + 170, y + 40, 200, {L"1280x720", L"1600x900", L"1920x1080", L"2560x1440", L"3840x2160"},
        ReadSetting(L"window_size", L"1920x1080"));
  Check(1, L"VSync", kVsync, x, y + 84);
  Label(1, L"Render scale", x, y + 128, 160);
  Combo(1, kRenderScale, x + 170, y + 124, 200, {L"1x (original)", L"2x", L"3x"}, ReadSetting(L"render_scale", L"1x (original)"));
  Label(1, L"Monitor (0 = main)", x, y + 168, 160);
  Edit(1, kMonitor, x + 170, y + 164, 60, ReadSetting(L"monitor", L"0"), ES_NUMBER);
  Label(1, L"Higher render scales sharpen the picture but cost speed.", x, y + 210, 520);
  // Controls
  Check(2, L"Keyboard and mouse (Space = A, Backspace = B, L = X, P = Y, X = Start, Tab = Back)", kKeyboard, x, y, 620);
  Label(2, L"Controller backend", x, y + 44, 160);
  Combo(2, kInputBackend, x + 170, y + 40, 200, {L"sdl", L"xinput"}, ReadSetting(L"input_backend", L"sdl"));
  Label(2, L"Mouse sensitivity", x, y + 88, 160);
  Edit(2, kSensitivity, x + 170, y + 84, 80, ReadSetting(L"mouse_sensitivity", L"1.0"));
  Check(2, L"DualSense (USB) direct backup: also read the pad directly as player 1", kDualSenseDirect, x, y + 128, 620);
  Check(2, L"Mouse button panel: a small window of clickable answer and menu buttons", kMousePanel, x, y + 160, 620);
  Label(2, L"Click inside the game window first. If Steam Input claims the DualSense, turn off\n"
           L"Steam > Settings > Controller > PlayStation controller support, or use the direct backup.", x, y + 196, 620, 40);
  // Show
  Label(3, L"Questions per show", x, y + 4, 170);
  Combo(3, kQuestions, x + 180, y, 200, {L"3 (quick test)", L"5", L"10", L"20", L"Full show (37)"},
        ReadSetting(L"questions", L"Full show (37)"));
  Label(3, L"Countdown before a show (seconds)", x, y + 48, 260);
  Edit(3, kPreShow, x + 270, y + 44, 70, ReadSetting(L"preshow_seconds", L"60"), ES_NUMBER);
  Label(3, L"Extended Play title (marquee, 24 max)", x, y + 92, 300);
  Edit(3, kShowTitle, x, y + 116, 300, ReadSetting(L"show_title", L"Back from the dead!"));
  Label(3, L"Live title (marquee, 24 max)", x + 330, y + 92, 300);
  Edit(3, kLiveTitle, x + 330, y + 116, 300, ReadSetting(L"live_title", L"1 vs 100 LIVE"));
  Check(3, L"Fill the studio with offline players (bots):", kBots, x, y + 160, 330);
  Edit(3, kBotCount, x + 335, y + 158, 80, ReadSetting(L"bot_count", L"99"), ES_NUMBER);
  Label(3, L"1 - 10000 (Extended Play; Live keeps 100 Mob seats)", x + 425, y + 162, 230, 36);
  Label(3, L"Show mode (host)", x, y + 204, 170);
  Combo(3, kShowMode, x + 180, y + 200, 280, {L"Extended Play", L"Live (Primetime) - experimental"},
        ReadSetting(L"show_mode", L"Extended Play"));
  Label(3, L"In an online room only the host's settings apply: everyone plays the host's mode,\n"
           L"question count and schedule.", x, y + 240, 560, 40);
  // Online
  Label(4, L"Your Steam ID (share it with friends who join you)", x, y, 420);
  std::wstring mine = MySteamId();
  Edit(4, kMySteamId, x, y + 24, 240, mine.empty() ? L"Steam is not running" : mine, ES_READONLY);
  Button(4, L"Copy", kCopyId, x + 252, y + 22, 90, 30);
  Label(4, L"Host's Steam ID (optional; empty = find a friend's party)", x, y + 72, 460);
  Edit(4, kHostId, x, y + 96, 240, ReadSetting(L"host_steam_id", L""), ES_NUMBER);
  Label(4, L"Steam must be running on both PCs. Rooms use the Steam development app ID 480.\n"
           L"Internet play has not been tested yet; two copies on one PC have.", x, y + 140, 620, 44);
  // Debug: original development switches and mods (applied to the next launch).
  Label(5, L"Sponsor", x, y + 4, 120);
  Combo(5, kSponsor, x + 130, y, 240, {L"No sponsor (recommended)", L"Sprint (original)"},
        ReadSetting(L"sponsor", L"No sponsor (recommended)"));
  Label(5, L"Show skin", x, y + 44, 120);
  Combo(5, kSkin, x + 130, y + 40, 240, {L"Standard", L"Holiday", L"The Big Game", L"Xbox LIVE", L"ColinCorp (test)", L"Sprint"},
        ReadSetting(L"show_skin", L"Standard"));
  Label(5, L"Host Tool", x, y + 84, 120);
  Combo(5, kHostTool, x + 130, y + 80, 240, {L"Off", L"Host (spectate + stats)", L"Producer (play + stats)"},
        ReadSetting(L"host_tool", L"Off"));
  Check(5, L"Debug menu (Back) && free camera (left stick click)", kDebugInput, x, y + 124, 330);
  Check(5, L"Fake live host video", kFakeLiveHost, x + 340, y + 124, 280);
  Check(5, L"Subtitles on by default", kSubtitles, x, y + 154, 330);
  Check(5, L"Award the sponsor dance", kSponsorDance, x + 340, y + 154, 280);
  Check(5, L"Fake \"real\" Avatars in the studio", kFakeReals, x, y + 184, 330);
  Check(5, L"Play without mods (original text && images)", kNoMods, x + 340, y + 184, 280);
  Button(5, L"Export game images && text", kExportContent, x, y + 226, 240, 32);
  Button(5, L"Open mods folder", kOpenMods, x + 260, y + 226, 180, 32);
  Label(5, L"Export writes every image and string table to mods\\_export. To swap a logo, put a\n"
           L"same-size PNG at mods\\launch\\files\\<same path> (or episode\\files). Text: see\n"
           L"mods\\README.txt. Host Tool: LT/RT change pages, right stick click shows/hides it.", x, y + 270, 620, 62);
  // Values
  SetCheck(kFullscreen, ReadSetting(L"fullscreen", L"0") == L"1");
  SetCheck(kVsync, ReadSetting(L"vsync", L"1") == L"1");
  SetCheck(kKeyboard, ReadSetting(L"keyboard", L"1") == L"1");
  SetCheck(kDualSenseDirect, ReadSetting(L"dualsense_direct", L"1") == L"1");
  SetCheck(kMousePanel, ReadSetting(L"mouse_panel", L"0") == L"1");
  SetCheck(kBots, ReadSetting(L"bots", L"1") == L"1");
  SetCheck(kDebugInput, ReadSetting(L"debug_input", L"0") == L"1");
  SetCheck(kFakeLiveHost, ReadSetting(L"fake_live_host", L"0") == L"1");
  SetCheck(kSubtitles, ReadSetting(L"subtitles", L"0") == L"1");
  SetCheck(kSponsorDance, ReadSetting(L"sponsor_dance", L"0") == L"1");
  SetCheck(kFakeReals, ReadSetting(L"fake_reals", L"0") == L"1");
  SetCheck(kNoMods, ReadSetting(L"no_mods", L"0") == L"1");
  ShowPage(0);
}

void CopyText(const std::wstring& text) {
  if (!OpenClipboard(window)) return;
  EmptyClipboard();
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (memory) {
    memcpy(GlobalLock(memory), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    SetClipboardData(CF_UNICODETEXT, memory);
  }
  CloseClipboard();
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      window = hwnd;
      Build();
      HWND title = CreateWindowExW(0, L"STATIC", L"1 vs 100", WS_CHILD | WS_VISIBLE, 16, 10, 300, 44, hwnd, nullptr, instance, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
      RECT client; GetClientRect(hwnd, &client);
      statusText = CreateWindowExW(0, L"STATIC", L"Ready.", WS_CHILD | WS_VISIBLE, 16, client.bottom - 36, client.right - 32, 24,
                                   hwnd, nullptr, instance, nullptr);
      SendMessageW(statusText, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      return 0;
    }
    case WM_NOTIFY:
      if (reinterpret_cast<NMHDR*>(lparam)->idFrom == kTabs && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE)
        ShowPage(TabCtrl_GetCurSel(tabs));
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kPlayOffline: SetStatus(L"Starting an offline show..."); if (Launch(L"Offline")) SetStatus(L"Game started."); return 0;
        case kHostOnline: SetStatus(L"Starting a party..."); if (Launch(L"SteamHost")) SetStatus(L"Hosting a party. Invite friends from the Steam overlay or Invite Friends."); return 0;
        case kJoinOnline: SetStatus(L"Joining..."); if (Launch(L"SteamJoin")) SetStatus(L"Game started; it looks for the party."); return 0;
        case kTestLocal:
          SetStatus(L"Starting the host copy...");
          if (Launch(L"LocalHost")) {
            Sleep(4000);
            SetStatus(L"Starting the second copy...");
            if (Launch(L"LocalJoin")) SetStatus(L"Two copies started (host and Player2).");
          }
          return 0;
        case kEditProfile: SetStatus(L"Opening the profile editor..."); if (Launch(L"Offline", true)) SetStatus(L"Game started with the profile editor."); return 0;
        case kAvatarEditor: OpenAvatarEditor(); return 0;
        case kImportAvatarItems: ImportAvatarItems(); return 0;
        case kSendLogs: SendLogs(); return 0;
        case kOpenLogs: ShellExecuteW(hwnd, L"open", (root + L"\\outputs\\diagnostics").c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case kExportContent: {
          SetStatus(L"Exporting game images and text (about a minute)...");
          std::wstring script = L"\"" + root + L"\\outputs\\online-tools\\Export-GameContent.ps1\"";
          int code = RunTool(script, 600000);
          if (code == 0) {
            SetStatus(L"Exported to mods\\_export. INDEX.txt lists every image and its size.");
            ShellExecuteW(hwnd, L"open", (root + L"\\mods\\_export").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          } else {
            SetStatus(L"Export failed. See mods\\_export\\export-log.txt.");
          }
          return 0;
        }
        case kOpenMods: {
          std::wstring mods = root + L"\\mods";
          CreateDirectoryW(mods.c_str(), nullptr);
          ShellExecuteW(hwnd, L"open", mods.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        }
        case kCopyId: CopyText(Text(kMySteamId)); SetStatus(L"Steam ID copied."); return 0;
      }
      return 0;
    case WM_CLOSE:
      SaveSettings();
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, PWSTR, int show) {
  instance = hinstance;
  root = FindRoot();
  iniPath = ExeFolder() + L"\\launcher-settings.ini";
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);
  NONCLIENTMETRICSW metrics = {sizeof(metrics)};
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  metrics.lfMessageFont.lfHeight = -15;
  font = CreateFontIndirectW(&metrics.lfMessageFont);
  LOGFONTW title = metrics.lfMessageFont;
  title.lfHeight = -30; title.lfWeight = FW_BOLD;
  titleFont = CreateFontIndirectW(&title);
  WNDCLASSW cls = {};
  cls.lpfnWndProc = Proc;
  cls.hInstance = hinstance;
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  cls.lpszClassName = L"OneVs100Launcher";
  cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassW(&cls);
  RECT size = {0, 0, 720, 520};
  AdjustWindowRect(&size, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"1 vs 100 Launcher - Private Alpha Test", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left, size.bottom - size.top,
                              nullptr, nullptr, hinstance, nullptr);
  ShowWindow(hwnd, show);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  return 0;
}
