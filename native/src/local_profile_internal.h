#pragma once
#include <filesystem>
#include <string>

// Shared by the profile core and the platform profile dialog.
bool ValidName(const std::string& value);
// Gamertag taken from the Steam name; empty when the player chooses one.
const std::string& SteamGamertag();
// Records the Avatar Editor save time as the dialog opens.
void NoteAvatarEditorStamp();
// Saves the name (unless it comes from Steam) and an optional PNG picture,
// and makes the name the current gamertag. False when either was not saved.
bool SaveLocalProfile(const std::string& name, const std::filesystem::path& picture);
// True when an argument of the process command line contains this text.
bool CommandLineHas(const char* argument);
