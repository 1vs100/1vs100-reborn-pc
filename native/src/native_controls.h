#pragma once
#include <cstdint>
#include <string>
// Extra input sources for player 1, merged into the game's own pad state
// (exit_probe.cpp). Ordinary SDL/XInput input stays active alongside them.
//   ONEVS100_DUALSENSE_HID=1  read a USB DualSense directly (read-only HID)
//   ONEVS100_MOUSE_PANEL=1    small always-on-top window of clickable buttons
// Xbox button bits: 0x1 up, 0x2 down, 0x4 left, 0x8 right, 0x10 Start,
// 0x20 Back, 0x40 LS, 0x80 RS, 0x100 LB, 0x200 RB, 0x1000 A, 0x2000 B,
// 0x4000 X, 0x8000 Y.
struct NativePadState {
  uint16_t buttons{};
  uint8_t lt{}, rt{};
  int16_t lx{}, ly{}, rx{}, ry{};
};
// True when an extra source has input for player 1 right now (only while this
// game has the foreground window).
bool GetNativeControls(NativePadState& state);
// Status lines for the game log (the caller logs them).
bool NativeControlsTakeLog(std::string& line);
