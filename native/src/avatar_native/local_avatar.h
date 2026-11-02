#pragma once
// Local player Avatar stored as userdata/profiles/avatar.bin.
#include <cstdint>
#include <filesystem>

namespace xe::kernel::xna::avatar {
class Catalog;
struct Description;
}

namespace local_avatar {
std::filesystem::path SavePath();
bool Load(const xe::kernel::xna::avatar::Catalog* catalog,
          xe::kernel::xna::avatar::Description* out);
bool Save(const xe::kernel::xna::avatar::Description& description);
xe::kernel::xna::avatar::Description Default(
    const xe::kernel::xna::avatar::Catalog& catalog, uint64_t xuid);
}  // namespace local_avatar
