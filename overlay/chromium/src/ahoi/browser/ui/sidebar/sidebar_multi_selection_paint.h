// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MULTI_SELECTION_PAINT_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MULTI_SELECTION_PAINT_H_

namespace gfx {
class Canvas;
class Rect;
}
namespace ui {
class ColorProvider;
}

namespace ahoi::sidebar {
// Both saved and runtime rows use the same outline and optional check badge.
void PaintSidebarMultiSelection(gfx::Canvas* canvas,
                                const ui::ColorProvider* colors,
                                const gfx::Rect& bounds,
                                const gfx::Rect& badge);
}

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MULTI_SELECTION_PAINT_H_
