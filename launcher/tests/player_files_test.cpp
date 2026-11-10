// The player file functions and WriteFileAtomically on a temporary folder,
// compared with fixtures/player_files.expected.txt.
//   player_files_test <fixtures folder> [--update]
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "files.h"
#include "player_files.h"

namespace fs = std::filesystem;

namespace {

fs::path root;

void WriteText(const fs::path& file, std::string_view text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Paths print relative to the temporary root, so the snapshot is the same on every run.
std::string Relative(const fs::path& path) {
  if (path.empty()) return "(none)";
  const std::string text = path.string();
  const std::string prefix = root.string();
  return text.rfind(prefix, 0) == 0 ? "<root>" + text.substr(prefix.size()) : text;
}

std::string Escaped(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (static_cast<unsigned char>(c) >= 0x80) {
      // Keeps the snapshot ASCII.
      static constexpr char kHex[] = "0123456789abcdef";
      const auto byte = static_cast<unsigned char>(c);
      out += "\\x";
      out += kHex[byte >> 4];
      out += kHex[byte & 0xF];
    } else {
      out += c;
    }
  }
  return out;
}

const char* KindName(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::InvalidGamertag: return "invalid_gamertag";
    case ErrorKind::InvalidHost: return "invalid_host";
    case ErrorKind::ConfigUnreadable: return "config_unreadable";
    case ErrorKind::ConfigValueInvalid: return "config_value_invalid";
    case ErrorKind::ConfigReread: return "config_reread";
    case ErrorKind::WriteFailed: return "write_failed";
    case ErrorKind::AvatarUnreadable: return "avatar_unreadable";
    case ErrorKind::GameMissing: return "game_missing";
    case ErrorKind::LaunchFailed: return "launch_failed";
    case ErrorKind::DataFolderUnavailable: return "data_folder_unavailable";
  }
  std::unreachable();
}

std::string DescribeError(const Error& error) {
  std::string text = std::string(KindName(error.kind)) + " path=" + Relative(error.path) + " detail=\"" + error.detail +
                     "\"";
  // The message names the path as given, so it is shown relative too.
  std::string message = Describe(error);
  const std::string prefix = root.string();
  for (size_t at = message.find(prefix); at != std::string::npos; at = message.find(prefix)) {
    message.replace(at, prefix.size(), "<root>");
  }
  return text + "\n  message: " + message;
}

std::string Result(const std::expected<void, Error>& result) {
  return result ? std::string("ok") : "error " + DescribeError(result.error());
}

// Every file under dir, so a stray temporary file shows in the snapshot.
std::string Tree(const fs::path& dir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
    names.push_back(Relative(entry.path()) + (entry.is_directory() ? "/" : ""));
  }
  std::sort(names.begin(), names.end());
  std::string text;
  for (const auto& name : names) text += "  " + name + "\n";
  return text;
}

