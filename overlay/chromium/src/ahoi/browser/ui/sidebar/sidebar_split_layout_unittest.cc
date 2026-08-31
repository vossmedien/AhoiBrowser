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

TEST(SidebarSplitLayoutTest,
     ThreePaneLinearPrimaryRatioExcludesBothDividerGaps) {
  const gfx::Rect horizontal_bounds(0, 0, 100, 50);
  auto horizontal = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kSideBySide,
      split_tabs::SplitTabArrangement::kLinear);
  ASSERT_TRUE(horizontal.set_split_ratio(0.5));
  ASSERT_TRUE(horizontal.set_secondary_split_ratio(0.5));

  EXPECT_EQ(gfx::Rect(0, 0, 48, 50),
            GetSplitSegmentBounds(horizontal_bounds, 0, 3, horizontal));
  EXPECT_EQ(gfx::Rect(50, 0, 24, 50),
            GetSplitSegmentBounds(horizontal_bounds, 1, 3, horizontal));
  EXPECT_EQ(gfx::Rect(76, 0, 24, 50),
            GetSplitSegmentBounds(horizontal_bounds, 2, 3, horizontal));

  const gfx::Rect vertical_bounds(0, 0, 50, 100);
  auto vertical = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kStacked,
      split_tabs::SplitTabArrangement::kLinear);
  ASSERT_TRUE(vertical.set_split_ratio(0.5));
  ASSERT_TRUE(vertical.set_secondary_split_ratio(0.5));

  EXPECT_EQ(gfx::Rect(0, 0, 50, 48),
            GetSplitSegmentBounds(vertical_bounds, 0, 3, vertical));
  EXPECT_EQ(gfx::Rect(0, 50, 50, 24),
            GetSplitSegmentBounds(vertical_bounds, 1, 3, vertical));
  EXPECT_EQ(gfx::Rect(0, 76, 50, 24),
            GetSplitSegmentBounds(vertical_bounds, 2, 3, vertical));
}

TEST(SidebarSplitLayoutTest,
     ThreePaneLinearDividersUseChromiumRatioExtentsAndIndices) {
  const gfx::Rect group_bounds(0, 0, 100, 50);
  auto visual_data = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kSideBySide,
      split_tabs::SplitTabArrangement::kLinear);
  ASSERT_TRUE(visual_data.set_split_ratio(0.5));
  ASSERT_TRUE(visual_data.set_secondary_split_ratio(0.5));
  std::vector<gfx::Rect> segments;
  for (size_t index = 0; index < 3; ++index) {
    segments.push_back(
        GetSplitSegmentBounds(group_bounds, index, 3, visual_data));
  }

  const std::vector<SidebarSplitDivider> dividers = GetSidebarSplitDividers(
      group_bounds, segments, gfx::RectF(group_bounds), visual_data);

  ASSERT_EQ(2u, dividers.size());
  EXPECT_EQ(0u, dividers[0].divider_index);
  EXPECT_EQ(96, dividers[0].ratio_extent);
  EXPECT_EQ(gfx::PointF(49.0f, 0.0f), dividers[0].start);
  EXPECT_EQ(gfx::PointF(49.0f, 50.0f), dividers[0].end);
  EXPECT_EQ(1u, dividers[1].divider_index);
  EXPECT_EQ(48, dividers[1].ratio_extent);
  EXPECT_EQ(gfx::PointF(75.0f, 0.0f), dividers[1].start);
  EXPECT_EQ(gfx::PointF(75.0f, 50.0f), dividers[1].end);
  EXPECT_EQ(gfx::Rect(44, 0, 10, 50),
            GetSidebarSplitDividerHitBounds(dividers[0]));
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

TEST(SidebarSplitLayoutTest, EdgeDropTargetsAreStableFullWidthSurfaces) {
  const gfx::Rect row_bounds(0, 0, 240, 40);
  const gfx::RectF before =
      GetSidebarEdgeDropTargetBounds(row_bounds, /*trailing_edge=*/false);
  const gfx::RectF after =
      GetSidebarEdgeDropTargetBounds(row_bounds, /*trailing_edge=*/true);

  EXPECT_EQ(12, GetSidebarEdgeDropTargetExtent(row_bounds.height()));
  EXPECT_EQ(gfx::RectF(4.0f, 0.0f, 232.0f, 12.0f), before);
  EXPECT_EQ(gfx::RectF(4.0f, 28.0f, 232.0f, 12.0f), after);
  EXPECT_EQ(before.size(), after.size());
}

TEST(SidebarSplitLayoutTest, EdgeDropTargetsRemainBoundedInNarrowSplitPane) {
  const gfx::Rect pane_bounds(0, 0, 6, 30);
  const gfx::RectF target =
      GetSidebarEdgeDropTargetBounds(pane_bounds, /*trailing_edge=*/false);

  EXPECT_FALSE(target.IsEmpty());
  EXPECT_GE(target.x(), pane_bounds.x());
  EXPECT_LE(target.right(), pane_bounds.right());
  EXPECT_LE(target.bottom(), pane_bounds.bottom());
}

}  // namespace
}  // namespace ahoi::sidebar
