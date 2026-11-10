// Path text and TOML file reading shared by the launcher's modules.
#pragma once

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

// SDL hands out UTF-8. Windows paths are UTF-16 and the Windows toolchain
// builds without char8_t, so these convert through SDL_iconv there.
std::filesystem::path PathFromUtf8(const char* text);
std::string PathText(const std::filesystem::path& path);

struct WriteError {
  enum class Step { Write, Replace } step;
  // The reason, for display.
  std::string reason;
};

// Writes a file next to the target, then renames it over the target, so a
// launcher that stops while writing leaves the old contents. The data is not
// synced to disk, so a power loss can still lose it. A symlinked target is
// replaced at the file it points to. An existing file keeps its permission
// bits, which on Windows is only the read-only attribute.
// The bytes are written as given.
std::expected<void, WriteError> WriteFileAtomically(const std::filesystem::path& file, std::string_view contents);

// Reads through fs::path, since toml++ parse_file takes a narrow path that
// breaks on non-ASCII Windows paths. The error is the reason, for display.
std::expected<toml::table, std::string> ParseTomlFile(const std::filesystem::path& file);
