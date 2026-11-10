#include "game_config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <system_error>
#include <utility>

#include <toml++/toml.hpp>

#include "files.h"

namespace fs = std::filesystem;

namespace {

std::optional<ConfigEntry> EntryFromNode(std::string key, const toml::node& node) {
  ConfigEntry entry;
  entry.key = std::move(key);
  if (auto flag = node.value_exact<bool>()) {
    entry.kind = ConfigEntry::Kind::Flag;
    entry.flag = *flag;
  } else if (auto integer = node.value_exact<int64_t>()) {
    entry.kind = ConfigEntry::Kind::Integer;
    entry.integer = *integer;
  } else if (auto number = node.value_exact<double>()) {
    entry.kind = ConfigEntry::Kind::Number;
    entry.number = *number;
  } else if (auto text = node.value_exact<std::string>()) {
    entry.kind = ConfigEntry::Kind::Text;
    entry.text = *text;
  } else {
    return std::nullopt;
  }
  return entry;
}

struct Decimal {
  double value;
  // Too large or too small for a double: strtod gave infinity or 0. It also
  // reports ERANGE for subnormal results, which a double holds.
  bool out_of_range;
};

// strtod, not from_chars: older libc++ has no floating-point from_chars. Only
// plain decimals count, so text such as 0x10, inf or nan stays text.
std::optional<Decimal> ParseDecimal(std::string_view value) {
  if (value.empty() || value.find_first_not_of("0123456789.-eE") != std::string_view::npos) return std::nullopt;
  const std::string text(value);
  char* end = nullptr;
  errno = 0;
  const double number = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size()) return std::nullopt;
  return Decimal{number, errno == ERANGE && (std::isinf(number) || number == 0.0)};
}

enum class ValueType { Flag, Text, LogLevel };

// Keys the launcher edits through its own controls, and the value each holds.
struct ManagedValue {
  std::string_view key;
  ValueType type;
};

constexpr auto kManagedValues = std::to_array<ManagedValue>({
    {"gameserver_host", ValueType::Text},
    {"fullscreen", ValueType::Flag},
    {"content", ValueType::Text},
    {"mnk_mode", ValueType::Flag},
    {"mnk_mouse", ValueType::Flag},
    {"gpu_allow_invalid_fetch_constants", ValueType::Flag},
    {"log_level", ValueType::LogLevel},
    {"lua_print_log", ValueType::Flag},
    {"native_renderer", ValueType::Flag},
});

// The game reads every config value as text (ApplyTomlTable in
// rexglue-sdk/src/core/cvar.cpp). Nothing for arrays and tables, which it skips.
std::optional<std::string> GameText(const toml::node& node) {
  if (node.is_boolean()) return node.as_boolean()->get() ? "true" : "false";
  if (node.is_integer()) return std::to_string(node.as_integer()->get());
  if (node.is_floating_point()) return std::to_string(node.as_floating_point()->get());
  if (node.is_string()) return node.as_string()->get();
  return std::nullopt;
}

// What a bool cvar makes of that text (from_string<bool> in
// rexglue-sdk/include/rex/string/numeric.h): anything else is false.
bool GameFlag(std::string_view text) { return text == "true" || text == "1" || text == "yes"; }

std::optional<std::string> ManagedText(const toml::table& table, std::string_view key) {
  const toml::node* node = table.get(key);
  return node ? GameText(*node) : std::nullopt;
}

ConfigError BadValue(const fs::path& config, std::string detail) {
  return ConfigError{ConfigError::Kind::BadValue, config, std::move(detail), std::nullopt};
}

// The game joins table and key names with _, so [mnk] mode = true sets
// mnk_mode. The launcher writes the top-level key, which would hide it.
std::optional<ConfigError> CheckNestedManagedKeys(const fs::path& config, const toml::table& table,
                                                  const std::string& prefix) {
  for (const auto& [key, node] : table) {
    const std::string name = prefix + "_" + std::string(key.str());
    if (const toml::table* inner = node.as_table()) {
      if (auto bad = CheckNestedManagedKeys(config, *inner, name)) return bad;
    } else if (ManagedKey(name)) {
      return BadValue(config, name + " is set inside a [table]. Write it as " + name + " before the first [table] line");
    }
  }
  return std::nullopt;
}

