#include "native_controls.h"

// The DualSense HID reader and mouse panel are Windows-only; SDL handles pads here.
bool GetNativeControls(NativePadState&) { return false; }
bool NativeControlsTakeLog(std::string&) { return false; }
