// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_button.h"

#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/i18n/rtl.h"
#include "cc/paint/paint_flags.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_manager.h"

namespace ahoi::sidebar {

SidebarBookmarkButton::SidebarBookmarkButton(PressedCallback callback,
                                             std::u16string text,
                                             const ui::ImageModel& icon,
                                             std::u16string tooltip,
                                             bool folder)
    : views::LabelButton(std::move(callback), text), folder_(folder) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetRequestFocusOnPress(true);
  SetHasInkDropActionOnClick(false);
  SetShowInkDropWhenHotTracked(false);
  SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON |
                           ui::EF_MIDDLE_MOUSE_BUTTON);
  SetTextSubpixelRenderingEnabled(false);
  for (ButtonState state : {STATE_NORMAL, STATE_HOVERED, STATE_PRESSED}) {
    SetTextColor(state, visual_style::kText);
  }
  SetTextColor(STATE_DISABLED, visual_style::kMutedText);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(5);
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(0, visual_style::kBookmarkShelfItemHorizontalInset)));
  if (text.empty() && !folder_) {
    SetPreferredSize(gfx::Size(visual_style::kBookmarkShelfItemSize,
                               visual_style::kBookmarkShelfItemSize));
  } else {
    SetMinSize(gfx::Size(visual_style::kBookmarkShelfFolderMinimumWidth,
                         visual_style::kBookmarkShelfItemSize));
    SetMaxSize(gfx::Size(visual_style::kBookmarkShelfFolderMaximumWidth,
                         visual_style::kBookmarkShelfItemSize));
  }
  if (folder_) {
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
    GetViewAccessibility().SetIsCollapsed();
    GetViewAccessibility().SetRoleDescription(l10n_util::GetStringUTF8(
        IDS_ACCNAME_BOOKMARK_FOLDER_BUTTON_ROLE_DESCRIPTION));
  }
  UpdatePresentation(std::move(text), icon, std::move(tooltip));
}

SidebarBookmarkButton::~SidebarBookmarkButton() = default;

void SidebarBookmarkButton::UpdatePresentation(std::u16string text,
                                               const ui::ImageModel& icon,
                                               std::u16string tooltip) {
  SetText(text);
  for (ButtonState state : {STATE_NORMAL, STATE_HOVERED, STATE_PRESSED}) {
    SetImageModel(state, icon);
  }
  SetAccessibleName(tooltip);
  SetTooltipText(tooltip);
}

void SidebarBookmarkButton::SetMenuOpen(bool open) {
  if (!folder_ || menu_open_ == open) {
    return;
  }
  menu_open_ = open;
  if (open) {
    GetViewAccessibility().SetIsExpanded();
  } else {
    GetViewAccessibility().SetIsCollapsed();
  }
  SchedulePaint();
}

void SidebarBookmarkButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ButtonState state = GetState();
  if (menu_open_ || state == STATE_PRESSED || state == STATE_HOVERED) {
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(GetColorProvider()->GetColor(
        menu_open_ || state == STATE_PRESSED ? visual_style::kSelectedSurface
                                             : visual_style::kHoverSurface));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                          visual_style::kBookmarkShelfCornerRadius, fill);
  }
  if (HasFocus()) {
    cc::PaintFlags focus;
    focus.setAntiAlias(true);
    focus.setStyle(cc::PaintFlags::kStroke_Style);
    focus.setStrokeWidth(2.0f);
    focus.setColor(GetColorProvider()->GetColor(visual_style::kFocusRing));
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(gfx::InsetsF(1.0f));
    canvas->DrawRoundRect(
        bounds, visual_style::kBookmarkShelfCornerRadius - 1.0f, focus);
  }
  views::LabelButton::PaintButtonContents(canvas);
}

void SidebarBookmarkButton::OnFocus() {
  views::LabelButton::OnFocus();
  ScrollRectToVisible(GetLocalBounds());
}

bool SidebarBookmarkButton::OnKeyPressed(const ui::KeyEvent& event) {
  if (!event.IsShiftDown() && !event.IsControlDown() && !event.IsAltDown() &&
      !event.IsCommandDown()) {
    if (folder_ && event.key_code() == ui::VKEY_DOWN) {
      NotifyClick(event);
      return true;
    }
    if (GetFocusManager() && (event.key_code() == ui::VKEY_LEFT ||
                              event.key_code() == ui::VKEY_RIGHT)) {
      GetFocusManager()->AdvanceFocus((event.key_code() == ui::VKEY_LEFT) !=
                                      base::i18n::IsRTL());
      return true;
    }
  }
  return views::LabelButton::OnKeyPressed(event);
}

BEGIN_METADATA(SidebarBookmarkButton)
END_METADATA

}  // namespace ahoi::sidebar
