// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi::sidebar {
namespace {

class SidebarMediaOverlayViewTest : public views::ViewsTestBase {};

struct OverlayFixture {
  std::unique_ptr<SidebarMediaOverlayView> host;
  raw_ptr<views::View> overlay = nullptr;
};

OverlayFixture CreateOverlayFixture() {
  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* rows = contents->AddChildView(std::make_unique<views::View>());
  rows->SetPreferredSize(gfx::Size(0, 500));
  auto* inset = contents->AddChildView(std::make_unique<views::View>());

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetContents(std::move(contents));
  auto overlay = std::make_unique<views::View>();
  overlay->SetPreferredSize(gfx::Size(0, 80));
  views::View* const overlay_ptr = overlay.get();
  return {.host = std::make_unique<SidebarMediaOverlayView>(
              std::move(scroll), std::move(overlay), inset),
          .overlay = overlay_ptr};
}

TEST_F(SidebarMediaOverlayViewTest, OverlaysFullHeightScrollAndReservesInset) {
  OverlayFixture fixture = CreateOverlayFixture();
  fixture.host->SetBoundsRect(gfx::Rect(0, 0, 240, 300));
  fixture.host->DeprecatedLayoutImmediately();

  EXPECT_EQ(gfx::Rect(0, 0, 240, 300), fixture.host->scroll_view()->bounds());
  EXPECT_EQ(gfx::Rect(0, 220, 240, 80), fixture.overlay->bounds());
  EXPECT_EQ(88, fixture.host->scroll_bottom_inset_for_testing());
}

TEST_F(SidebarMediaOverlayViewTest, HiddenOverlayReleasesInset) {
  OverlayFixture fixture = CreateOverlayFixture();
  fixture.host->SetBoundsRect(gfx::Rect(0, 0, 240, 300));
  fixture.host->DeprecatedLayoutImmediately();
  ASSERT_EQ(88, fixture.host->scroll_bottom_inset_for_testing());

  fixture.overlay->SetVisible(false);
  fixture.host->DeprecatedLayoutImmediately();
  EXPECT_EQ(0, fixture.host->scroll_bottom_inset_for_testing());
  EXPECT_EQ(0, fixture.overlay->height());
}

TEST_F(SidebarMediaOverlayViewTest, ResizeKeepsOverlayAtBottom) {
  OverlayFixture fixture = CreateOverlayFixture();
  fixture.host->SetBoundsRect(gfx::Rect(0, 0, 180, 220));
  fixture.host->DeprecatedLayoutImmediately();
  EXPECT_EQ(gfx::Rect(0, 140, 180, 80), fixture.overlay->bounds());

  fixture.host->SetBoundsRect(gfx::Rect(0, 0, 320, 410));
  fixture.host->DeprecatedLayoutImmediately();
  EXPECT_EQ(gfx::Rect(0, 330, 320, 80), fixture.overlay->bounds());
  EXPECT_EQ(88, fixture.host->scroll_bottom_inset_for_testing());
}

TEST_F(SidebarMediaOverlayViewTest, ExpandedHeightUpdatesScrollInset) {
  OverlayFixture fixture = CreateOverlayFixture();
  fixture.host->SetBoundsRect(gfx::Rect(0, 0, 240, 300));
  fixture.host->DeprecatedLayoutImmediately();
  ASSERT_EQ(88, fixture.host->scroll_bottom_inset_for_testing());

  fixture.overlay->SetPreferredSize(gfx::Size(0, 120));
  fixture.host->DeprecatedLayoutImmediately();
  EXPECT_EQ(gfx::Rect(0, 180, 240, 120), fixture.overlay->bounds());
  EXPECT_EQ(128, fixture.host->scroll_bottom_inset_for_testing());
}

}  // namespace
}  // namespace ahoi::sidebar
