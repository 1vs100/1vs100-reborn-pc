#include "local_profile.h"
#include "local_profile_internal.h"
#include "local_avatar.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {
HWND nameEdit, errorLabel;
bool accepted = false;
std::filesystem::path selectedPicture;
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
      if (!SteamGamertag().empty()) std::strncpy(name, SteamGamertag().c_str(), sizeof(name) - 1);
      if (!ValidName(name)) {
        SetWindowTextW(errorLabel, L"Use 1–15 letters, numbers, spaces, _ or -.");
        SetFocus(nameEdit);
        return 0;
      }
      if (!SaveLocalProfile(name, selectedPicture)) {
        SetWindowTextW(errorLabel,L"Could not save the profile. Please try again."); return 0;
      }
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
bool EditLocalProfile() {
  accepted=false; selectedPicture.clear();
  NoteAvatarEditorStamp();
  // Bounded automated startup tests do not create a user profile or open a dialog.
  if (CommandLineHas("--headless=true")) return false;
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
  CreateWindowW(L"STATIC", SteamGamertag().empty() ? L"Choose a gamertag for your local player."
                                              : L"Your gamertag is your Steam name.", WS_CHILD|WS_VISIBLE,
      22, 20, 420, 24, window, nullptr, cls.hInstance, nullptr);
  nameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
      22, 53, 420, 28, window, reinterpret_cast<HMENU>(100), cls.hInstance, nullptr);
  SendMessageW(nameEdit, EM_SETLIMITTEXT, 15, 0);
  SetWindowTextA(nameEdit,LocalGamertag().c_str());
  if (!SteamGamertag().empty()) EnableWindow(nameEdit, FALSE);
  errorLabel = CreateWindowW(L"STATIC", SteamGamertag().empty() ? L"Saved on this computer. No Xbox Live account required."
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
