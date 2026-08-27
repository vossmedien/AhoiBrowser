// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_PAGE_TINT_H_
#define AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_PAGE_TINT_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

class SkBitmap;

namespace ahoi::appearance {

// Extracts a representative color from a favicon that is already available in
// memory. Work is bounded by downsampling before color analysis.
std::optional<SkColor> ExtractSidebarPageColorFromFavicon(
    const SkBitmap& favicon);

// Resolves a deliberately subtle overlay from the active page's declared
// theme color, falling back to an already-loaded favicon color. High-contrast
// mode and the user toggle always win.
std::optional<SkColor> ResolveSidebarPageTint(
    bool enabled,
    bool high_contrast,
    std::optional<SkColor> page_theme_color,
    std::optional<SkColor> favicon_color = std::nullopt);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_PAGE_TINT_H_
