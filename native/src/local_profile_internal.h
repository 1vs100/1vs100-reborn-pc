#pragma once
#include <filesystem>
#include <string>

// Shared by the profile core and the platform profile dialog.
bool ValidName(const std::string& value);
// Saves the name and optional PNG picture as the current gamertag.
bool SaveLocalProfile(const std::string& name, const std::filesystem::path& picture);
// True when an argument of the process command line contains this text.
bool CommandLineHas(const char* argument);
