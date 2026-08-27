// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"

#include "components/split_tabs/split_tab_visual_data.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/insets_f.h"

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

TEST(SidebarSplitLayoutTest,
     SplitSeparatorsStayInsideInsetPaintedGroupBackground) {
  const gfx::Rect group_bounds(0, 0, 200, 50);
  const auto visual_data = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kSideBySide);
  std::vector<gfx::Rect> segments;
  for (size_t index = 0; index < 4; ++index) {
    segments.push_back(
        GetSplitSegmentBounds(group_bounds, index, 4, visual_data));
  }

  // The group paint is inset 4 DIPs horizontally and 2 vertically. Account for
  // half of the one-DIP separator stroke so anti-aliasing cannot escape it.
  gfx::RectF separator_bounds(group_bounds);
  separator_bounds.Inset(gfx::InsetsF::VH(2.5f, 4.5f));
  const std::vector<SidebarSplitSeparator> separators =
      GetSidebarSplitSeparators(segments, separator_bounds);

  EXPECT_EQ(
      (std::vector<SidebarSplitSeparator>{
          {.start = gfx::PointF(100.0f, 2.5f),
           .end = gfx::PointF(100.0f, 24.0f)},
          {.start = gfx::PointF(4.5f, 25.0f), .end = gfx::PointF(99.0f, 25.0f)},
          {.start = gfx::PointF(101.0f, 25.0f),
           .end = gfx::PointF(195.5f, 25.0f)},
          {.start = gfx::PointF(100.0f, 26.0f),
           .end = gfx::PointF(100.0f, 47.5f)},
      }),
      separators);
}

}  // namespace
}  // namespace ahoi::sidebar
