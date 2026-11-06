// A sona code as 360sona.com copies it: decodes to the manifest bytes
// (fixtures/sona_code.bin, decoded separately with Python's base64), and the
// parsed avatar matches fixtures/sona_code.expected.txt.
//   sona_code_test <fixtures folder> [--update]
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "xna_avatar_format.h"

namespace av = xe::kernel::xna::avatar;

namespace {

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string Hex(const uint8_t* bytes, size_t size) {
  std::string text;
  for (size_t i = 0; i < size; ++i) text += fmt::format("{:02x}", bytes[i]);
  return text;
}

// Every field the manifest carries, one per line, so a change shows as a readable diff.
std::string Describe(const av::Description& description) {
  std::string text = fmt::format("body {}\nheight_bits {:08x}\nweight_bits {:08x}\n", description.body,
                                 description.height_bits, description.weight_bits);
  for (uint32_t i = 0; i < av::kColorCount; ++i) {
    text += fmt::format("color {} {:08x}\n", av::ColorName(i), description.colors[i]);
  }
  for (size_t i = 0; i < description.blend_shapes.size(); ++i) {
    text += fmt::format("blend_shape {} {}\n", i, Hex(description.blend_shapes[i].data(), 16));
  }
  for (const auto& component : description.components) {
    text += fmt::format("component {}\n", Hex(component.data(), component.size()));
  }
  return text;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const std::filesystem::path fixtures = argv[1];
  const bool update = argc > 2 && std::strcmp(argv[2], "--update") == 0;

  const auto decoded = av::DecodeAvatarText(ReadText(fixtures / "sona_code.txt"));
  const std::string expected_bytes = ReadText(fixtures / "sona_code.bin");
  if (!decoded || decoded->size() != av::kManifestBytes || expected_bytes.size() != av::kManifestBytes ||
      std::memcmp(decoded->data(), expected_bytes.data(), expected_bytes.size()) != 0) {
    std::puts("FAIL: the sona code did not decode to the manifest bytes");
    return 1;
  }

  // No asset pack here: parsing a manifest needs none.
  av::Description description;
  if (!av::ParseAnyDescription(nullptr, decoded->data(), decoded->size(), &description)) {
    std::puts("FAIL: the decoded manifest did not parse");
    return 1;
  }
  const std::string actual = Describe(description);
  const auto snapshot = fixtures / "sona_code.expected.txt";
  if (update) {
    std::ofstream(snapshot, std::ios::binary) << actual;
    std::puts("updated sona_code.expected.txt");
    return 0;
  }
  if (actual != ReadText(snapshot)) {
    std::printf("FAIL: parsed avatar differs from sona_code.expected.txt\n%s", actual.c_str());
    return 1;
  }
  std::puts("PASS: sona code decodes and parses as recorded");
  return 0;
}
