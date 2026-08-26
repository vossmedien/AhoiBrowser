// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_LAYOUT_H_
#define AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_LAYOUT_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace ahoi::popup_ui {

struct PopupOverlayLayout {
  gfx::Rect card_bounds;
  gfx::Rect action_rail_bounds;
};

// Computes a responsive card and external action rail inside one originating
// WebContents pane. The requested card size may come from window.open feature
// hints; it is always clamped to the browser-owned safe area.
PopupOverlayLayout CalculatePopupOverlayLayout(
    const gfx::Rect& available_bounds,
    const gfx::Size& requested_card_size);

}  // namespace ahoi::popup_ui

#endif  // AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_LAYOUT_H_
