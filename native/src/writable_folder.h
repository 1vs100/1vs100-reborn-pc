#pragma once
#include <filesystem>
// Folder for the files the game writes: config, cache, crash reports, player
// data and the default content folder.
std::filesystem::path WritableFolder();
