// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_TITLE_LABEL_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_TITLE_LABEL_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/label.h"

namespace ahoi::sidebar {

// Returns title bounds that are wholly contained by the pane projection. A
// one-DIP edge clearance keeps glyph anti-aliasing away from split separators,
// which can border any side and are painted by a sibling or ancestor.
gfx::Rect GetDividerSafeSidebarTitleBounds(const gfx::Rect& requested_bounds,
                                           const gfx::Rect& pane_bounds,
                                           bool has_split_separator);

// Sidebar titles are painted below split chrome in some projections and above
// it in others. Label elision alone is not a paint clip, so this label applies
// an explicit local canvas clip in addition to ELIDE_TAIL. Both saved and live
// tab rows use this type to keep hover/drag state from crossing a pane divider.
class SidebarTabTitleLabel final : public views::Label {
  METADATA_HEADER(SidebarTabTitleLabel, views::Label)

 public:
  SidebarTabTitleLabel();
  SidebarTabTitleLabel(const SidebarTabTitleLabel&) = delete;
  SidebarTabTitleLabel& operator=(const SidebarTabTitleLabel&) = delete;
  ~SidebarTabTitleLabel() override;

  void SetDividerSafeBounds(const gfx::Rect& requested_bounds,
                            const gfx::Rect& pane_bounds,
                            bool has_split_separator);

  const gfx::Rect& paint_clip_bounds_for_testing() const {
    return paint_clip_bounds_;
  }

  // views::Label:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  gfx::Rect paint_clip_bounds_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_TITLE_LABEL_H_
