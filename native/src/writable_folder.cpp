#include "writable_folder.h"

#include <rex/filesystem.h>
#include <rex/platform.h>

#if !REX_PLATFORM_WINRT
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#endif

#if !REX_PLATFORM_WINRT
namespace {

// The install folder is replaced as a whole or is read-only: an app bundle
// (writing into it also breaks its signature) or a Velopack install, which
// keeps sq.version next to the executable. Same rule as MakePaths in
// launcher/main.cpp.
bool InstallFolderIsReplaced(const std::filesystem::path& install) {
#if REX_PLATFORM_MAC
  if (install.filename() == "MacOS" && install.parent_path().filename() == "Contents") return true;
#endif
  std::error_code error;
  return std::filesystem::is_regular_file(install / "sq.version", error);
}

std::filesystem::path PrefFolder() {
  char* pref = SDL_GetPrefPath("", "1vs100");
  if (!pref) {
    // Runs before SDL video starts, where fatal_dialog only prints to stderr.
    const std::string text = std::string("Cannot create the player data folder: ") + SDL_GetError();
    std::fputs((text + "\n").c_str(), stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "1 vs 100", text.c_str(), nullptr);
    std::exit(1);
  }
  // SDL returns UTF-8.
  const std::filesystem::path path = rex::to_path(pref);
  SDL_free(pref);
  return path.has_filename() ? path : path.parent_path();
}

}  // namespace
#endif

std::filesystem::path WritableFolder() {
#if REX_PLATFORM_WINRT
  // The package install folder is read-only.
  return rex::filesystem::GetUserFolder();
#else
  static const std::filesystem::path folder = [] {
    const auto install = rex::filesystem::GetExecutableFolder();
    return InstallFolderIsReplaced(install) ? PrefFolder() : install;
  }();
  return folder;
#endif
}

std::filesystem::path ShippedFolder() {
  const auto install = rex::filesystem::GetExecutableFolder();
#if REX_PLATFORM_MAC
  // codesign accepts only code in Contents/MacOS. Same rule as MakePaths in launcher/main.cpp.
  if (install.filename() == "MacOS" && install.parent_path().filename() == "Contents") {
    return install.parent_path() / "Resources";
  }
#endif
  return install;
}
