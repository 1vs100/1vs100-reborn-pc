#include "player_files.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "files.h"

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr const char* kGameExecutable = "onevs100.exe";
#else
constexpr const char* kGameExecutable = "onevs100";
#endif

constexpr size_t kGamertagMax = 15;

bool InAppBundle(const fs::path& executable_folder) {
  return executable_folder.filename() == "MacOS" && executable_folder.parent_path().filename() == "Contents";
}

}  // namespace

std::string Describe(const Error& error) {
  const std::string path = PathText(error.path);
  switch (error.kind) {
    case ErrorKind::InvalidGamertag:
      return "Enter a valid gamertag first (1-15 chars: letters, numbers, spaces, _ or -, no leading/trailing space).";
    case ErrorKind::InvalidHost:
      return "Enter a gameserver host first (hostname or IPv4, no spaces or ://).";
    case ErrorKind::ConfigUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::WriteFailed: return "Cannot write " + path + ": " + error.detail;
    case ErrorKind::AvatarUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::GameMissing: return "Game not found: " + path;
    case ErrorKind::LaunchFailed: return "Cannot start the game: " + error.detail;
    case ErrorKind::DataFolderUnavailable: return "Cannot create the player data folder: " + error.detail;
  }
  std::unreachable();
}

bool GamertagChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
         c == '-';
}
bool ValidGamertag(std::string_view name) {
  if (name.empty() || name.size() > kGamertagMax || name.front() == ' ' || name.back() == ' ') return false;
  for (unsigned char c : name)
    if (!GamertagChar(c)) return false;
  return true;
}

bool IsVelopackInstall(const fs::path& executable_folder) {
  std::error_code ec;
  return fs::is_regular_file(executable_folder / "sq.version", ec);
}

fs::path ExecutableFolder() {
  // Do NOT SDL_free the result: this SDL version caches the base path and
  // frees it in SDL_Quit, so freeing here double-frees on shutdown.
  const char* base_path = SDL_GetBasePath();
  fs::path base = base_path ? PathFromUtf8(base_path) : fs::current_path();
  if (!base.has_filename()) base = base.parent_path();
#ifdef __APPLE__
  // In a bundle SDL_GetBasePath is Contents/Resources; the executables are in Contents/MacOS.
  if (base.filename() == "Resources" && base.parent_path().filename() == "Contents") {
    base = base.parent_path() / "MacOS";
  }
#endif
  return base;
}

Error ToError(const ConfigError& error) {
  switch (error.kind) {
    case ConfigError::Kind::Unreadable: return Error{ErrorKind::ConfigUnreadable, error.file, error.detail};
    case ConfigError::Kind::InvalidHost: return Error{ErrorKind::InvalidHost, error.file, error.detail};
    case ConfigError::Kind::WriteFailed: return Error{ErrorKind::WriteFailed, error.file, error.detail};
  }
  std::unreachable();
}

std::expected<Paths, Error> MakePaths() {
  const fs::path base = ExecutableFolder();
  // The install folder is replaced as a whole or is read-only: an app bundle
  // or a Velopack install. Same rule as WritableFolder in
  // native/src/writable_folder.cpp.
  const bool replaced = InAppBundle(base) || IsVelopackInstall(base);
  // codesign accepts only code in Contents/MacOS. Same rule as ShippedFolder in native/src/writable_folder.cpp.
  const fs::path shipped = InAppBundle(base) ? base.parent_path() / "Resources" : base;
  fs::path data = base;
  if (replaced) {
    char* pref = SDL_GetPrefPath("", "1vs100");
    if (!pref) return std::unexpected(Error{ErrorKind::DataFolderUnavailable, {}, SDL_GetError()});
    data = PathFromUtf8(pref);
    SDL_free(pref);
    if (!data.has_filename()) data = data.parent_path();
  }
  // Same rule as PlayerDataFolder in native/src/local_profile.cpp.
  const char* player_data = SDL_getenv("ONEVS100_PLAYER_DATA");
  const fs::path player = (player_data && *player_data) ? PathFromUtf8(player_data) : data / "userdata";
  const fs::path profiles = player / "profiles";
  return Paths{.base = base,
               .shipped = shipped,
               .data = data,
               .game = base / kGameExecutable,
               .shipped_config = shipped / "onevs100.toml",
               .config = data / "onevs100.toml",
               .launcher_config = data / "launcher.toml",
               .profiles = profiles,
               .gamertag = profiles / "local-name.txt",
               .avatar = profiles / "avatar.bin"};
}

std::expected<void, Error> PrepareDataFolder(const Paths& paths) {
  if (paths.data == paths.base) return {};
  std::error_code ec;
  if (!fs::exists(paths.config, ec) && fs::exists(paths.shipped_config, ec)) {
    // Atomic, since a partial copy would stay: the copy only runs while the file is missing.
    std::ifstream in(paths.shipped_config, std::ios::binary);
    const std::string shipped((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // A read error ends the copy early without setting a stream flag.
    const uintmax_t size = fs::file_size(paths.shipped_config, ec);
    if (!in.is_open() || ec || shipped.size() != size)
      return std::unexpected(Error{ErrorKind::ConfigUnreadable, paths.shipped_config, "cannot read file"});
    if (auto written = WriteFileAtomically(paths.config, shipped); !written)
      return std::unexpected(Error{ErrorKind::WriteFailed, paths.config, written.error().reason});
  }
  const fs::path content = paths.data / kDefaultContent;
  fs::create_directories(content, ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, content, ec.message()});
  return {};
}

std::optional<std::string> LoadGamertag(const fs::path& file) {
  std::ifstream in(file);
  std::string name;
  if (in && std::getline(in, name)) {
    if (!name.empty() && name.back() == '\r') name.pop_back();
    if (ValidGamertag(name)) return name;
  }
  return std::nullopt;
}

std::expected<void, Error> SaveGamertag(const Paths& paths, const std::string& name) {
  if (!ValidGamertag(name)) return std::unexpected(Error{ErrorKind::InvalidGamertag, {}, {}});
  std::error_code ec;
  fs::create_directories(paths.profiles, ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, paths.profiles, ec.message()});
  if (auto written = WriteFileAtomically(paths.gamertag, name + "\n"); !written)
    return std::unexpected(Error{ErrorKind::WriteFailed, paths.gamertag, written.error().reason});
  return {};
}

std::expected<void, Error> WriteBytes(const fs::path& target, const uint8_t* data, size_t size) {
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (ec) return std::unexpected(Error{ErrorKind::WriteFailed, target.parent_path(), ec.message()});
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  if (auto written = WriteFileAtomically(target, bytes); !written)
    return std::unexpected(Error{ErrorKind::WriteFailed, target, written.error().reason});
  return {};
}

std::expected<std::vector<uint8_t>, Error> ReadBytes(const fs::path& source) {
  std::ifstream in(source, std::ios::binary);
  if (!in) return std::unexpected(Error{ErrorKind::AvatarUnreadable, source, "cannot open file"});
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path ResolveContentDir(const Paths& paths, const std::string& setting) {
  const std::string raw = setting.empty() ? kDefaultContent : setting;
  fs::path dir = PathFromUtf8(raw.c_str());
  if (dir.is_relative()) dir = paths.data / dir;
  return dir;
}
