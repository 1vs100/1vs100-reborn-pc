#include "avatar_dialog.h"

#include <algorithm>

#include <imgui.h>
#include <rex/platform.h>

#if REX_PLATFORM_WINRT
#include <cstddef>

#include <onboarding/qr.h>
#endif

namespace avatar_view {
namespace {

#if REX_PLATFORM_WINRT
void DrawQr(const std::string& text, float side) {
  const onboarding::QrMatrix qr = onboarding::EncodeQr(text);
  if (qr.modules > 0) {
    const int modules = qr.modules + 4;  // two-module quiet zone
    const float cell = side / float(modules);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32_WHITE);
    for (int y = 0; y < qr.modules; ++y) {
      for (int x = 0; x < qr.modules; ++x) {
        if (!qr.bits[static_cast<std::size_t>(y) * qr.modules + x]) continue;
        const ImVec2 a(origin.x + (x + 2) * cell, origin.y + (y + 2) * cell);
        list->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), IM_COL32_BLACK);
      }
    }
  }
  ImGui::Dummy(ImVec2(side, side));
}
#endif

}  // namespace

void AvatarDialog::OnDraw(ImGuiIO& io) {
  if (!IsOpen()) {
    texture_.reset();
    serial_ = 0;
    return;
  }
  Frame frame;
  if (immediate_drawer_ && TakeFrame(serial_, &frame)) {
    serial_ = frame.serial;
    texture_ = immediate_drawer_->CreateTexture(frame.width, frame.height, rex::ui::ImmediateTextureFilter::kLinear,
                                                false, frame.rgba.data());
  }
  const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::Begin("Avatar", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  ImGui::SetWindowFontScale(scale);
  const ImVec2 image(384.0f * scale * 0.75f, 512.0f * scale * 0.75f);
  if (texture_) {
    ImGui::Image(reinterpret_cast<ImTextureID>(texture_.get()), image);
  } else {
    ImGui::Dummy(image);
  }
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300.0f * scale);
  for (const auto& line : StatusLines()) ImGui::TextUnformatted(line.c_str());
  ImGui::Spacing();
  ImGui::TextUnformatted(UploadHint().c_str());
#if REX_PLATFORM_WINRT
  if (const std::string url = UploadUrl(); !url.empty()) DrawQr(url, 160.0f * scale);
#endif
  ImGui::Spacing();
  ImGui::TextUnformatted("A new avatar shows the next time the game loads yours.");
  ImGui::TextDisabled("Left stick: turn    B: close");
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::End();
}

}  // namespace avatar_view
