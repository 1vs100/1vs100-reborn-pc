// Where this copy is installed and which data folder goes with it. Uses SDL,
// so it stays apart from the testable player file code.
#pragma once

#include <expected>
#include <filesystem>

#include "player_files.h"

// Velopack keeps sq.version next to the executables it installs. Same rule
// as WritableFolder in native/src/writable_folder.cpp.
bool IsVelopackInstall(const std::filesystem::path& executable_folder);

// Works before SDL_Init.
std::filesystem::path ExecutableFolder();

// The data folder is next to the executables, or the SDL pref folder when
// the install folder is replaced as a whole or read-only.
std::expected<Paths, Error> MakePaths();
