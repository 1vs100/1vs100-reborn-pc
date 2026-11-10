// Where the launcher and the game keep their files, the launcher's errors,
// and the player's own files: gamertag and avatar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game_config.h"

struct Paths {
  // Folder holding the game executables.
  std::filesystem::path base;
  // Folder holding the files shipped with the game.
  std::filesystem::path shipped;
  // Folder for the config, player data and the default content folder.
  std::filesystem::path data;
  std::filesystem::path game;
  std::filesystem::path shipped_config;
  std::filesystem::path config;
  // Settings only the launcher reads; the game warns about unknown keys in config.
  std::filesystem::path launcher_config;
  std::filesystem::path profiles;
  std::filesystem::path gamertag;
  std::filesystem::path avatar;
};

enum class ErrorKind {
  InvalidGamertag,
  InvalidHost,
  ConfigUnreadable,
  ConfigValueInvalid,
  // The file could not be read earlier and reads now. Launch stops so the
  // player sees what it holds before it is saved.
  ConfigReread,
  WriteFailed,
  AvatarUnreadable,
  GameMissing,
  LaunchFailed,
  DataFolderUnavailable
};

struct Error {
  ErrorKind kind;
  std::filesystem::path path;
  std::string detail;
};

std::string Describe(const Error& error);

// Same rules as ValidName in native/src/local_profile.cpp.
bool GamertagChar(unsigned char c);
bool ValidGamertag(std::string_view name);

// Velopack keeps sq.version next to the executables it installs. Same rule
// as WritableFolder in native/src/writable_folder.cpp.
bool IsVelopackInstall(const std::filesystem::path& executable_folder);

// Works before SDL_Init.
std::filesystem::path ExecutableFolder();

Error ToError(const ConfigError& error);

std::expected<Paths, Error> MakePaths();

// A separate data folder starts with the shipped config, as the game does on
// first run, and an empty default content folder for the player to fill.
std::expected<void, Error> PrepareDataFolder(const Paths& paths);

// Nothing when the file is missing or holds no valid gamertag.
std::optional<std::string> LoadGamertag(const std::filesystem::path& file);
std::expected<void, Error> SaveGamertag(const Paths& paths, const std::string& name);

// Creates the parent folder.
std::expected<void, Error> WriteBytes(const std::filesystem::path& target, const uint8_t* data, size_t size);
std::expected<std::vector<uint8_t>, Error> ReadBytes(const std::filesystem::path& source);

// The content setting is relative to the data folder unless absolute.
std::filesystem::path ResolveContentDir(const Paths& paths, const std::string& setting);
