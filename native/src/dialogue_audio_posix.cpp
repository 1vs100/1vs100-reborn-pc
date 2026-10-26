#include "dialogue_audio.h"

// Local WAV dialogue playback is implemented for Windows only.
bool PlayLocalDialogue(const std::string&) { return false; }
void StopLocalDialogue() {}
