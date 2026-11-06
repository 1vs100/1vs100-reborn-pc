#include "native_controls.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <rex/filesystem.h>
#include <rex/platform.h>

// The DualSense HID reader and mouse panel are Windows-only; SDL handles pads here.
// For automated tests, ONEVS100_TEST_PAD_FILE names a file holding an Xbox button
// mask (hex, see native_controls.h); it is consumed and the buttons held 250 ms.
bool GetNativeControls(NativePadState& state) {
#if REX_PLATFORM_WINRT
  static const std::filesystem::path path = rex::filesystem::GetUserFolder() / "test-pad.txt";
#else
  static const char* variable = std::getenv("ONEVS100_TEST_PAD_FILE");
  if (!variable || !*variable) return false;
  static const std::filesystem::path path = variable;
#endif
  using Clock = std::chrono::steady_clock;
  static uint16_t held = 0;
  static Clock::time_point releaseAt;
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    std::ifstream file(path);
    std::string text;
    file >> text;
    file.close();
    std::filesystem::remove(path, error);
    held = static_cast<uint16_t>(std::strtoul(text.c_str(), nullptr, 16));
    releaseAt = Clock::now() + std::chrono::milliseconds(250);
  }
  if (!held) return false;
  if (Clock::now() >= releaseAt) { held = 0; return false; }
  state.buttons = held;
  return true;
}
bool NativeControlsTakeLog(std::string&) { return false; }
