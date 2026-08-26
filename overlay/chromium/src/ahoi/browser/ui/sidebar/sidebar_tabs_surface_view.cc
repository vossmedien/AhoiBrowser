// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tabs_surface_view.h"

#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

namespace {

class SidebarTabsSurfaceView final : public views::View {
 public:
  SidebarTabsSurfaceView() = default;
  SidebarTabsSurfaceView(const SidebarTabsSurfaceView&) = delete;
  SidebarTabsSurfaceView& operator=(const SidebarTabsSurfaceView&) = delete;
  ~SidebarTabsSurfaceView() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size preferred = views::View::CalculatePreferredSize(available_size);
    if (available_size.width().is_bounded()) {
      preferred.set_width(available_size.width().value());
    }
    return preferred;
  }
};

}  // namespace

std::unique_ptr<views::View> CreateSidebarTabsSurfaceView() {
  return std::make_unique<SidebarTabsSurfaceView>();
}

}  // namespace ahoi::sidebar
