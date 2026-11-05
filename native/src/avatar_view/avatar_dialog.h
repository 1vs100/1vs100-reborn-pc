#pragma once
#include <memory>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include "avatar_view.h"

namespace avatar_view {

// Draws the view while it is open; idle otherwise.
class AvatarDialog : public rex::ui::ImGuiDialog {
 public:
  AvatarDialog(rex::ui::ImGuiDrawer* imgui_drawer, rex::ui::ImmediateDrawer* immediate_drawer)
      : ImGuiDialog(imgui_drawer), immediate_drawer_(immediate_drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  rex::ui::ImmediateDrawer* immediate_drawer_;
  std::unique_ptr<rex::ui::ImmediateTexture> texture_;
  uint64_t serial_ = 0;
};

}  // namespace avatar_view
