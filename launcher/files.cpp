#include "files.h"

#ifdef _WIN32
#include <SDL3/SDL_stdinc.h>
#endif

#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

fs::path PathFromUtf8(const char* text) {
#ifdef _WIN32
  auto* wide = reinterpret_cast<wchar_t*>(SDL_iconv_string("UTF-16LE", "UTF-8", text, SDL_strlen(text) + 1));
  fs::path path(wide ? wide : L"");
  SDL_free(wide);
  return path;
#else
  return fs::path(text);
#endif
}

std::string PathText(const fs::path& path) {
#ifdef _WIN32
  const std::wstring& wide = path.native();
  char* utf8 = SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char*>(wide.c_str()),
                                (wide.size() + 1) * sizeof(wchar_t));
  std::string text(utf8 ? utf8 : "");
  SDL_free(utf8);
  return text;
#else
  return path.string();
#endif
}

std::expected<toml::table, std::string> ParseTomlFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    std::error_code ec;
    (void)fs::exists(file, ec);
    return std::unexpected(ec ? ec.message() : std::string("cannot open file"));
  }
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try {
    return toml::parse(contents, PathText(file));
  } catch (const toml::parse_error& e) {
    return std::unexpected(std::string(e.description()));
  }
}
