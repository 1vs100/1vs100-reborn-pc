// Local player Avatar stored as userdata/profiles/avatar.bin.
#include "local_avatar.h"
#include "../local_profile.h"
#include "xna_avatar_format.h"
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace av = xe::kernel::xna::avatar;

namespace local_avatar {
std::filesystem::path SavePath() {
  return PlayerDataFolder() / "profiles" / "avatar.bin";
}

namespace {
std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
bool WriteAll(const std::filesystem::path& path, const uint8_t* data, size_t size) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data), std::streamsize(size));
    file.flush();
    if (!file) return false;
  }
  std::filesystem::rename(temporary, path, error);
  return !error;
}
}  // namespace

bool Load(const av::Catalog* catalog, av::Description* out) {
  const auto bytes = ReadAll(SavePath());
  if ((bytes.size() != av::kDescriptionBytes && bytes.size() != av::kManifestBytes) ||
      !av::ParseAnyDescription(catalog, bytes.data(), bytes.size(), out))
    return false;
  return true;
}

bool Save(const av::Description& description) {
  const auto bytes = av::SerializeDescription(description);
  return WriteAll(SavePath(), bytes.data(), bytes.size());
}

av::Description Default(const av::Catalog& catalog, uint64_t xuid) {
  std::mt19937 profile(uint32_t(xuid) ^ uint32_t(xuid >> 32));
  return av::RandomDescription(catalog, profile, -1);
}
}  // namespace local_avatar
