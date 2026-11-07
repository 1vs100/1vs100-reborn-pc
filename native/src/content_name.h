// Content package file names. The full name is "<40-hex content id>58", the
// name the game queries. Some dumps carry the short name, without the "58".
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

inline constexpr size_t kContentIdLength = 40;
inline constexpr std::string_view kContentNameSuffix = "58";

constexpr bool IsContentId(std::string_view name) {
  if (name.size() != kContentIdLength) return false;
  for (char c : name) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

// The short name for a full content package name; nullopt for any other name.
constexpr std::optional<std::string_view> ShortContentName(std::string_view name) {
  if (name.size() != kContentIdLength + kContentNameSuffix.size() ||
      name.substr(kContentIdLength) != kContentNameSuffix || !IsContentId(name.substr(0, kContentIdLength))) {
    return std::nullopt;
  }
  return name.substr(0, kContentIdLength);
}

// Matches `path` against a manifest by its own name, else, when its last
// '/'-separated component is a short content name, by the full name.
template <class Manifest>
auto MatchContentPath(const Manifest& manifest, std::string_view path) {
  auto index = manifest.Match(path);
  const size_t slash = path.rfind('/');
  if (!index && IsContentId(path.substr(slash == std::string_view::npos ? 0 : slash + 1))) {
    index = manifest.Match(std::string(path) + std::string(kContentNameSuffix));
  }
  return index;
}
