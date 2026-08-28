// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_title_label.h"

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ahoi::sidebar {

gfx::Rect GetDividerSafeSidebarTitleBounds(const gfx::Rect& requested_bounds,
                                           const gfx::Rect& pane_bounds,
                                           bool has_split_separator) {
  gfx::Rect clip_bounds = pane_bounds;
  if (has_split_separator && !clip_bounds.IsEmpty()) {
    // A pane can sit on either side of a vertical or horizontal separator.
    // Leave a full DIP on every edge for anti-aliased glyph pixels.
    clip_bounds.Inset(gfx::Insets(1));
  }
  gfx::Rect result = requested_bounds;
  result.Intersect(clip_bounds);
  return result;
}

SidebarTabTitleLabel::SidebarTabTitleLabel() {
  SetSubpixelRenderingEnabled(false);
  SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  SetElideBehavior(gfx::ELIDE_TAIL);
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
}

SidebarTabTitleLabel::~SidebarTabTitleLabel() = default;

void SidebarTabTitleLabel::SetDividerSafeBounds(
    const gfx::Rect& requested_bounds,
    const gfx::Rect& pane_bounds,
    bool has_split_separator) {
  SetBoundsRect(GetDividerSafeSidebarTitleBounds(requested_bounds, pane_bounds,
                                                 has_split_separator));
  paint_clip_bounds_ = GetLocalBounds();
}

void SidebarTabTitleLabel::OnPaint(gfx::Canvas* canvas) {
  // Child-label paint ordering differs between saved and runtime split
  // projections. Clip here, at the final text-paint boundary, so an elided
  // glyph can never overpaint a separator regardless of parent ordering.
  gfx::ScopedCanvas scoped_canvas(canvas);
  canvas->ClipRect(paint_clip_bounds_);
  views::Label::OnPaint(canvas);
}

BEGIN_METADATA(SidebarTabTitleLabel)
END_METADATA

}  // namespace ahoi::sidebar
