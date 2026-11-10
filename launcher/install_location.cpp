#include "install_location.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <system_error>

#include "files.h"

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr const char* kGameExecutable = "onevs100.exe";
#else
constexpr const char* kGameExecutable = "onevs100";
#endif

bool InAppBundle(const fs::path& executable_folder) {
  return executable_folder.filename() == "MacOS" && executable_folder.parent_path().filename() == "Contents";
}

}  // namespace

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