Paths MakeTestPaths(const fs::path& base, const fs::path& data) {
  const fs::path profiles = data / "userdata" / "profiles";
  return Paths{.base = base,
               .data = data,
               .game = base / "onevs100",
               .shipped_config = base / "onevs100.toml",
               .config = data / "onevs100.toml",
               .launcher_config = data / "launcher.toml",
               .profiles = profiles,
               .gamertag = profiles / "local-name.txt",
               .avatar = profiles / "avatar.bin"};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const fs::path fixtures = argv[1];
  const bool update = argc > 2 && std::strcmp(argv[2], "--update") == 0;

  // A name of its own, so parallel runs do not delete each other's files.
  root = fs::temp_directory_path() / ("onevs100_player_files_test_" + std::to_string(std::random_device{}()));
  fs::remove_all(root);
  fs::create_directories(root);

  std::string actual;

  actual += "== valid gamertag\n";
  for (const char* name : {"Player", "Player One", "a", "abcdefghijklmno", "abcdefghijklmnop", "", " Lead", "Trail ",
                           "under_score", "dash-name", "dot.name", "Umlaut\xc3\xa4", "tab\tname"}) {
    actual += "\"" + Escaped(name) + "\" " + (ValidGamertag(name) ? "yes" : "no") + "\n";
  }

  actual += "== load gamertag\n";
  const fs::path gamertag_file = root / "local-name.txt";
  actual += "missing -> " + LoadGamertag(gamertag_file).value_or("(none)") + "\n";
  for (const char* text : {"Player\n", "Player\r\n", "Player", "Second\nLine two\n", " Bad\n", "", "\n"}) {
    WriteText(gamertag_file, text);
    actual += "\"" + Escaped(text) + "\" -> " + LoadGamertag(gamertag_file).value_or("(none)") + "\n";
  }

  actual += "== save gamertag\n";
  {
    const Paths paths = MakeTestPaths(root / "install", root / "data");
    actual += "invalid: " + Result(SaveGamertag(paths, " Bad")) + "\n";
    actual += "valid: " + Result(SaveGamertag(paths, "Player One")) + "\n";
    actual += "file: \"" + Escaped(ReadText(paths.gamertag)) + "\"\n";
    actual += "again: " + Result(SaveGamertag(paths, "Player Two")) + "\n";
    actual += "file: \"" + Escaped(ReadText(paths.gamertag)) + "\"\n";
  }

  actual += "== write and read bytes\n";
  {
    const fs::path avatar = root / "nested" / "folder" / "avatar.bin";
    const std::vector<uint8_t> bytes = {0x00, 0x01, 0xfe, 0xff, '\n', '\r'};
    actual += "write into missing folders: " + Result(WriteBytes(avatar, bytes.data(), bytes.size())) + "\n";
    const auto read = ReadBytes(avatar);
    actual += std::string("read back equal: ") + (read && *read == bytes ? "yes" : "no") + "\n";
    const auto missing = ReadBytes(root / "missing.bin");
    actual += "read missing: " + (missing ? std::string("ok") : "error " + DescribeError(missing.error())) + "\n";
  }

  actual += "== write file atomically\n";
  {
    const fs::path file = root / "atomic.txt";
    WriteText(file, "old contents");
    const auto replaced = WriteFileAtomically(file, "new");
    actual += "replace: " + (replaced ? std::string("ok") : "error " + replaced.error().reason) + "\n";
    actual += "file: \"" + ReadText(file) + "\"\n";
    const auto missing_folder = WriteFileAtomically(root / "no_such_folder" / "file.txt", "x");
    actual += std::string("into a missing folder: ") +
              (missing_folder ? "ok" : missing_folder.error().step == WriteError::Step::Write ? "write error" : "replace error") +
              "\n";
    // The new file keeps the permission bits of the one it replaces.
    const fs::path private_file = root / "private.txt";
    WriteText(private_file, "old");
    fs::permissions(private_file, fs::perms::owner_read | fs::perms::owner_write);
    const auto kept = WriteFileAtomically(private_file, "new");
    const bool owner_only = (fs::status(private_file).permissions() & fs::perms::all) ==
                            (fs::perms::owner_read | fs::perms::owner_write);
    actual += std::string("keeps permissions: ") + (kept && owner_only ? "yes" : "no") + "\n";
    // A symlink stays a symlink, and the file it points to changes.
    const fs::path real = root / "real.txt";
    const fs::path link = root / "link.txt";
    WriteText(real, "old");
    fs::create_symlink(real, link);
    const auto through_link = WriteFileAtomically(link, "new");
    actual += std::string("through a symlink: ") + (through_link ? "ok" : "error") +
              ", link kept: " + (fs::is_symlink(link) ? "yes" : "no") + ", target: \"" + ReadText(real) + "\"\n";
    // The rename fails over a folder that holds files; the temporary file goes.
    const fs::path folder = root / "folder_target";
    fs::create_directories(folder);
    WriteText(folder / "inside.txt", "kept");
    const auto over_folder = WriteFileAtomically(folder, "x");
    actual += std::string("over a folder with files: ") +
              (over_folder ? "ok" : over_folder.error().step == WriteError::Step::Write ? "write error" : "replace error") +
              "\n";
  }

  actual += "== prepare data folder\n";
  {
    const fs::path install = root / "same";
    fs::create_directories(install);
    WriteText(install / "onevs100.toml", "shipped = true\n");
    actual += "data next to the install: " + Result(PrepareDataFolder(MakeTestPaths(install, install))) + "\n";

    const fs::path data = root / "pref";
    const Paths paths = MakeTestPaths(install, data);
    fs::create_directories(data);
    actual += "separate, first run: " + Result(PrepareDataFolder(paths)) + "\n";
    actual += "config: \"" + Escaped(ReadText(paths.config)) + "\"\n";
    WriteText(paths.config, "edited = true\n");
    actual += "separate, again: " + Result(PrepareDataFolder(paths)) + "\n";
    actual += "config: \"" + Escaped(ReadText(paths.config)) + "\"\n";
  }

  actual += "== resolve content folder\n";
  {
    const Paths paths = MakeTestPaths(root / "install", root / "data");
    for (const std::string setting : {std::string(""), std::string("content"), std::string("packages/360"),
                                      (root / "elsewhere").string()}) {
      actual += "\"" + Relative(setting) + "\" -> " + Relative(ResolveContentDir(paths, setting)) + "\n";
    }
  }

  actual += "== config errors\n";
  for (const auto kind : {ConfigError::Kind::Unreadable, ConfigError::Kind::BadValue, ConfigError::Kind::InvalidHost,
                          ConfigError::Kind::WriteFailed}) {
    actual += DescribeError(ToError(ConfigError{kind, root / "onevs100.toml", "reason", std::nullopt})) +
              "\n";
  }

  actual += "== files left\n" + Tree(root);
  fs::remove_all(root);

  const fs::path expected_file = fixtures / "player_files.expected.txt";
  if (update) {
    WriteText(expected_file, actual);
    std::puts("updated player_files.expected.txt");
    return 0;
  }
  if (actual != ReadText(expected_file)) {
    std::fputs("FAIL: player file output differs from player_files.expected.txt; actual:\n", stdout);
    std::fputs(actual.c_str(), stdout);
    return 1;
  }
  std::puts("PASS");
  return 0;
}
