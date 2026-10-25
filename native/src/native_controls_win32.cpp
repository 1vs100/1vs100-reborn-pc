// Extra input sources for player 1 (see native_controls.h). Plain Win32: no
// runtime headers here; status lines are queued for exit_probe.cpp to log.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "native_controls.h"

namespace {
bool Enabled(const char* name) { const auto p = std::getenv(name); return p && *p == '1'; }

std::mutex logLock;
std::deque<std::string> logLines;
void Log(std::string line) {
  std::lock_guard guard(logLock);
  if (logLines.size() < 64) logLines.push_back(std::move(line));
}

std::atomic<uint16_t> pulse{};
std::atomic<ULONGLONG> pulseEnd{};
LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_COMMAND && HIWORD(w) == BN_CLICKED) {
    pulse = static_cast<uint16_t>(LOWORD(w));
    pulseEnd = GetTickCount64() + 300;  // a 0.3 s press
    return 0;
  }
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;  // keep the game focused
  if (message == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
  return DefWindowProcW(hwnd, message, w, l);
}

class Controls {
 public:
  Controls() : direct_(Enabled("ONEVS100_DUALSENSE_HID")), panel_(Enabled("ONEVS100_MOUSE_PANEL")) {
    if (direct_) reader_ = std::jthread([this](std::stop_token stop) { Read(stop); });
    if (panel_) panelThread_ = std::jthread([](std::stop_token stop) { Panel(stop); });
    if (!direct_ && !panel_) Log("Native controls: off (SDL/XInput only)");
  }
  bool State(NativePadState& out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;  // never act on background input
    bool available = false;
    {
      std::lock_guard lock(mutex_);
      if (direct_ && last_ && GetTickCount64() - last_ < 500) { out = state_; available = true; }
    }
    if (GetTickCount64() < pulseEnd.load()) { out.buttons |= pulse.load(); available = true; }
    return available;
  }

