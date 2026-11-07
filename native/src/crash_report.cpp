#include "crash_report.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

#include <imgui.h>

namespace crash_report {
namespace {

constexpr uint16_t kButtonA = 0x1000, kButtonB = 0x2000;
constexpr uint16_t kClose = kButtonA | kButtonB;

std::atomic<bool> g_open{false};
std::string g_text;  // written before g_open is set, read only while it is
std::mutex g_pad_mutex;

class Dialog final : public rex::ui::ImGuiDialog {
 public:
  explicit Dialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_open.load(std::memory_order_acquire)) return;
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::Begin("1 vs 100", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted("The game stopped with an error the last time it ran.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(600.0f * scale);
    ImGui::TextUnformatted(g_text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::TextDisabled("A or B: close");
    ImGui::End();
  }
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> Show(rex::ui::ImGuiDrawer* drawer, std::string text) {
  g_text = std::move(text);
  g_open.store(true, std::memory_order_release);
  return std::make_unique<Dialog>(drawer);
}

bool FilterPad(uint32_t user, uint16_t buttons) {
  if (user >= 4) return false;
  std::lock_guard<std::mutex> lock(g_pad_mutex);
  static uint16_t prior[4] = {};
  static bool held_after_close[4] = {};
  const bool pressed = (buttons & kClose) && !(prior[user] & kClose);
  prior[user] = buttons;
  if (g_open.load(std::memory_order_acquire)) {
    if (pressed) {
      g_open.store(false, std::memory_order_release);
      for (bool& held : held_after_close) held = true;
    }
    return true;
  }
  // The press that closed the report stays away from the game until released.
  if (held_after_close[user] && (buttons & kClose)) return true;
  held_after_close[user] = false;
  return false;
}

}  // namespace crash_report
