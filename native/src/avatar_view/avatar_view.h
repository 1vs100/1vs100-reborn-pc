// In-game Avatar view: START+BACK shows the stored Avatar, the stick turns it,
// and an uploaded or copied manifest replaces it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace avatar_view {

// A pad's state before the game reads it. True while the view owns the pads,
// so the caller hands the game an idle pad.
bool FilterPad(uint32_t user, uint16_t buttons, int16_t stick_x);
bool IsOpen();

struct Frame {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
  uint64_t serial = 0;
};
// The latest preview, when it is newer than `serial`.
bool TakeFrame(uint64_t serial, Frame* out);

// What the view shows under the preview: the Avatar's source and the last
// upload result.
std::vector<std::string> StatusLines();

struct StoreResult {
  bool ok = false;
  std::string message;
};
// Replaces the stored Avatar with a manifest (0x3E8 bytes) or description
// (1020 bytes), checked against the asset pack first.
StoreResult Store(const uint8_t* bytes, size_t size);

// The current Avatar facing the viewer, as PNG; empty without an asset pack.
std::vector<uint8_t> PreviewPng();

// Starts the LAN upload page where the platform has one (UWP).
void StartUploadServer();
// How to replace the Avatar: the upload page address, or the file to copy over.
std::string UploadHint();
// The upload page URL for a QR code, or empty where there is no page.
std::string UploadUrl();

}  // namespace avatar_view
