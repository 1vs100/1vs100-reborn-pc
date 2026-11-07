// Content package name handling against the names found in dumps.
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "content_name.h"

namespace {

int failures = 0;

void Expect(std::string_view input, std::string_view actual, std::string_view expected) {
  if (actual == expected) return;
  std::fprintf(stderr, "FAIL %.*s: got %.*s, want %.*s\n", int(input.size()), input.data(),
               int(actual.size()), actual.data(), int(expected.size()), expected.data());
  ++failures;
}

void Short(std::string_view input, std::string_view expected) {
  Expect(input, ShortContentName(input).value_or("none"), expected);
}

// Records each path passed to Match; matches only the listed paths.
struct FakeManifest {
  std::vector<std::string> paths;
  mutable std::string queries{};
  std::optional<size_t> Match(std::string_view path) const {
    queries += std::string(path) + ";";
    for (size_t i = 0; i < paths.size(); ++i)
      if (paths[i] == path) return i;
    return std::nullopt;
  }
};

void Match(std::string_view input, std::string_view expected) {
  const FakeManifest manifest{{"content/83136FF343124795DC1806A2DB044476589BD5C058", "content/FFFE07DF00000002"}};
  const auto index = MatchContentPath(manifest, input);
  Expect(input, (index ? std::to_string(*index) : "none") + " " + manifest.queries, expected);
}

}  // namespace

int main() {
  Short("2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D58", "2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D");
  Short("2d23f392b51e38b1a93fa1e1b9fae7b2c5dbbd6d58", "2d23f392b51e38b1a93fa1e1b9fae7b2c5dbbd6d");
  Short("2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D", "none");
  Short("2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D59", "none");
  Short("2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBDXX58", "none");
  Short("FFFE07DF00000002", "none");
  Short("AvatarAssetPack.toc", "none");
  Short("", "none");

  Match("content/83136FF343124795DC1806A2DB044476589BD5C058",
        "0 content/83136FF343124795DC1806A2DB044476589BD5C058;");
  Match("content/83136FF343124795DC1806A2DB044476589BD5C0",
        "0 content/83136FF343124795DC1806A2DB044476589BD5C0;content/83136FF343124795DC1806A2DB044476589BD5C058;");
  Match("content/83136FF343124795DC1806A2DB044476589BD5C", "none content/83136FF343124795DC1806A2DB044476589BD5C;");
  Match("content/FFFE07DF00000002", "1 content/FFFE07DF00000002;");
  if (failures == 0) std::printf("content_name_test ok\n");
  return failures == 0 ? 0 : 1;
}
