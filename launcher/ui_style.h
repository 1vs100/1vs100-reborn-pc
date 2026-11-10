// The launcher's look: the 1vs100.fans palette, fonts, the ImGui style and
// the widgets drawn by hand.
#pragma once

#include <imgui.h>

#include <cstdint>

struct Fonts {
  ImFont* body;
  ImFont* title;
};

// Palette of 1vs100.fans and docs.1vs100.fans (gameserver/site/src/styles.css, dark scheme).
constexpr ImVec4 Hex(uint32_t rgb, float alpha = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}
inline constexpr ImVec4 kBackground = Hex(0x0d0b1f);
inline constexpr ImVec4 kSurface = Hex(0x17142e);
inline constexpr ImVec4 kSurface2 = Hex(0x211d3d);
inline constexpr ImVec4 kSurface2Hover = Hex(0x2c2752);
inline constexpr ImVec4 kText = Hex(0xf3f1ff);
inline constexpr ImVec4 kText3 = Hex(0xf3f1ff, 0.52f);
inline constexpr ImVec4 kAccent = Hex(0xb28cff);
inline constexpr ImVec4 kError = Hex(0xff2d55);
inline constexpr ImVec4 kCaution = Hex(0xffb020);

void ApplyStyle(float scale);
Fonts LoadFonts(float scale);

// Draws the show gradient across a rectangle.
void ShowGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha = 1.0f);

// The Launch button. The gradient marks it ready; a disabled one is flat gray so the two never look alike.
bool ShowButton(const char* label, ImVec2 size, bool enabled);
// Opens url when clicked.
bool LinkText(const char* label, const char* url);
// Small dimmed text above a group of controls.
void SectionLabel(const char* text);
// A turning arc while something loads.
void Spinner(ImVec2 center);

// Green: as expected. Yellow: present by name, but not what the manifest or the game expects. Red: missing.
enum class Indicator { Ok, Caution, Missing };

void StatusIcon(Indicator indicator);
