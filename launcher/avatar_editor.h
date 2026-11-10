// The Avatar tab's model: the pack catalog, loaded off the UI thread, the
// avatar shown and saved, its preview texture, and the 360sona links.
#pragma once

#include <SDL3/SDL_render.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "player_files.h"
#include "xna_avatar_format.h"

inline constexpr uint32_t kPreviewWidth = 240;
inline constexpr uint32_t kPreviewHeight = 320;

// An Xbox gamertag as typed, without surrounding spaces.
class XboxGamertag {
 public:
  static std::optional<XboxGamertag> Parse(std::string_view text);
  const std::string& text() const { return text_; }

 private:
  explicit XboxGamertag(std::string text) : text_(std::move(text)) {}
  std::string text_;
};

// The avatar manifest for a gamertag, as a .bin download.
std::string SonaManifestUrl(const XboxGamertag& gamertag);

class AvatarEditor {
 public:
  struct Message {
    enum class Severity { Info, Error } severity;
    std::string text;
  };

  void Init(std::filesystem::path avatar_file, SDL_Renderer* renderer);
  // The texture belongs to the renderer, so this runs before the renderer goes.
  void ReleasePreview();

  bool Loading() const { return catalog_load_.valid(); }
  bool Ready() const { return catalog_ != nullptr; }
  // The pack table is large, so it loads off the UI thread the first time the
  // Avatar tab opens. Nothing happens without packs or after a failed load.
  void StartCatalog(const std::vector<std::filesystem::path>& packs);
  // A load that failed may work with the packs there are now.
  void PacksChanged();
  std::optional<Message> PollCatalog();

  // Imports the sona code in the code buffer.
  std::optional<Message> ImportCode();
  // A manifest or description file, or its text form.
  std::optional<Message> Import(std::vector<uint8_t> bytes);
  // Nothing when no avatar is shown.
  std::optional<Message> Export(const std::filesystem::path& target);
  // Writes the avatar file when the shown avatar is not saved yet.
  std::expected<void, Error> Save();

  bool Shown() const { return avatar_.has_value(); }
  bool Unsaved() const { return avatar_unsaved_; }
  // The game needs one to start: shown, or saved earlier.
  bool HasAvatar() const;

  void Turn(float radians);
  // Rendered again when the avatar or the angle changed. Null without an avatar.
  SDL_Texture* Preview();

  // The text box of the tab. Larger than any code, so overlong input reaches
  // parsing instead of being cut off by ImGui.
  std::array<char, 8192> code{};

 private:
  void Show(const xe::kernel::xna::avatar::Description& description, bool unsaved);
  std::optional<Message> LoadStored();

  std::filesystem::path avatar_file_;
  SDL_Renderer* renderer_ = nullptr;
  std::future<std::unique_ptr<xe::kernel::xna::avatar::Catalog>> catalog_load_;
  std::unique_ptr<xe::kernel::xna::avatar::Catalog> catalog_;
  bool catalog_failed_ = false;
  std::optional<xe::kernel::xna::avatar::Description> avatar_;
  bool avatar_unsaved_ = false;
  xe::kernel::xna::avatar::Scene scene_;
  float yaw_ = 0.0f;
  bool preview_dirty_ = false;
  SDL_Texture* preview_ = nullptr;
  std::vector<uint8_t> preview_pixels_;
};
