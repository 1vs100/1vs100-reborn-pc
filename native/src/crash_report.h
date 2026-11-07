// The previous run's crash report, shown once at launch where no native message box exists (UWP). A pad press
// closes it, since a console has no pointer to click a button with.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <rex/ui/imgui_dialog.h>

namespace crash_report {

// Shows `text` until a pad press closes it. The dialog draws nothing once closed.
std::unique_ptr<rex::ui::ImGuiDialog> Show(rex::ui::ImGuiDrawer* drawer, std::string text);

// Pad polling thread. True when the report takes the buttons; the game then sees an idle pad.
bool FilterPad(uint32_t user, uint16_t buttons);

}  // namespace crash_report
