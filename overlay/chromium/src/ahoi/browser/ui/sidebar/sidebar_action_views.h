// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_ACTION_VIEWS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_ACTION_VIEWS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
struct VectorIcon;
}

namespace views {
class View;
}

namespace ahoi::sidebar {

struct WorkspaceSelectorIndicator {
  size_t workspace_index = 0;
  std::u16string name;
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
};

// Small, reusable sidebar controls. Concrete View types stay private to keep
// BrowserSidebarHostView coupled only to native Views primitives.
std::unique_ptr<views::View> CreatePageFallbackIconView(bool active,
                                                        bool new_tab);
std::unique_ptr<views::View> CreateTabCloseIconView(bool active);

std::unique_ptr<views::Button> CreateWorkspaceSelectorButton(
    views::Button::PressedCallback callback);
void SetWorkspaceSelectorPresentation(views::Button* button,
                                      const std::u16string& name,
                                      const std::u16string& icon,
                                      std::optional<uint32_t> accent_argb);
void SetWorkspaceSelectorIndicators(
    views::Button* button,
    std::vector<WorkspaceSelectorIndicator> indicators,
    base::RepeatingCallback<void(size_t)> activate_callback);

std::unique_ptr<views::View> CreateSidebarActionButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name);

// Compact header control used for persistent sidebar presentation actions.
// It shares the footer's focus, hover, theme and accessibility behavior while
// fitting beside the workspace selector.
std::unique_ptr<views::View> CreateSidebarHeaderActionButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name);

std::unique_ptr<views::View> CreateSidebarSplitActionCell(
    views::Button::PressedCallback top_callback,
    const gfx::VectorIcon& top_icon,
    std::u16string top_name,
    views::Button::PressedCallback bottom_callback,
    const gfx::VectorIcon& bottom_icon,
    std::u16string bottom_name);

std::unique_ptr<views::Button> CreateGroupColorSwatchButton(
    views::Button::PressedCallback callback,
    std::optional<uint32_t> color,
    std::u16string accessible_name);
void SetGroupColorSwatchSelected(views::Button* button, bool selected);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_ACTION_VIEWS_H_
