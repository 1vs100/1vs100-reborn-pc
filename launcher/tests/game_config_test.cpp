// LoadSettings, SaveSettings and the value parsers on onevs100.toml files,
// compared with fixtures/game_config.expected.txt.
//   game_config_test <fixtures folder> [--update]
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <random>
#include <string>
#include <string_view>

#include "game_config.h"

namespace fs = std::filesystem;

namespace {

void WriteText(const fs::path& file, std::string_view text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// native_renderer is a managed key only where the game builds that renderer,
// so lines naming it are left out to keep one snapshot for every host.
std::string WithoutNativeRenderer(const std::string& text) {
  std::string out;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string_view line(text.data() + start, end - start);
    if (line.find("native_renderer") == std::string_view::npos) {
      out.append(line);
      out += '\n';
    }
    start = end + 1;
  }
  return out;
}

const char* Flag(bool value) { return value ? "yes" : "no"; }

std::string DescribeEntry(const ConfigEntry& entry) {
  switch (entry.kind) {
    case ConfigEntry::Kind::Flag: return entry.key + " flag " + Flag(entry.flag);
    case ConfigEntry::Kind::Integer: return entry.key + " integer " + std::to_string(entry.integer);
    case ConfigEntry::Kind::Number: return entry.key + " number " + std::to_string(entry.number);
    case ConfigEntry::Kind::Text: return entry.key + " text \"" + entry.text + "\"";
  }
  std::unreachable();
}

const char* ErrorKindName(ConfigError::Kind kind) {
  switch (kind) {
    case ConfigError::Kind::Unreadable: return "unreadable";
    case ConfigError::Kind::BadValue: return "bad_value";
    case ConfigError::Kind::InvalidHost: return "invalid_host";
    case ConfigError::Kind::WriteFailed: return "write_failed";
  }
  std::unreachable();
}

std::string Describe(const std::expected<Settings, ConfigError>& loaded) {
  if (!loaded) {
    return std::string("error ") + ErrorKindName(loaded.error().kind) + " " + loaded.error().file.filename().string() +
           ": " + loaded.error().detail + "\n";
  }
  const Settings& s = *loaded;
  std::string text;
  text += "host " + s.host + "\n";
  text += std::string("windowed ") + Flag(s.windowed) + "\n";
  text += "content " + s.content + "\n";
  text += std::string("keyboard ") + Flag(s.keyboard) + "\n";
  text += std::string("mouse_look ") + Flag(s.mouse_look) + "\n";
  text += std::string("allow_invalid_fetch_constants ") + Flag(s.allow_invalid_fetch_constants) + "\n";
  text += std::string("log_level ") + (s.log_level ? LogLevelName(*s.log_level) : "none") + "\n";
  text += std::string("lua_print_log ") + Flag(s.lua_print_log) + "\n";
  for (const auto& entry : s.others) text += "other " + DescribeEntry(entry) + "\n";
  return text;
}

std::string DescribeSave(const std::expected<void, ConfigError>& saved, const fs::path& file) {
  if (!saved) {
    return std::string("error ") + ErrorKindName(saved.error().kind) + ": " + saved.error().detail +
           " refused_host=" + saved.error().refused_host.value_or("none") + "\n" +
           "file unchanged:\n" + ReadText(file);
  }
  return "saved:\n" + ReadText(file);
}

constexpr std::string_view kFullConfig =
    "gameserver_host = \"play.example.org\"\n"
    "fullscreen = false\n"
    "content = \"D:/packages\"\n"
    "mnk_mode = true\n"
    "mnk_mouse = true\n"
    "gpu_allow_invalid_fetch_constants = false\n"
    "log_level = \"debug\"\n"
    "lua_print_log = true\n"
    "vsync = false\n"
    "frame_limit = 60\n"
    "ui_scale = 1.25\n"
    "language = \"de\"\n"
    "# Arrays and tables are not editable entries and are left out.\n"
    "list = [1, 2]\n"
    "[section]\n"
    "key = 1\n";

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const fs::path fixtures = argv[1];
  const bool update = argc > 2 && std::strcmp(argv[2], "--update") == 0;

  // A name of its own, so parallel runs do not delete each other's files.
  const fs::path root = fs::temp_directory_path() / ("onevs100_game_config_test_" + std::to_string(std::random_device{}()));
  fs::remove_all(root);
  fs::create_directories(root);

  std::string actual;

  const fs::path full = root / "full.toml";
  WriteText(full, kFullConfig);
  actual += "== load full\n" + Describe(LoadSettings(full));

  actual += "== load missing\n" + Describe(LoadSettings(root / "missing.toml"));

