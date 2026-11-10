#include "files.h"

#ifdef _WIN32
#include <SDL3/SDL_stdinc.h>
#endif

#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <thread>

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

namespace {

std::error_code ReplaceFile(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::rename(from, to, ec);
#ifdef _WIN32
  // A virus scanner or sync client often holds a new file for a moment, and
  // a reader that opened the target without FILE_SHARE_DELETE blocks the
  // rename until it closes: ERROR_ACCESS_DENIED or ERROR_SHARING_VIOLATION.
  // A read-only target gives ERROR_ACCESS_DENIED too, and waiting cannot help.
  constexpr int kAccessDenied = 5;
  constexpr int kSharingViolation = 32;
  const auto busy = [&ec] {
    return ec && ec.category() == std::system_category() &&
           (ec.value() == kAccessDenied || ec.value() == kSharingViolation);
  };
  std::error_code ignored;
  const auto status = fs::status(to, ignored);
  const bool read_only = fs::exists(status) && (status.permissions() & fs::perms::owner_write) == fs::perms::none;
  for (int attempt = 0; attempt < 5 && busy() && !read_only; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20 << attempt));
    fs::rename(from, to, ec);
  }
#endif
  return ec;
}

}  // namespace

std::expected<void, WriteError> WriteFileAtomically(const fs::path& file, std::string_view contents) {
  std::error_code ec;
  fs::path target = fs::weakly_canonical(file, ec);
  if (ec) target = file;
  // A name of its own, so two launchers saving at once do not share one.
  fs::path temporary = target;
  temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
  const auto discard = [&temporary] {
    std::error_code ignored;
    fs::remove(temporary, ignored);
  };
  std::ofstream out(temporary, std::ios::trunc | std::ios::binary);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.close();
  if (!out) {
    discard();
    return std::unexpected(WriteError{WriteError::Step::Write, "write error"});
  }
  if (const auto status = fs::status(target, ec); !ec && fs::exists(status)) {
    std::error_code ignored;
    fs::permissions(temporary, status.permissions(), ignored);
  }
  if (const std::error_code replaced = ReplaceFile(temporary, target)) {
    discard();
    return std::unexpected(WriteError{WriteError::Step::Replace, replaced.message()});
  }
  return {};
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
