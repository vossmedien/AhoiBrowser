// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/scroll_view.h"

namespace ahoi::sidebar {
namespace {

constexpr int kOverlayScrollGap = 8;

}  // namespace

SidebarMediaOverlayView::SidebarMediaOverlayView(
    std::unique_ptr<views::ScrollView> scroll_view,
    std::unique_ptr<views::View> media_overlay,
    views::View* scroll_bottom_inset) {
  CHECK(scroll_view);
  CHECK(media_overlay);
  CHECK(scroll_bottom_inset);
  CHECK(scroll_view->contents());
  CHECK(scroll_bottom_inset->parent() == scroll_view->contents());

  scroll_view_ = AddChildView(std::move(scroll_view));
  media_overlay_ = AddChildView(std::move(media_overlay));
  scroll_bottom_inset_ = scroll_bottom_inset;
}

SidebarMediaOverlayView::~SidebarMediaOverlayView() = default;

int SidebarMediaOverlayView::scroll_bottom_inset_for_testing() const {
  return scroll_bottom_inset_->GetPreferredSize().height();
}

void SidebarMediaOverlayView::Layout(PassKey) {
  const gfx::Rect bounds = GetContentsBounds();
  scroll_view_->SetBoundsRect(bounds);

  int overlay_height = 0;
  if (media_overlay_->GetVisible()) {
    overlay_height =
        std::min(bounds.height(), media_overlay_
                                      ->GetPreferredSize(views::SizeBounds(
                                          bounds.width(), bounds.height()))
                                      .height());
  }
  const bool inset_changed = UpdateScrollBottomInset(overlay_height);
  if (inset_changed) {
    scroll_view_->InvalidateLayout();
  }

  if (overlay_height == 0) {
    media_overlay_->SetBounds(bounds.x(), bounds.bottom(), bounds.width(), 0);
    return;
  }
  media_overlay_->SetBounds(bounds.x(), bounds.bottom() - overlay_height,
                            bounds.width(), overlay_height);
}

gfx::Size SidebarMediaOverlayView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // Overlay height is intentionally excluded: it must never steal viewport
  // height from the tab list. Its footprint is represented inside the scroll
  // contents by `scroll_bottom_inset_` instead.
  return scroll_view_->GetPreferredSize(available_size);
}

void SidebarMediaOverlayView::ChildPreferredSizeChanged(views::View* child) {
  InvalidateLayout();
  PreferredSizeChanged();
}

void SidebarMediaOverlayView::ChildVisibilityChanged(views::View* child) {
  InvalidateLayout();
  PreferredSizeChanged();
}

bool SidebarMediaOverlayView::UpdateScrollBottomInset(int overlay_height) {
  const int target_height =
      overlay_height > 0 ? overlay_height + kOverlayScrollGap : 0;
  if (scroll_bottom_inset_->GetPreferredSize().height() == target_height) {
    return false;
  }
  scroll_bottom_inset_->SetPreferredSize(gfx::Size(0, target_height));
  return true;
}

BEGIN_METADATA(SidebarMediaOverlayView)
END_METADATA

}  // namespace ahoi::sidebar
