#include "avatar_editor.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <system_error>
#include <utility>

#include "files.h"

namespace av = xe::kernel::xna::avatar;
namespace fs = std::filesystem;

namespace {

constexpr const char* kSonaGamertagUrl = "https://360sona.com/sona/v1/gamertag/";

bool FitsAvatar(size_t size) { return size == av::kManifestBytes || size == av::kDescriptionBytes; }

AvatarEditor::Message Info(std::string text) { return {AvatarEditor::Message::Severity::Info, std::move(text)}; }
AvatarEditor::Message Failure(std::string text) { return {AvatarEditor::Message::Severity::Error, std::move(text)}; }

}  // namespace

std::optional<XboxGamertag> XboxGamertag::Parse(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  if (text.empty()) return std::nullopt;
  return XboxGamertag(std::string(text));
}

std::string SonaManifestUrl(const XboxGamertag& gamertag) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string url = kSonaGamertagUrl;
  for (unsigned char c : gamertag.text()) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      url += static_cast<char>(c);
    } else {
      url += '%';
      url += kHex[c >> 4];
      url += kHex[c & 0xF];
    }
  }
  return url + "?client=1vs100rb&format=bin";
}

void AvatarEditor::Init(fs::path avatar_file, SDL_Renderer* renderer) {
  avatar_file_ = std::move(avatar_file);
  renderer_ = renderer;
}

void AvatarEditor::ReleasePreview() {
  if (preview_) SDL_DestroyTexture(preview_);
  preview_ = nullptr;
  renderer_ = nullptr;
}

void AvatarEditor::StartCatalog(const std::vector<fs::path>& packs) {
  if (catalog_ || catalog_failed_ || catalog_load_.valid() || packs.empty()) return;
  catalog_load_ = std::async(std::launch::async, [packs] { return av::LoadPackCatalog(packs); });
}

void AvatarEditor::PacksChanged() {
  if (!catalog_) catalog_failed_ = false;
}

std::optional<AvatarEditor::Message> AvatarEditor::PollCatalog() {
  if (!catalog_load_.valid() || catalog_load_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return std::nullopt;
  try {
    catalog_ = catalog_load_.get();
  } catch (const std::exception&) {
    catalog_.reset();
  }
  if (!catalog_ || !catalog_->loaded()) {
    catalog_.reset();
    catalog_failed_ = true;
    return Failure("The avatar pack table did not load. See the Files tab.");
  }
  std::optional<Message> message = LoadStored();
  // A code pasted while the pack was loading.
  if (code[0]) {
    if (auto imported = ImportCode()) message = std::move(imported);
  }
  return message;
}

std::optional<AvatarEditor::Message> AvatarEditor::ImportCode() {
  const std::string_view text(code.data());
  const auto decoded = av::DecodeAvatarText(text);
  if (decoded && FitsAvatar(decoded->size())) return Import(*decoded);
  if (!text.empty()) return Info("That is not a complete avatar code yet.");
  return std::nullopt;
}

void AvatarEditor::Show(const av::Description& description, bool unsaved) {
  avatar_ = description;
  avatar_unsaved_ = unsaved;
  scene_ = av::BuildScene(*catalog_, description);
  preview_dirty_ = true;
}

std::optional<AvatarEditor::Message> AvatarEditor::LoadStored() {
  avatar_.reset();
  avatar_unsaved_ = false;
  std::error_code ec;
  if (!fs::is_regular_file(avatar_file_, ec)) return std::nullopt;
  auto bytes = ReadBytes(avatar_file_);
  av::Description description;
  if (!bytes || !av::ParseAnyDescription(catalog_.get(), bytes->data(), bytes->size(), &description)) {
    return Failure("The saved avatar cannot be read. Open or paste a new one.");
  }
  Show(description, false);
  return std::nullopt;
}

std::optional<AvatarEditor::Message> AvatarEditor::Import(std::vector<uint8_t> bytes) {
  if (!catalog_) return Failure("The avatar pack table is not loaded yet. See the Files tab.");
  if (!FitsAvatar(bytes.size())) {
    auto decoded = av::DecodeAvatarText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!decoded) return Failure("That is neither an avatar file nor an avatar code.");
    bytes = std::move(*decoded);
  }
  av::Description description;
  if (!FitsAvatar(bytes.size()) || !av::ParseAnyDescription(catalog_.get(), bytes.data(), bytes.size(), &description))
    return Failure("That is not an avatar.");
  // Marketplace items this PC lacks become similar standard items, as in the game.
  const size_t similar = av::SubstituteMissingItems(*catalog_, &description);
  Show(description, true);
  return Info(similar ? std::to_string(similar) +
                            " item(s) you do not have became similar standard items. Launch saves this avatar."
                      : std::string("Launch saves this avatar."));
}

std::expected<void, Error> AvatarEditor::Save() {
  if (!avatar_ || !avatar_unsaved_) return {};
  const auto bytes = av::SerializeDescription(*avatar_);
  if (auto written = WriteBytes(avatar_file_, bytes.data(), bytes.size()); !written) return written;
  avatar_unsaved_ = false;
  return {};
}

bool AvatarEditor::HasAvatar() const {
  std::error_code ec;
  return avatar_.has_value() || fs::is_regular_file(avatar_file_, ec);
}

std::optional<AvatarEditor::Message> AvatarEditor::Export(const fs::path& target) {
  if (!avatar_) return std::nullopt;
  // The manifest does not record the owner, so no profile id is needed.
  const auto bytes = av::SerializeManifest(catalog_.get(), *avatar_, 0);
  if (auto written = WriteBytes(target, bytes.data(), bytes.size()); !written) {
    return Failure(Describe(written.error()));
  }
  return Info("Exported to " + PathText(target) + ".");
}

void AvatarEditor::Turn(float radians) {
  yaw_ += radians;
  preview_dirty_ = true;
}

SDL_Texture* AvatarEditor::Preview() {
  if (!avatar_) return nullptr;
  if (preview_dirty_ && renderer_) {
    preview_dirty_ = false;
    if (!preview_) {
      preview_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kPreviewWidth,
                                   kPreviewHeight);
    }
    if (preview_) {
      av::RenderIdle(*catalog_, scene_, kPreviewWidth, kPreviewHeight, yaw_, &preview_pixels_);
      SDL_UpdateTexture(preview_, nullptr, preview_pixels_.data(), kPreviewWidth * 4);
    }
  }
  return preview_;
}
