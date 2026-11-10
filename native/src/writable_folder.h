#pragma once
#include <filesystem>
// Folder for the files the game writes: config, cache, crash reports, player
// data and the default content folder.
std::filesystem::path WritableFolder();
// Folder for the files shipped with the game, such as the default config.
std::filesystem::path ShippedFolder();
