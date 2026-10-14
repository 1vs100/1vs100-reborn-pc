#pragma once
// Local player's saved Avatar for the offline native build.
// Stored beside the build as userdata/profiles/avatar.bin (the 1020-byte
// description format that xna_avatar_format already reads and writes).
#include <cstdint>
#include <filesystem>

namespace xe::kernel::xna::avatar {
class Catalog;
struct Description;
}

namespace local_avatar {
std::filesystem::path SavePath();
// avatar_manifest.bin shared with the Avatar Editor (launcher's second app):
// ONEVS100_AVATAR_EDITOR_DIR, else <player data>\avatar-editor.
std::filesystem::path EditorManifestPath();
// Catalog from ONEVS100_NATIVE_AVATAR_CONTENT, or nullptr when the native
// Avatar provider is not enabled.
xe::kernel::xna::avatar::Catalog* EditorCatalog();
// True when a saved Avatar exists and parses. An Avatar Editor save newer
// than avatar.bin wins (it can carry closet items, which avatar.bin cannot
// name reliably).
bool Load(const xe::kernel::xna::avatar::Catalog* catalog,
          xe::kernel::xna::avatar::Description* out);
// Writes avatar.bin and the editor manifest, so the Avatar Editor opens on it.
bool Save(const xe::kernel::xna::avatar::Description& description);
// The appearance used before an Avatar is saved: seeded by the profile XUID.
xe::kernel::xna::avatar::Description Default(
    const xe::kernel::xna::avatar::Catalog& catalog, uint64_t xuid);
}  // namespace local_avatar

namespace local_avatar {
// Last change time of the Avatar Editor's save (0 when there is none), so a
// caller can tell whether the editor saved while it waited.
long long EditorSaveStamp();
}  // namespace local_avatar

#ifdef _WIN32
#include <windows.h>
// Starts the Avatar Editor (ONEVS100_AVATAR_EDITOR_EXE) on this player's
// Avatar. It runs alongside the game; its saves are read the next time Avatars
// load. Shows a message and returns false when it is not installed.
bool OpenAvatarEditor(HWND owner);
#endif
