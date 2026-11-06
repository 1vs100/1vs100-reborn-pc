// Reads one file from the root of an STFS package (CON, PIRS or LIVE).
// The same block and hash table rules as the SDK's StfsContainerDevice, so
// the avatar code needs no runtime.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace xe::kernel::xna::avatar {

bool ReadStfsRootFile(const std::filesystem::path& package, std::string_view name, std::vector<uint8_t>* out);

}  // namespace xe::kernel::xna::avatar
