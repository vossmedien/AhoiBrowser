// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_title_label.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi::sidebar {
namespace {

class SidebarTabTitleLabelTest : public views::ViewsTestBase {};

TEST_F(SidebarTabTitleLabelTest,
       SplitBoundsStopBeforeTrailingAndBottomDivider) {
  const gfx::Rect pane_bounds(0, 0, 120, 30);
  const gfx::Rect requested_bounds(30, 0, 140, 40);

  const gfx::Rect clipped = GetDividerSafeSidebarTitleBounds(
      requested_bounds, pane_bounds, /*has_split_separator=*/true);

  EXPECT_EQ(gfx::Rect(30, 1, 89, 28), clipped);
  EXPECT_LT(clipped.right(), pane_bounds.right());
  EXPECT_LT(clipped.bottom(), pane_bounds.bottom());
}

TEST_F(SidebarTabTitleLabelTest, SplitDropHalfIsARealPaintClip) {
  SidebarTabTitleLabel label;
  label.SetText(
      u"A deliberately long tab title that must end before the divider");
  const gfx::Rect leading_half(0, 0, 120, 40);
  label.SetDividerSafeBounds(gfx::Rect(30, 0, 190, 40), leading_half,
                             /*has_split_separator=*/true);

  EXPECT_EQ(gfx::Rect(30, 1, 89, 38), label.bounds());
  EXPECT_EQ(label.GetLocalBounds(), label.paint_clip_bounds_for_testing());
  EXPECT_LT(label.bounds().right(), leading_half.right());
  EXPECT_LT(label.bounds().bottom(), leading_half.bottom());
  const std::u16string_view display_text = label.GetDisplayTextForTesting();
  ASSERT_FALSE(display_text.empty());
  EXPECT_NE(label.GetText(), display_text);
  EXPECT_EQ(u'\u2026', display_text.back());
}

TEST_F(SidebarTabTitleLabelTest, OrdinaryRowKeepsItsFullRequestedBounds) {
  const gfx::Rect pane_bounds(0, 0, 240, 40);
  EXPECT_EQ(
      gfx::Rect(30, 0, 180, 40),
      GetDividerSafeSidebarTitleBounds(gfx::Rect(30, 0, 180, 40), pane_bounds,
                                       /*has_split_separator=*/false));
}

}  // namespace
}  // namespace ahoi::sidebar
