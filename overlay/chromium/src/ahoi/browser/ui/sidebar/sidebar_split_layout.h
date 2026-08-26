// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_

#include <cstddef>

#include "components/split_tabs/split_tab_visual_data.h"
#include "ui/gfx/geometry/rect.h"

namespace ahoi::sidebar {

// Returns the compact sidebar bounds for one pane of a Chromium split.
// Two-, three- and four-pane groups mirror the WebContents arrangement. Four
// panes use the same row-major 2x2 order as MultiContentsView.
gfx::Rect GetSplitSegmentBounds(
    const gfx::Rect& bounds,
    size_t segment,
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data);

// Returns a readable row height for a runtime split representation. Layouts
// with panes on multiple visual rows grow just enough to keep every segment
// usable instead of squeezing a full tab row into a few pixels.
int GetSplitRowPreferredHeight(
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data,
    int standard_row_height);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_
