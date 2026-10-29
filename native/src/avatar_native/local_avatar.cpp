// Local player's saved Avatar and its editor for the offline native build.
#include "local_avatar.h"
#include "../local_profile.h"
#include "xna_avatar_format.h"
#include <rex/filesystem.h>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
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

av::Catalog* EditorCatalog() {
  const char* path = std::getenv("ONEVS100_NATIVE_AVATAR_CONTENT");
  return path && *path ? av::SharedCatalog(std::filesystem::path(path)) : nullptr;
}

std::filesystem::path EditorManifestPath() {
  const char* dir = std::getenv("ONEVS100_AVATAR_EDITOR_DIR");
  const std::filesystem::path folder =
      dir && *dir ? std::filesystem::path(dir) : PlayerDataFolder() / "avatar-editor";
  return folder / "avatar_manifest.bin";
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
  // Write a temporary file first so a failed save never leaves half an Avatar.
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
bool ReadEditorManifest(const av::Catalog* catalog, av::Description* out) {
  const auto bytes = ReadAll(EditorManifestPath());
  return bytes.size() == av::kManifestBytes && av::ParseManifest(catalog, bytes.data(), bytes.size(), out);
}
// True when the Avatar Editor saved after avatar.bin was last written.
bool EditorSavedLast() {
  std::error_code error;
  const auto manifestTime = std::filesystem::last_write_time(EditorManifestPath(), error);
  if (error) return false;
  const auto savedTime = std::filesystem::last_write_time(SavePath(), error);
  return error || manifestTime > savedTime;
}
bool HasClosetItem(const av::Catalog* catalog, const av::Description& description) {
  for (const uint16_t item : description.items) {
    if (item == av::kNoItem) continue;
    const av::Entry* entry = catalog->Find(item);
    if (!entry || entry->external) return true;  // closet item, or one no longer there
  }
  return false;
}
void ExportToEditor(const av::Catalog* catalog, const av::Description& description) {
  if (!catalog) return;
  const auto manifest = av::SerializeManifest(catalog, description, LocalXuid());
  if (!WriteAll(EditorManifestPath(), manifest.data(), manifest.size()))
    std::fprintf(stderr, "Avatar: could not write %s\n", EditorManifestPath().string().c_str());
}
}  // namespace

bool Load(const av::Catalog* catalog, av::Description* out) {
  // A save from the Avatar Editor (launcher > Avatar Editor) after the last
  // in-game save: use it. It names items by asset ID, closet items included.
  if (catalog && EditorSavedLast() && ReadEditorManifest(catalog, out)) return true;
  const auto bytes = ReadAll(SavePath());
  if (bytes.size() != av::kDescriptionBytes || !av::ParseAnyDescription(catalog, bytes.data(), bytes.size(), out))
    return false;
  if (!catalog) return true;
  // avatar.bin names closet items by their place in the closet, which moves when
  // the closet changes; the manifest written in the same save names them by ID.
  if (HasClosetItem(catalog, *out)) {
    av::Description fromEditor;
    if (ReadEditorManifest(catalog, &fromEditor)) *out = fromEditor;
    return true;
  }
  // First run with the Avatar Editor: give it this Avatar to start from.
  std::error_code error;
  if (!std::filesystem::exists(EditorManifestPath(), error)) ExportToEditor(catalog, *out);
  return true;
}

bool Save(const av::Description& description) {
  // The editor's copy first, so avatar.bin is the newer file and the next Load
  // reads it (avatar.bin keeps empty slots exactly; a manifest fills them with
  // the console's default clothing).
  ExportToEditor(EditorCatalog(), description);
  const auto bytes = av::SerializeDescription(description);
  return WriteAll(SavePath(), bytes.data(), bytes.size());
}

av::Description Default(const av::Catalog& catalog, uint64_t xuid) {
  // Same seed native_provider.cpp has always used for the local profile.
  std::mt19937 profile(uint32_t(xuid) ^ uint32_t(xuid >> 32));
  return av::RandomDescription(catalog, profile, -1);
}
}  // namespace local_avatar

namespace local_avatar {
long long EditorSaveStamp() {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(EditorManifestPath(), error);
  return error ? 0 : static_cast<long long>(time.time_since_epoch().count());
}
}  // namespace local_avatar

#if defined(_WIN32) && !REX_PLATFORM_WINRT
// The built-in Win32 Avatar editor was replaced by the Avatar Editor
// recompilation on 2026-10-13 (backup: claude\backup\avatar-primary-20261013).
bool OpenAvatarEditor(HWND owner) {
  wchar_t exe[1024] = {};
  if (!GetEnvironmentVariableW(L"ONEVS100_AVATAR_EDITOR_EXE", exe, 1024) || !exe[0] ||
      GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(owner, L"The Avatar Editor is not installed with this copy of the game.",
                L"Avatar Editor", MB_OK | MB_ICONINFORMATION);
    return false;
  }
  const std::filesystem::path program(exe);
  const std::filesystem::path data = local_avatar::EditorManifestPath().parent_path();
  std::error_code error;
  std::filesystem::create_directories(data, error);
  // First use: give the editor the Avatar this player already has (Load
  // exports avatar.bin when the editor has no save yet). A player who never
  // saved one gets the XUID-seeded Avatar the game has been showing.
  if (auto* catalog = local_avatar::EditorCatalog(); catalog && catalog->loaded()) {
    xe::kernel::xna::avatar::Description current;
    if (!local_avatar::Load(catalog, &current))
      local_avatar::Save(local_avatar::Default(*catalog, LocalXuid()));
  }
  std::wstring command = L"\"" + program.wstring() + L"\" --avatar_data_dir=\"" + data.wstring() + L"\"";
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  const std::wstring folder = program.parent_path().wstring();
  if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      folder.c_str(), &startup, &process)) {
    MessageBoxW(owner, L"The Avatar Editor could not be started.", L"Avatar Editor", MB_OK | MB_ICONWARNING);
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}
#endif
