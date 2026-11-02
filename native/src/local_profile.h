#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
void InitializeLocalProfile();
bool EditLocalProfile();
const std::string& LocalGamertag();
// XUID the game sees for the local player: ONEVS100_PROFILE_XUID (hex) if set,
// else derived from this PC, Windows user and ONEVS100_PROFILE_SLOT.
uint64_t LocalXuid();
// Folder holding profiles and storage.
std::filesystem::path PlayerDataFolder();
