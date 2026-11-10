// Path text and TOML file reading shared by the launcher's modules.
#pragma once

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <string>

// SDL hands out UTF-8. Windows paths are UTF-16 and the Windows toolchain
// builds without char8_t, so these convert through SDL_iconv there.
std::filesystem::path PathFromUtf8(const char* text);
std::string PathText(const std::filesystem::path& path);

// The error is the reason, for display.
std::expected<toml::table, std::string> ParseTomlFile(const std::filesystem::path& file);
