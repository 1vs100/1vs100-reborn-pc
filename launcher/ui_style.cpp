#include "ui_style.h"

#include <SDL3/SDL_misc.h>

#include <array>
#include <cstddef>
#include <utility>

#include "font_data.h"

namespace {

// The "show" gradient: blue, violet at 40%, magenta at 70%, red.
constexpr std::array<std::pair<float, uint32_t>, 4> kShowStops = {
    {{0.0f, 0x3d6bff}, {0.4f, 0x8b3dff}, {0.7f, 0xe23fd2}, {1.0f, 0xff2d55}}};

}  // namespace

void ShowGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha) {
  const float width = max.x - min.x;
  for (size_t i = 0; i + 1 < kShowStops.size(); ++i) {
    const ImU32 left = ImGui::GetColorU32(Hex(kShowStops[i].second, alpha));
    const ImU32 right = ImGui::GetColorU32(Hex(kShowStops[i + 1].second, alpha));
    draw->AddRectFilledMultiColor(ImVec2(min.x + width * kShowStops[i].first, min.y),
                                  ImVec2(min.x + width * kShowStops[i + 1].first, max.y), left, right, right, left);
  }
}

void ApplyStyle(float scale) {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(24, 20);
  style.FramePadding = ImVec2(12, 8);
  style.ItemSpacing = ImVec2(10, 10);
  style.FrameRounding = 8;
  style.TabRounding = 8;
  style.GrabRounding = 8;
  style.ChildRounding = 12;
  style.WindowBorderSize = 0;
  style.FrameBorderSize = 0;
  style.ChildBorderSize = 1;
  style.TabBarBorderSize = 0;
  style.ScaleAllSizes(scale);

  ImVec4* c = style.Colors;
  c[ImGuiCol_WindowBg] = kBackground;
  c[ImGuiCol_ChildBg] = kSurface;
  c[ImGuiCol_PopupBg] = kSurface;
  c[ImGuiCol_Border] = Hex(0xffffff, 0.08f);
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kText3;
  c[ImGuiCol_FrameBg] = kSurface2;
  c[ImGuiCol_FrameBgHovered] = kSurface2Hover;
  c[ImGuiCol_FrameBgActive] = kSurface2Hover;
  c[ImGuiCol_Button] = kSurface2;
  c[ImGuiCol_ButtonHovered] = kSurface2Hover;
  c[ImGuiCol_ButtonActive] = kSurface2Hover;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_Tab] = kBackground;
  c[ImGuiCol_TabHovered] = kSurface2;
  c[ImGuiCol_TabSelected] = kSurface;
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TabDimmedSelected] = kSurface;
  c[ImGuiCol_TabDimmedSelectedOverline] = kAccent;
  c[ImGuiCol_TextSelectedBg] = Hex(0xb28cff, 0.35f);
  c[ImGuiCol_NavHighlight] = kAccent;
  c[ImGuiCol_ScrollbarBg] = kSurface;
  c[ImGuiCol_ScrollbarGrab] = kSurface2;
}

Fonts LoadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  auto* data = const_cast<unsigned char*>(kFontData);
  const int size = static_cast<int>(sizeof(kFontData));
  Fonts fonts{};
  fonts.body = io.Fonts->AddFontFromMemoryTTF(data, size, 18.0f * scale, &config);
  fonts.title = io.Fonts->AddFontFromMemoryTTF(data, size, 36.0f * scale, &config);
  return fonts;
}

bool ShowButton(const char* label, ImVec2 size, bool enabled) {
  const bool pressed = ImGui::InvisibleButton(label, size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  // AddRectFilledMultiColor has no rounding, so clip the gradient to the
  // button rect instead of overpainting the surroundings.
  if (!enabled) {
    draw->AddRectFilled(min, max, ImGui::GetColorU32(kSurface2), ImGui::GetStyle().FrameRounding);
  } else {
    ImGui::PushClipRect(min, max, true);
    ShowGradient(draw, min, max, ImGui::IsItemActive() ? 0.8f : 1.0f);
    if (ImGui::IsItemHovered()) draw->AddRectFilled(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.12f)));
    ImGui::PopClipRect();
  }
  draw->AddRect(min, max, ImGui::GetColorU32(Hex(0xffffff, 0.14f)), ImGui::GetStyle().FrameRounding);
  const ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
  draw->AddText(ImVec2(min.x + (size.x - text.x) * 0.5f, min.y + (max.y - min.y - text.y) * 0.5f),
                ImGui::GetColorU32(enabled ? kText : kText3), label);
  return pressed;
}

bool LinkText(const char* label, const char* url) {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsItemClicked()) {
    SDL_OpenURL(url);
    return true;
  }
  return false;
}

void SectionLabel(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

void Spinner(ImVec2 center) {
  const float radius = ImGui::GetFontSize();
  const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PathArcTo(center, radius, start, start + 4.5f, 24);
  draw->PathStroke(ImGui::GetColorU32(kAccent), 0, radius * 0.2f);
}

void StatusIcon(Indicator indicator) {
  const bool ok = indicator == Indicator::Ok;
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 center(at.x + size * 0.5f, at.y + size * 0.5f);
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 ring = ImGui::GetColorU32(ok                                ? ImVec4(0.20f, 0.78f, 0.35f, 1.0f)
                                        : indicator == Indicator::Caution ? kCaution
                                                                          : kError);
  const ImU32 mark = ImGui::GetColorU32(Hex(0xffffff));
  draw->AddCircle(center, size * 0.5f, ring, 20, size * 0.12f);
  if (indicator == Indicator::Caution) {
    draw->AddLine(ImVec2(center.x, center.y - size * 0.24f), ImVec2(center.x, center.y + size * 0.06f), mark,
                  size * 0.11f);
    draw->AddCircleFilled(ImVec2(center.x, center.y + size * 0.22f), size * 0.06f, mark);
  } else if (ok) {
    const ImVec2 p1(center.x - size * 0.20f, center.y);
    const ImVec2 p2(center.x - size * 0.02f, center.y + size * 0.18f);
    const ImVec2 p3(center.x + size * 0.24f, center.y - size * 0.18f);
    draw->AddLine(p1, p2, mark, size * 0.10f);
    draw->AddLine(p2, p3, mark, size * 0.10f);
  } else {
    const float d = size * 0.18f;
    draw->AddLine(ImVec2(center.x - d, center.y - d), ImVec2(center.x + d, center.y + d), mark,
                  size * 0.10f);
    draw->AddLine(ImVec2(center.x - d, center.y + d), ImVec2(center.x + d, center.y - d), mark,
                  size * 0.10f);
  }
}
