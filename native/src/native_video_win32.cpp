#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_video.h"
#include <windows.h>
#include <mfplay.h>
#include <atomic>
#include <string>
#include <thread>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "ole32.lib")

namespace {
std::atomic<bool> playing{false};
std::atomic<bool> stopRequested{false};
constexpr UINT kEndMessage = WM_APP + 1;
constexpr UINT_PTR kFollowTimer = 1, kLimitTimer = 2;
HWND gameWindow = nullptr;

class Callback final : public IMFPMediaPlayerCallback {
 public:
  explicit Callback(HWND window) : window_(window) {}
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    if (id == __uuidof(IUnknown) || id == __uuidof(IMFPMediaPlayerCallback)) {
      *out = static_cast<IMFPMediaPlayerCallback*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = --references_;
    if (!count) delete this;
    return count;
  }
  void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
    if (!event) return;
    if (event->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED || event->eEventType == MFP_EVENT_TYPE_ERROR ||
        FAILED(event->hrEvent))
      PostMessageW(window_, kEndMessage, 0, 0);
  }

 private:
  std::atomic<ULONG> references_{1};
  HWND window_;
};

BOOL CALLBACK FindGameWindow(HWND window, LPARAM bestArea) {
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
  if (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;  // control panel
  RECT client{};
  GetClientRect(window, &client);
  const LONG area = (client.right - client.left) * (client.bottom - client.top);
  auto* best = reinterpret_cast<LONG*>(bestArea);
  if (area > *best) { *best = area; gameWindow = window; }
  return TRUE;
}

RECT GameClientOnScreen() {
  RECT rect{};
  if (!gameWindow || !IsWindow(gameWindow)) return rect;
  GetClientRect(gameWindow, &rect);
  POINT topLeft{rect.left, rect.top}, bottomRight{rect.right, rect.bottom};
  ClientToScreen(gameWindow, &topLeft);
  ClientToScreen(gameWindow, &bottomRight);
  return RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

IMFPMediaPlayer* player = nullptr;

LRESULT CALLBACK VideoProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      if (player) player->UpdateVideo();
      else FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      EndPaint(window, &paint);
      return 0;
    }
    case WM_SIZE:
      if (player) player->UpdateVideo();
      return 0;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_TIMER:
      if (wparam == kLimitTimer || stopRequested.load()) { DestroyWindow(window); return 0; }
      if (wparam == kFollowTimer) {
        const RECT rect = GameClientOnScreen();
        if (rect.right > rect.left)
          SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case kEndMessage:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void PlayOnThisThread(std::filesystem::path file) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  LONG bestArea = 0;
  gameWindow = nullptr;
  EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&bestArea));
  const RECT rect = GameClientOnScreen();
  if (rect.right > rect.left) {
    WNDCLASSW cls{};
    cls.lpfnWndProc = VideoProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"OneVs100IntroVideo";
    RegisterClassW(&cls);  // fails harmlessly when already registered
    // Owned by the game window: stays above it, moves with it, never takes focus.
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP,
                                  rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                  gameWindow, nullptr, cls.hInstance, nullptr);
    if (window) {
      ShowWindow(window, SW_SHOWNOACTIVATE);
      auto* callback = new Callback(window);
      if (SUCCEEDED(MFPCreateMediaPlayer(file.c_str(), TRUE, MFP_OPTION_NONE, callback, window, &player))) {
        SetTimer(window, kFollowTimer, 100, nullptr);
        SetTimer(window, kLimitTimer, 15000, nullptr);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        player->Shutdown();
        player->Release();
        player = nullptr;
      } else {
        DestroyWindow(window);
      }
      callback->Release();
    }
  }
  if (SUCCEEDED(com)) CoUninitialize();
  playing = false;
}
}  // namespace

void NativeVideoPlay(const std::filesystem::path& file) {
  bool expected = false;
  if (!playing.compare_exchange_strong(expected, true)) return;
  stopRequested = false;
  std::thread(PlayOnThisThread, file).detach();
}

void NativeVideoStop() { stopRequested = true; }
