// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "components/split_tabs/split_tab_visual_data.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace ahoi::sidebar {

// Returns the compact sidebar bounds for one pane of a Chromium split.
// Two-, three- and four-pane groups mirror the WebContents arrangement. Four
// panes use the same row-major 2x2 order as MultiContentsView.
gfx::Rect GetSplitSegmentBounds(
    const gfx::Rect& bounds,
    size_t segment,
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data);

// Returns a readable row height for any sidebar split representation. Layouts
// with panes on multiple visual rows grow just enough to keep every segment
// usable instead of squeezing a full tab row into a few pixels.
int GetSplitRowPreferredHeight(
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data,
    int standard_row_height);

struct SidebarSplitSeparator {
  gfx::PointF start;
  gfx::PointF end;

  bool operator==(const SidebarSplitSeparator&) const = default;
};

// One interactive ratio boundary in the compact sidebar projection. A single
// Chromium ratio can be represented by more than one painted separator (for
// example the aligned horizontal edges in a 2x2 split); those pieces are
// intentionally merged into one generous pointer target here.
struct SidebarSplitDivider {
  size_t divider_index = 0;
  gfx::PointF start;
  gfx::PointF end;
  double ratio = 0.5;
  int ratio_extent = 0;
  bool reverse_ratio_direction = false;

  bool resizes_horizontally() const { return start.x() == end.x(); }

  bool operator==(const SidebarSplitDivider&) const = default;
};

// Returns separators only for panes that are geometrically adjacent. Every
// endpoint is clamped to `paint_bounds`, which is the inset area actually
// painted for the split group's background.
std::vector<SidebarSplitSeparator> GetSidebarSplitSeparators(
    const std::vector<gfx::Rect>& segment_bounds,
    const gfx::RectF& paint_bounds);

// Returns the logical primary/secondary ratio controls that correspond to
// the exact segment projection above. Divider indices intentionally match
// TabStripModel::UpdateSplitRatio(), so a sidebar gesture can update the same
// Chromium-owned state as the WebContents divider without a parallel model.
std::vector<SidebarSplitDivider> GetSidebarSplitDividers(
    const gfx::Rect& group_bounds,
    const std::vector<gfx::Rect>& segment_bounds,
    const gfx::RectF& paint_bounds,
    const split_tabs::SplitTabVisualData& visual_data);

// The visible separator remains one quiet DIP while its native pointer target
// is deliberately wider. This is the only geometry used for hit testing, so
// click/drag ownership does not depend on aiming at a hairline.
gfx::Rect GetSidebarSplitDividerHitBounds(const SidebarSplitDivider& divider);

// Returns the stable, full-surface target used for before/after tab drops.
// Hit testing and paint both use the same 30% edge geometry; unlike a moving
// insertion line the returned surface remains easy to aim at during a native
// drag. `trailing_edge` means after/bottom in the vertical sidebar list.
int GetSidebarEdgeDropTargetExtent(int row_height);
gfx::RectF GetSidebarEdgeDropTargetBounds(const gfx::Rect& row_bounds,
                                          bool trailing_edge);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_LAYOUT_H_
