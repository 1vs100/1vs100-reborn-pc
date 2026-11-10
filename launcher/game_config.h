// The launcher's view of onevs100.toml: the keys it edits itself, every other
// key as a typed entry, and log levels as the game names them.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr const char* kDefaultHost = "1vs100.fans";
// The content folder, relative to the data folder.
inline constexpr const char* kDefaultContent = "content";
// Keys the launcher edits through its own controls; the advanced list leaves them out.
inline constexpr std::array<std::string_view, 8> kManagedKeys = {
    "gameserver_host", "fullscreen", "mnk_mode", "mnk_mouse", "content", "gpu_allow_invalid_fetch_constants",
    "log_level", "lua_print_log"};
// The game builds its native renderer only where a DXC release exists.
#if defined(_WIN32) || (defined(__linux__) && defined(__x86_64__))
inline constexpr bool kHasNativeRenderer = true;
#else
inline constexpr bool kHasNativeRenderer = false;
#endif

// One onevs100.toml key outside the launcher's own controls.
struct ConfigEntry {
  enum class Kind { Flag, Integer, Number, Text };
  std::string key;
  Kind kind = Kind::Text;
  bool flag = false;
  int64_t integer = 0;
  double number = 0;
  std::string text;
};

// Typed the way a TOML value is: true/false, a whole number, a decimal number, else text.
ConfigEntry EntryFromText(std::string key, std::string_view value);

bool ValidConfigKey(std::string_view key);

// A key with its own control, kept out of the list of other keys.
bool ManagedKey(std::string_view key);

// Rejects text the game cannot take as a host: empty, longer than 253
// characters, a URL, or holding a space, tab, /, :, #, " or '.
bool ValidHost(std::string_view host);

// The game's log levels, most detail first, as onevs100.toml names them.
enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical, Off };
inline constexpr std::array<LogLevel, 7> kLogLevels = {LogLevel::Trace, LogLevel::Debug,    LogLevel::Info,
                                                       LogLevel::Warn,  LogLevel::Error,    LogLevel::Critical,
                                                       LogLevel::Off};

const char* LogLevelName(LogLevel level);

// Exactly the values the log_level cvar allows; the game refuses any other and logs at info.
std::optional<LogLevel> ParseLogLevel(std::string_view text);

struct Settings {
  std::string host;
  bool windowed = false;
  std::string content = kDefaultContent;
  bool keyboard = false;
  bool mouse_look = false;
  bool native_renderer = false;
  // The game turns this on itself when the key is missing; off leaves the Press Start scene black on the
  // emulated renderer.
  bool allow_invalid_fetch_constants = true;
  // None: the key is absent, and the game logs at info.
  std::optional<LogLevel> log_level;
  bool lua_print_log = false;
  std::vector<ConfigEntry> others;
  // Keys removed from `others` since loading, so saving drops them from the file.
  std::vector<std::string> removed;
};

struct ConfigError {
  enum class Kind { Unreadable, InvalidHost, WriteFailed } kind;
  std::filesystem::path file;
  // The reason, for display.
  std::string detail;
  // Set for InvalidHost.
  std::optional<std::string> refused_host;
};

// Missing keys take the game's own defaults, except the host, which the
// release presets to the community server.
std::expected<Settings, ConfigError> LoadSettings(const std::filesystem::path& config);

// Keeps keys the launcher does not know and drops those in Settings::removed.
std::expected<void, ConfigError> SaveSettings(const std::filesystem::path& config, const Settings& settings);