  // Keys with their own control, read the way the game reads them: as text,
  // and for flags only true, 1 or yes count as true.
  const fs::path wrong_type = root / "wrong_type.toml";
  for (const char* text : {"gameserver_host = 5\n", "content = false\n", "fullscreen = \"no\"\n", "mnk_mode = 1\n",
                           "mnk_mouse = \"yes\"\n", "lua_print_log = \"true\"\n", "gpu_allow_invalid_fetch_constants = 0\n"}) {
    WriteText(wrong_type, text);
    actual += std::string("== load as the game reads it: ") + text + Describe(LoadSettings(wrong_type));
  }
  // Values the game would not read.
  for (const char* text : {"log_level = \"verbose\"\n", "log_level = \"INFO\"\n", "log_level = \"warning\"\n",
                           "log_level = 3\n", "fullscreen = [true]\n", "fullscreen = 1979-05-27\n", "[mnk]\nmode = true\n",
                           "[gpu]\n[gpu.allow_invalid]\nfetch_constants = false\n"}) {
    WriteText(wrong_type, text);
    actual += std::string("== load refused: ") + text + Describe(LoadSettings(wrong_type));
  }
  {
    WriteText(wrong_type, "log_level = \"verbose\"\nvsync = true\n");
    Settings settings;
    settings.host = kDefaultHost;
    actual += "== save over wrong type\n" + DescribeSave(SaveSettings(wrong_type, settings), wrong_type);
  }
  // Not a managed key, so any type stays as it is.
  WriteText(wrong_type, "vsync = \"maybe\"\n");
  actual += "== load other key of any type\n" + Describe(LoadSettings(wrong_type));

  const fs::path broken = root / "broken.toml";
  WriteText(broken, "gameserver_host = \"unterminated\n");
  actual += "== load broken\n" + Describe(LoadSettings(broken));

  // Save over the full file: edit managed keys, change one other key, add one
  // and remove one. Keys the launcher does not show (list, section) stay.
  {
    auto settings = LoadSettings(full);
    settings->host = "10.0.0.2";
    settings->windowed = false;
    settings->log_level.reset();
    for (auto& entry : settings->others) {
      if (entry.key == "frame_limit") entry.integer = 30;
    }
    settings->others.push_back(EntryFromText("new_key", "text value"));
    settings->removed.push_back("vsync");
    std::erase_if(settings->others, [](const ConfigEntry& entry) { return entry.key == "vsync"; });
    actual += "== save full\n" + DescribeSave(SaveSettings(full, *settings), full);
  }
  {
    Settings settings;
    settings.host = "bad host/name";
    actual += "== save invalid host\n" + DescribeSave(SaveSettings(full, settings), full);
  }
  {
    Settings settings;
    settings.host = kDefaultHost;
    const fs::path fresh = root / "fresh.toml";
    actual += "== save new file\n" + DescribeSave(SaveSettings(fresh, settings), fresh);
  }

  actual += "== entry from text\n";
  for (const char* value : {"true", "false", "42", "-7", "3.5", "-0.5", "1e3", "0x10", "inf", "nan", "", "hello",
                            "1.2.3", "99999999999999999999", "True"}) {
    actual += "\"" + std::string(value) + "\" -> " + DescribeEntry(EntryFromText("k", value)) + "\n";
  }

  actual += "== valid host\n";
  for (const char* host : {"1vs100.fans", "192.168.1.10", "localhost", "", "a b", "http://x", "host:1234", "a/b",
                           "x#y", "quote\"", "it's"}) {
    actual += "\"" + std::string(host) + "\" " + Flag(ValidHost(host)) + "\n";
  }
  actual += "length 253 " + std::string(Flag(ValidHost(std::string(253, 'a')))) + "\n";
  actual += "length 254 " + std::string(Flag(ValidHost(std::string(254, 'a')))) + "\n";

  actual += "== valid config key\n";
  for (const char* key : {"vsync", "gpu_x2", "", "with space", "dash-key", "dot.key"}) {
    actual += "\"" + std::string(key) + "\" " + Flag(ValidConfigKey(key)) + "\n";
  }

  actual += "== managed key\n";
  for (const char* key : {"gameserver_host", "log_level", "vsync"}) {
    actual += std::string(key) + " " + Flag(ManagedKey(key)) + "\n";
  }

  actual += "== log level\n";
  for (const char* level : {"trace", "debug", "info", "warn", "error", "critical", "off", "Info", "warning", ""}) {
    const auto parsed = ParseLogLevel(level);
    actual += "\"" + std::string(level) + "\" -> " + (parsed ? LogLevelName(*parsed) : "none") + "\n";
  }

  fs::remove_all(root);
  actual = WithoutNativeRenderer(actual);

  const fs::path expected_file = fixtures / "game_config.expected.txt";
  if (update) {
    WriteText(expected_file, actual);
    std::puts("updated game_config.expected.txt");
    return 0;
  }
  if (actual != ReadText(expected_file)) {
    std::fputs("FAIL: game config output differs from game_config.expected.txt; actual:\n", stdout);
    std::fputs(actual.c_str(), stdout);
    return 1;
  }
  std::puts("PASS");
  return 0;
}
