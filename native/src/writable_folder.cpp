#include "writable_folder.h"

#include <rex/filesystem.h>
#include <rex/platform.h>

#if REX_PLATFORM_MAC
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#endif

std::filesystem::path WritableFolder() {
#if REX_PLATFORM_WINRT
  // The package install folder is read-only.
  return rex::filesystem::GetUserFolder();
#else
  const auto install = rex::filesystem::GetExecutableFolder();
#if REX_PLATFORM_MAC
  // An app bundle can be read-only, and writing into it breaks its signature.
  // Same rule as MakePaths in launcher/main.cpp.
  if (install.filename() == "MacOS" && install.parent_path().filename() == "Contents") {
    static const std::filesystem::path folder = [] {
      char* pref = SDL_GetPrefPath("", "1vs100");
      if (!pref) {
        // Runs before SDL video starts, where fatal_dialog only prints to stderr.
        const std::string text = std::string("Cannot create the Application Support folder: ") + SDL_GetError();
        std::fputs((text + "\n").c_str(), stderr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "1 vs 100", text.c_str(), nullptr);
        std::exit(1);
      }
      const std::filesystem::path path(pref);
      SDL_free(pref);
      return path.has_filename() ? path : path.parent_path();
    }();
    return folder;
  }
#endif
  return install;
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