// A value the game would not read. The launcher would show its default for
// it and write that back.
std::optional<ConfigError> CheckManagedValues(const fs::path& config, const toml::table& table) {
  for (const ManagedValue& managed : kManagedValues) {
    if (!ManagedKey(managed.key)) continue;
    const toml::node* node = table.get(managed.key);
    if (!node) continue;
    const auto text = GameText(*node);
    if (!text) return BadValue(config, std::string(managed.key) + " must be text, a number, true or false");
    // The log_level cvar refuses any other text, case included.
    if (managed.type == ValueType::LogLevel && !ParseLogLevel(*text)) {
      return BadValue(config, std::string(managed.key) +
                                  " must be one of \"trace\", \"debug\", \"info\", \"warn\", \"error\", "
                                  "\"critical\" or \"off\"");
    }
  }
  for (const auto& [key, node] : table) {
    if (const toml::table* inner = node.as_table()) {
      if (auto bad = CheckNestedManagedKeys(config, *inner, std::string(key.str()))) return bad;
    }
  }
  return std::nullopt;
}

}  // namespace

bool ManagedKey(std::string_view key) {
  // Without the checkbox the key stays in the advanced list, for builds that enable the renderer.
  if (key == "native_renderer") return kHasNativeRenderer;
  return std::any_of(kManagedValues.begin(), kManagedValues.end(),
                     [key](const ManagedValue& managed) { return managed.key == key; });
}

std::string Describe(EntryError error) {
  switch (error) {
    case EntryError::WholeNumberOutOfRange:
      return "Whole numbers must be from -9223372036854775808 to 9223372036854775807.";
    case EntryError::DecimalOutOfRange: return "That decimal number is too large or too small to store.";
  }
  std::unreachable();
}

std::expected<ConfigEntry, EntryError> EntryFromText(std::string key, std::string_view value) {
  ConfigEntry entry;
  entry.key = std::move(key);
  int64_t integer = 0;
  const char* end = value.data() + value.size();
  const auto parsed = std::from_chars(value.data(), end, integer);
  if (value == "true" || value == "false") {
    entry.kind = ConfigEntry::Kind::Flag;
    entry.flag = value == "true";
  } else if (!value.empty() && parsed.ptr == end && parsed.ec == std::errc()) {
    entry.kind = ConfigEntry::Kind::Integer;
    entry.integer = integer;
  } else if (!value.empty() && parsed.ptr == end && parsed.ec == std::errc::result_out_of_range) {
    // As a decimal number it would be saved rounded.
    return std::unexpected(EntryError::WholeNumberOutOfRange);
  } else if (const auto decimal = ParseDecimal(value)) {
    if (decimal->out_of_range) return std::unexpected(EntryError::DecimalOutOfRange);
    entry.kind = ConfigEntry::Kind::Number;
    entry.number = decimal->value;
  } else {
    entry.kind = ConfigEntry::Kind::Text;
    entry.text = std::string(value);
  }
  return entry;
}

bool ValidConfigKey(std::string_view key) {
  if (key.empty()) return false;
  for (unsigned char c : key)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

bool ValidHost(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  if (host.find("://") != std::string_view::npos) return false;
  for (char c : host) {
    if (c == ' ' || c == '\t' || c == '/' || c == ':' || c == '#' || c == '"' || c == '\'') return false;
  }
  return true;
}

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    case LogLevel::Critical: return "critical";
    case LogLevel::Off: return "off";
  }
  std::unreachable();
}

std::optional<LogLevel> ParseLogLevel(std::string_view text) {
  for (const LogLevel level : kLogLevels) {
    if (text == LogLevelName(level)) return level;
  }
  return std::nullopt;
}

