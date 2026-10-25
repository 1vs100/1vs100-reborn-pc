#pragma once
#include <cstdint>

// Data changes in the loaded title image; no effect outside gameserver mode.
void ApplyGameserverImagePatches(uint8_t* base);
