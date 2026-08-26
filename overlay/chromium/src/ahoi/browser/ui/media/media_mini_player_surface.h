// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_SURFACE_H_
#define AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_SURFACE_H_

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "ui/color/color_id.h"

namespace ahoi::media_ui {

// Compatibility alias for the component-level seam.  The authoritative role
// lives in the shared appearance policy, so every Ahoi surface can be themed
// by one resolver.
using AhoiAppearanceSurfaceRole = appearance::SurfaceRole;

inline ui::ColorId ColorForSurfaceRole(AhoiAppearanceSurfaceRole role) {
  return appearance::AppearanceResolver::Resolve(role, {}).background_color;
}

inline ui::ColorId BorderColorForSurfaceRole(
    AhoiAppearanceSurfaceRole role) {
  return appearance::AppearanceResolver::Resolve(role, {}).border_color;
}

}  // namespace ahoi::media_ui

#endif  // AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_SURFACE_H_
