#pragma once
#include <filesystem>
// Plays a video file in a borderless window over the game window, with its
// own sound, until it ends, NativeVideoStop() is called, or 15 s pass.
// Used for the intro bumper, whose original .wmv is not available.
void NativeVideoPlay(const std::filesystem::path& file);
void NativeVideoStop();
