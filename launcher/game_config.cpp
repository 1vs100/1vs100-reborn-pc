#include "game_config.h"

#include <algorithm>
#include <charconv>
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

// strtod, not from_chars: older libc++ has no floating-point from_chars. Only
// plain decimals count, so text such as 0x10, inf or nan stays text.
bool ParseDecimal(std::string_view value, double* out) {
  if (value.empty() || value.find_first_not_of("0123456789.-eE") != std::string_view::npos) return false;
  const std::string text(value);
  char* end = nullptr;
  *out = std::strtod(text.c_str(), &end);
  return !text.empty() && end == text.c_str() + text.size();
}

}  // namespace

bool ManagedKey(std::string_view key) {
  // Without the checkbox the key stays in the advanced list, for builds that enable the renderer.
  if (kHasNativeRenderer && key == "native_renderer") return true;
  return std::find(kManagedKeys.begin(), kManagedKeys.end(), key) != kManagedKeys.end();
}

ConfigEntry EntryFromText(std::string key, std::string_view value) {
  ConfigEntry entry;
  entry.key = std::move(key);
  int64_t integer = 0;
  double number = 0;
  const char* end = value.data() + value.size();
  if (value == "true" || value == "false") {
    entry.kind = ConfigEntry::Kind::Flag;
    entry.flag = value == "true";
  } else if (const auto parsed = std::from_chars(value.data(), end, integer);
             !value.empty() && parsed.ec == std::errc() && parsed.ptr == end) {
    entry.kind = ConfigEntry::Kind::Integer;
    entry.integer = integer;
  } else if (ParseDecimal(value, &number)) {
    entry.kind = ConfigEntry::Kind::Number;
    entry.number = number;
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
  if (!fs::exists(config, ec)) return settings;
  auto parsed = ParseTomlFile(config);
  if (!parsed) return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, parsed.error()});
  const toml::table& table = *parsed;
  if (auto host = table["gameserver_host"].value<std::string>()) settings.host = *host;
  if (auto fullscreen = table["fullscreen"].value<bool>()) settings.windowed = !*fullscreen;
  if (auto content = table["content"].value<std::string>()) settings.content = *content;
  if (auto keyboard = table["mnk_mode"].value<bool>()) settings.keyboard = *keyboard;
  if (auto mouse_look = table["mnk_mouse"].value<bool>()) settings.mouse_look = *mouse_look;
  if (auto native = table["native_renderer"].value<bool>(); native && kHasNativeRenderer)
    settings.native_renderer = *native;
  if (auto fetch = table["gpu_allow_invalid_fetch_constants"].value<bool>())
    settings.allow_invalid_fetch_constants = *fetch;
  if (auto level = table["log_level"].value<std::string>()) settings.log_level = ParseLogLevel(*level);
  if (auto lua = table["lua_print_log"].value<bool>()) settings.lua_print_log = *lua;
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
  if (fs::exists(config, ec)) {
    auto parsed = ParseTomlFile(config);
    if (!parsed)
      return std::unexpected(ConfigError{ConfigError::Kind::Unreadable, config, parsed.error()});
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
