// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_BUTTON_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_BUTTON_H_

#include "ui/views/controls/button/label_button.h"

namespace ahoi::sidebar {

// Shared, flat presentation for bookmark URLs, folders and the manager action.
// Identity and navigation remain in the shelf; this view owns only appearance,
// keyboard affordances and keeping the focused button inside the viewport.
class SidebarBookmarkButton final : public views::LabelButton {
  METADATA_HEADER(SidebarBookmarkButton, views::LabelButton)

 public:
  SidebarBookmarkButton(PressedCallback callback,
                        std::u16string text,
                        const ui::ImageModel& icon,
                        std::u16string tooltip,
                        bool folder);
  SidebarBookmarkButton(const SidebarBookmarkButton&) = delete;
  SidebarBookmarkButton& operator=(const SidebarBookmarkButton&) = delete;
  ~SidebarBookmarkButton() override;

  void UpdatePresentation(std::u16string text,
                          const ui::ImageModel& icon,
                          std::u16string tooltip);
  void SetMenuOpen(bool open);
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnFocus() override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

 private:
  const bool folder_;
  bool menu_open_ = false;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_BUTTON_H_
