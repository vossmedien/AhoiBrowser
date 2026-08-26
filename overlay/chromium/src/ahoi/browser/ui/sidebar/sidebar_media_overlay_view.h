// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_OVERLAY_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_OVERLAY_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class ScrollView;
}

namespace ahoi::sidebar {

// Keeps the sidebar's scroll viewport full-height while painting the media
// controls above its lower edge. `scroll_bottom_inset` belongs to the scroll
// contents and is resized to the exact obscured height, so the final tab can
// always be scrolled above the overlay.
class SidebarMediaOverlayView final : public views::View {
  METADATA_HEADER(SidebarMediaOverlayView, views::View)

 public:
  SidebarMediaOverlayView(std::unique_ptr<views::ScrollView> scroll_view,
                          std::unique_ptr<views::View> media_overlay,
                          views::View* scroll_bottom_inset);
  SidebarMediaOverlayView(const SidebarMediaOverlayView&) = delete;
  SidebarMediaOverlayView& operator=(const SidebarMediaOverlayView&) = delete;
  ~SidebarMediaOverlayView() override;

  views::ScrollView* scroll_view() const { return scroll_view_; }
  views::View* media_overlay() const { return media_overlay_; }

  // Height currently reserved at the bottom of the scroll contents. Exposed
  // for focused layout tests and host diagnostics.
  int scroll_bottom_inset_for_testing() const;

 private:
  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void ChildPreferredSizeChanged(views::View* child) override;
  void ChildVisibilityChanged(views::View* child) override;

  bool UpdateScrollBottomInset(int overlay_height);

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> media_overlay_ = nullptr;
  raw_ptr<views::View> scroll_bottom_inset_ = nullptr;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MEDIA_OVERLAY_VIEW_H_
