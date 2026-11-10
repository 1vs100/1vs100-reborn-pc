#include "player_files.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "files.h"

namespace fs = std::filesystem;

namespace {

constexpr size_t kGamertagMax = 15;

}  // namespace

std::string Describe(const Error& error) {
  const std::string path = PathText(error.path);
  switch (error.kind) {
    case ErrorKind::InvalidGamertag:
      return "Enter a valid gamertag first (1-15 chars: letters, numbers, spaces, _ or -, no leading/trailing space).";
    case ErrorKind::InvalidHost:
      return "Enter a gameserver host first (hostname or IPv4, no spaces or ://).";
    case ErrorKind::ConfigUnreadable: return "Cannot read " + path + ": " + error.detail;
    case ErrorKind::ConfigValueInvalid: return "Fix " + path + ": " + error.detail + ".";
    case ErrorKind::ConfigReread: return "Loaded " + path + ". Check the settings, then press Launch.";
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

Error ToError(const ConfigError& error) {
  switch (error.kind) {
    case ConfigError::Kind::Unreadable: return Error{ErrorKind::ConfigUnreadable, error.file, error.detail};
    case ConfigError::Kind::BadValue: return Error{ErrorKind::ConfigValueInvalid, error.file, error.detail};
    case ConfigError::Kind::InvalidHost: return Error{ErrorKind::InvalidHost, error.file, error.detail};
    case ConfigError::Kind::WriteFailed: return Error{ErrorKind::WriteFailed, error.file, error.detail};
  }
  std::unreachable();
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
