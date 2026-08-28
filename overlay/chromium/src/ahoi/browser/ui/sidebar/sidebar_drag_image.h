// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DRAG_IMAGE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DRAG_IMAGE_H_

#include <string>
#include <vector>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"

namespace ui {
class ColorProvider;
}

namespace views {
class Widget;
}

namespace ahoi::sidebar {

// Builds the shared unpadded preview card used by the passive hover bubble.
// Drag and hover deliberately share the same title/favicon/split-thumbnail
// renderer so the interaction never changes visual language mid-gesture.
gfx::ImageSkia CreateSidebarPreviewImage(
    views::Widget* source_widget,
    const ui::ColorProvider* color_provider,
    const gfx::ImageSkia& favicon,
    const std::u16string& title,
    const std::vector<gfx::ImageSkia>& cached_thumbnails);

// Composites a Retina-aware native drag card from thumbnails that were cached
// before drag initiation. No capture, decode, source-view resize or layout is
// performed synchronously by this function.
gfx::ImageSkia CreateSidebarDragImage(
    views::Widget* source_widget,
    const ui::ColorProvider* color_provider,
    const gfx::ImageSkia& favicon,
    const std::u16string& title,
    const std::vector<gfx::ImageSkia>& cached_thumbnails);

// Places the compact visible card to the right of the cursor so it can extend
// over WebContents without covering narrow sidebar drop targets. Every native
// drag client, including Chromium's macOS bridge, consumes this hotspot.
gfx::Vector2d GetSidebarDragImageCursorOffset(const gfx::ImageSkia& image,
                                              const gfx::Point& press_point);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DRAG_IMAGE_H_