 private:
  static void Panel(std::stop_token stop) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = PanelProc;
    cls.lpszClassName = L"OneVs100MouseControls";
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW (this build is not UNICODE)
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&cls);
    // Topmost and never activated: clicks press buttons while the game keeps focus.
    auto hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls.lpszClassName,
                                L"1 vs 100 - click controls", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                40, 40, 410, 175, nullptr, nullptr, cls.hInstance, nullptr);
    if (!hwnd) { Log("Native controls: mouse panel could not be created"); return; }
    const struct { const wchar_t* label; int id, x, y, width; } buttons[] = {
        {L"Answer 1 (X)", 0x4000, 8, 8, 120},   {L"Answer 2 (A)", 0x1000, 136, 8, 120},
        {L"Answer 3 (B)", 0x2000, 264, 8, 120}, {L"Start", 0x10, 8, 48, 120},
        {L"Select (A)", 0x1000, 136, 48, 120},  {L"Back (B)", 0x2000, 264, 48, 120},
        {L"Up", 1, 8, 88, 86},                  {L"Down", 2, 102, 88, 86},
        {L"Y (Shield/Amp)", 0x8000, 196, 88, 100}, {L"View (Back)", 0x20, 302, 88, 82}};
    for (const auto& b : buttons)
      CreateWindowW(L"BUTTON", b.label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, b.x, b.y, b.width, 32, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), cls.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    Log("Native controls: mouse panel shown (clickable answers and menu buttons)");
    while (!stop.stop_requested()) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
      Sleep(10);
    }
    DestroyWindow(hwnd);
  }

  void Read(std::stop_token stop) {
    Log("Native controls: USB DualSense direct reader on (read-only)");
    GUID guid{};
    HidD_GetHidGuid(&guid);
    bool reportedNone = false;
    while (!stop.stop_requested()) {
      bool found = false;
      auto devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (devices == INVALID_HANDLE_VALUE) { Sleep(250); continue; }
      for (DWORD index = 0; !stop.stop_requested(); ++index) {
        SP_DEVICE_INTERFACE_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &data)) break;
        DWORD length = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &data, nullptr, 0, &length, nullptr);
        if (!length) continue;
        std::vector<uint8_t> detailBytes(length);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBytes.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &data, detail, length, nullptr, nullptr)) continue;
        // Metadata only first; never write reports or change device settings.
        auto metadata = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (metadata == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{sizeof(attributes)};
        bool candidate = HidD_GetAttributes(metadata, &attributes) && attributes.VendorID == 0x054c &&
                         (attributes.ProductID == 0x0ce6 || attributes.ProductID == 0x0df2);
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (candidate && HidD_GetPreparsedData(metadata, &preparsed)) {
          candidate = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 &&
                      caps.Usage == 5 && caps.InputReportByteLength == 64;  // USB report 0x01
          HidD_FreePreparsedData(preparsed);
        } else {
          candidate = false;
        }
        CloseHandle(metadata);
        if (!candidate) continue;
        found = true;
        auto handle = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
          if (openFailLogged_) continue;
          openFailLogged_ = true;
          Log("Native controls: DualSense found but could not be opened, error " + std::to_string(GetLastError()) +
              " (another program may hold it, e.g. Steam Input or DS4Windows)");
          continue;
        }
        openFailLogged_ = false;
        Log("Native controls: USB DualSense opened");
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) { CloseHandle(handle); continue; }
        bool first = true;
        while (!stop.stop_requested()) {
          std::array<uint8_t, 64> bytes{};
          OVERLAPPED ov{};
          ov.hEvent = event;
          ResetEvent(event);
          DWORD count = 0;
          bool ok = ReadFile(handle, bytes.data(), 64, &count, &ov) != 0;
          if (!ok && GetLastError() == ERROR_IO_PENDING) {
            while (!stop.stop_requested() && WaitForSingleObject(event, 100) == WAIT_TIMEOUT) {}
            if (stop.stop_requested()) { CancelIoEx(handle, &ov); WaitForSingleObject(event, INFINITE); break; }
            ok = GetOverlappedResult(handle, &ov, &count, FALSE) != 0;
          }
          if (!ok) { Log("Native controls: DualSense read stopped, error " + std::to_string(GetLastError())); break; }
          if (count != 64 || bytes[0] != 1) continue;
          // USB report 0x01: 1-4 sticks, 5-6 triggers, 8 hat+face, 9 shoulders/menus.
          NativePadState state{};
          const auto a = bytes[8], b = bytes[9];
          if (a & 0x10) state.buttons |= 0x4000;  // Square   -> X
          if (a & 0x20) state.buttons |= 0x1000;  // Cross    -> A
          if (a & 0x40) state.buttons |= 0x2000;  // Circle   -> B
          if (a & 0x80) state.buttons |= 0x8000;  // Triangle -> Y
          const auto hat = a & 15;
          if (hat == 0 || hat == 1 || hat == 7) state.buttons |= 1;
          if (hat == 3 || hat == 4 || hat == 5) state.buttons |= 2;
          if (hat == 5 || hat == 6 || hat == 7) state.buttons |= 4;
          if (hat == 1 || hat == 2 || hat == 3) state.buttons |= 8;
          if (b & 0x01) state.buttons |= 0x100;   // L1
          if (b & 0x02) state.buttons |= 0x200;   // R1
          if (b & 0x10) state.buttons |= 0x20;    // Create  -> Back
          if (b & 0x20) state.buttons |= 0x10;    // Options -> Start
          if (b & 0x40) state.buttons |= 0x40;    // L3
          if (b & 0x80) state.buttons |= 0x80;    // R3
          state.lt = bytes[5];
          state.rt = bytes[6];
          const auto axis = [](uint8_t v, bool flip) {
            int value = (int(v) - 128) * 256;
            if (flip) value = -value - 1;
            return static_cast<int16_t>(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
          };
          state.lx = axis(bytes[1], false);
          state.ly = axis(bytes[2], true);
          state.rx = axis(bytes[3], false);
          state.ry = axis(bytes[4], true);
          {
            std::lock_guard lock(mutex_);
            state_ = state;
            last_ = GetTickCount64();
          }
          if (first) { Log("Native controls: DualSense input received"); first = false; }
        }
        CloseHandle(event);
        CloseHandle(handle);
        std::lock_guard lock(mutex_);
        last_ = 0;
      }
      SetupDiDestroyDeviceInfoList(devices);
      if (!found && !reportedNone) { Log("Native controls: no USB DualSense found yet (still looking)"); reportedNone = true; }
      if (found) reportedNone = false;
      for (int i = 0; i < 20 && !stop.stop_requested(); ++i) Sleep(100);
    }
  }

  bool direct_, panel_;
  bool openFailLogged_ = false;
  std::mutex mutex_;
  NativePadState state_{};
  ULONGLONG last_{};
  std::jthread reader_, panelThread_;
};
}  // namespace

bool GetNativeControls(NativePadState& state) {
  static Controls controls;
  return controls.State(state);
}

bool NativeControlsTakeLog(std::string& line) {
  std::lock_guard guard(logLock);
  if (logLines.empty()) return false;
  line = std::move(logLines.front());
  logLines.pop_front();
  return true;
}
