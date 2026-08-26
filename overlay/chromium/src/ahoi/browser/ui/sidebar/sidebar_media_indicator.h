// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_INDICATOR_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_INDICATOR_H_

#include <optional>

#include "components/tabs/public/tab_alert.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/rect.h"

namespace ahoi::sidebar {

// Returns the compact, theme-aware media or capture glyph used by both
// persistent and temporary sidebar rows. Camera, microphone, and sharing
// states are projected directly from Chromium's authoritative TabAlert state.
// Unrelated device and automation alerts intentionally return an empty image.
ui::ImageModel GetSidebarMediaIndicator(
    std::optional<tabs::TabAlert> alert_state);

// Maps MediaSession-derived state. Unlike WebContents audible state, this can
// keep a muted-but-still-playing tab visibly muted after the recently-audible
// grace period expires.
std::optional<tabs::TabAlert> GetSidebarMediaAlertForSession(
    bool playing,
    bool muted,
    bool picture_in_picture,
    bool relevant);

struct SidebarTabTrailingLayout {
  gfx::Rect title;
  gfx::Rect media_indicator;
  gfx::Rect hover_action;
};

// Keeps the media state and hover action in independent trailing slots. This
// is shared by ordinary rows and each pane-cell row in a split.
SidebarTabTrailingLayout GetSidebarTabTrailingLayout(int width,
                                                     int height,
                                                     bool has_media_indicator);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_INDICATOR_H_