std::expected<Settings, ConfigError> LoadSettings(const fs::path& config) {
  Settings settings;
  settings.host = kDefaultHost;
  std::error_code ec;
  if (!fs::exists(config, ec)) {
    if (ec) return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, ec.message()});
    return settings;
  }
  auto parsed = ParseTomlFile(config);
  if (!parsed) return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, parsed.error()});
  const toml::table& table = *parsed;
  if (auto bad = CheckManagedValues(config, table)) return std::unexpected(std::move(*bad));
  // Read as the game reads them, so a value such as mnk_mode = 1 keeps its
  // meaning. Saving writes it back as true or false.
  if (auto host = ManagedText(table, "gameserver_host")) settings.host = *host;
  if (auto fullscreen = ManagedText(table, "fullscreen")) settings.windowed = !GameFlag(*fullscreen);
  if (auto content = ManagedText(table, "content")) settings.content = *content;
  if (auto keyboard = ManagedText(table, "mnk_mode")) settings.keyboard = GameFlag(*keyboard);
  if (auto mouse_look = ManagedText(table, "mnk_mouse")) settings.mouse_look = GameFlag(*mouse_look);
  if (auto native = ManagedText(table, "native_renderer"); native && kHasNativeRenderer)
    settings.native_renderer = GameFlag(*native);
  if (auto fetch = ManagedText(table, "gpu_allow_invalid_fetch_constants"))
    settings.allow_invalid_fetch_constants = GameFlag(*fetch);
  if (auto level = ManagedText(table, "log_level")) settings.log_level = ParseLogLevel(*level);
  if (auto lua = ManagedText(table, "lua_print_log")) settings.lua_print_log = GameFlag(*lua);
  for (const auto& [key, node] : table) {
    if (ManagedKey(key.str())) continue;
    if (auto entry = EntryFromNode(std::string(key.str()), node)) settings.others.push_back(std::move(*entry));
  }
  return settings;
}

std::expected<void, ConfigError> SaveSettings(const fs::path& config, const Settings& settings) {
  if (!ValidHost(settings.host))
    return std::unexpected(ConfigError{ConfigError::Kind::InvalidHost, config, {}, settings.host});
  toml::table table;
  std::error_code ec;
  const bool exists = fs::exists(config, ec);
  if (ec) return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, ec.message()});
  if (exists) {
    auto parsed = ParseTomlFile(config);
    if (!parsed)
      return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, parsed.error()});
    if (auto bad = CheckManagedValues(config, *parsed)) return std::unexpected(std::move(*bad));
    table = std::move(*parsed);
  }
  for (const auto& key : settings.removed) table.erase(key);
  for (const auto& entry : settings.others) {
    switch (entry.kind) {
      case ConfigEntry::Kind::Flag: table.insert_or_assign(entry.key, entry.flag); break;
      case ConfigEntry::Kind::Integer: table.insert_or_assign(entry.key, entry.integer); break;
      case ConfigEntry::Kind::Number: table.insert_or_assign(entry.key, entry.number); break;
      case ConfigEntry::Kind::Text: table.insert_or_assign(entry.key, entry.text); break;
    }
  }
  table.insert_or_assign("gameserver_host", settings.host);
  table.insert_or_assign("content", settings.content);
  table.insert_or_assign("fullscreen", !settings.windowed);
  table.insert_or_assign("mnk_mode", settings.keyboard);
  table.insert_or_assign("mnk_mouse", settings.mouse_look);
  if (kHasNativeRenderer) table.insert_or_assign("native_renderer", settings.native_renderer);
  table.insert_or_assign("gpu_allow_invalid_fetch_constants", settings.allow_invalid_fetch_constants);
  if (settings.log_level)
    table.insert_or_assign("log_level", LogLevelName(*settings.log_level));
  else
    table.erase("log_level");
  table.insert_or_assign("lua_print_log", settings.lua_print_log);
  std::ostringstream text;
  text << table << '\n';
  if (auto written = WriteFileAtomically(config, text.str()); !written)
    return std::unexpected(ConfigError{ConfigError::Kind::WriteFailed, config, written.error().reason});
  return {};
}
