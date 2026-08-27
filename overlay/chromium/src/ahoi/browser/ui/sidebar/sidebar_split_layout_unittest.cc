// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"

#include "components/split_tabs/split_tab_visual_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {
namespace {

TEST(SidebarSplitLayoutTest, FourPaneMirrorsRowMajorTwoByTwoGrid) {
  const gfx::Rect bounds(0, 0, 200, 50);
  const auto visual_data = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kSideBySide);

  EXPECT_EQ(gfx::Rect(0, 0, 99, 24),
            GetSplitSegmentBounds(bounds, 0, 4, visual_data));
  EXPECT_EQ(gfx::Rect(101, 0, 99, 24),
            GetSplitSegmentBounds(bounds, 1, 4, visual_data));
  EXPECT_EQ(gfx::Rect(0, 26, 99, 24),
            GetSplitSegmentBounds(bounds, 2, 4, visual_data));
  EXPECT_EQ(gfx::Rect(101, 26, 99, 24),
            GetSplitSegmentBounds(bounds, 3, 4, visual_data));
}

TEST(SidebarSplitLayoutTest, SplitRatiosExcludeGapsFromAvailableExtent) {
  const gfx::Rect bounds(0, 0, 200, 50);
  const split_tabs::SplitTabVisualData stacked(
      split_tabs::SplitTabLayout::kStacked, 0.5);
  const auto three_main = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kSideBySide,
      split_tabs::SplitTabArrangement::kMainStart);

  EXPECT_EQ(24, GetSplitSegmentBounds(bounds, 0, 2, stacked).height());
  EXPECT_EQ(24, GetSplitSegmentBounds(bounds, 1, 2, stacked).height());
  EXPECT_EQ(24, GetSplitSegmentBounds(bounds, 1, 3, three_main).height());
  EXPECT_EQ(24, GetSplitSegmentBounds(bounds, 2, 3, three_main).height());
}

TEST(SidebarSplitLayoutTest, RuntimeHeightKeepsStackedSegmentsReadable) {
  constexpr int kStandardRowHeight = 32;
  const split_tabs::SplitTabVisualData side_by_side(
      split_tabs::SplitTabLayout::kSideBySide);
  const split_tabs::SplitTabVisualData stacked(
      split_tabs::SplitTabLayout::kStacked);
  const auto three_stacked = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kStacked);
  const auto three_main = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kSideBySide,
      split_tabs::SplitTabArrangement::kMainStart);
  const auto four = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kSideBySide);

  EXPECT_EQ(32,
            GetSplitRowPreferredHeight(2, side_by_side, kStandardRowHeight));
  EXPECT_EQ(50, GetSplitRowPreferredHeight(2, stacked, kStandardRowHeight));
  EXPECT_EQ(76,
            GetSplitRowPreferredHeight(3, three_stacked, kStandardRowHeight));
  EXPECT_EQ(50, GetSplitRowPreferredHeight(3, three_main, kStandardRowHeight));
  EXPECT_EQ(50, GetSplitRowPreferredHeight(4, four, kStandardRowHeight));
}

}  // namespace
}  // namespace ahoi::sidebar
