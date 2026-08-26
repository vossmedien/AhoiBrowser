// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_keyboard_actions.h"

#include <cmath>
#include <limits>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::split_drop {
namespace {

TEST(SplitKeyboardActionsTest, ResolvesOnlyVisibleOneBasedPanes) {
  EXPECT_EQ(0u, ResolveKeyboardPane(1, 4));
  EXPECT_EQ(3u, ResolveKeyboardPane(4, 4));
  EXPECT_FALSE(ResolveKeyboardPane(0, 4).has_value());
  EXPECT_FALSE(ResolveKeyboardPane(3, 2).has_value());
  EXPECT_FALSE(ResolveKeyboardPane(1, 1).has_value());
}

TEST(SplitKeyboardActionsTest, ReorderStopsAtEdgesAndRejectsBadDirection) {
  EXPECT_EQ(1u, ResolveReorderTarget(0, 4, 1));
  EXPECT_EQ(1u, ResolveReorderTarget(2, 4, -1));
  EXPECT_FALSE(ResolveReorderTarget(0, 4, -1).has_value());
  EXPECT_FALSE(ResolveReorderTarget(3, 4, 1).has_value());
  EXPECT_FALSE(ResolveReorderTarget(1, 4, 2).has_value());
}

TEST(SplitKeyboardActionsTest, TwoAndFourPaneLayoutsToggleOrientation) {
  split_tabs::SplitTabVisualData two(split_tabs::SplitTabLayout::kSideBySide);
  EXPECT_EQ(split_tabs::SplitTabLayout::kStacked,
            NextKeyboardLayout(two, 2).split_layout());

  split_tabs::SplitTabVisualData four =
      split_tabs::SplitTabVisualData::ForFourPane(
          split_tabs::SplitTabLayout::kStacked);
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide,
            NextKeyboardLayout(four, 4).split_layout());
}

TEST(SplitKeyboardActionsTest, ThreePaneCycleCoversAllSixLayouts) {
  split_tabs::SplitTabVisualData state =
      split_tabs::SplitTabVisualData::ForThreePane(
          split_tabs::SplitTabLayout::kSideBySide);
  const split_tabs::SplitTabVisualData original = state;
  for (int step = 0; step < 6; ++step) {
    state = NextKeyboardLayout(state, 3);
  }
  EXPECT_EQ(original, state);
}

TEST(SplitKeyboardActionsTest, DividerStepsAreBoundedAndFailClosed) {
  EXPECT_DOUBLE_EQ(0.55, AdjustKeyboardSplitRatio(0.5, 1));
  EXPECT_DOUBLE_EQ(0.1, AdjustKeyboardSplitRatio(0.1, -1));
  EXPECT_DOUBLE_EQ(0.9, AdjustKeyboardSplitRatio(0.9, 1));
  EXPECT_DOUBLE_EQ(0.4, AdjustKeyboardSplitRatio(0.4, 7));
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_TRUE(std::isnan(AdjustKeyboardSplitRatio(nan, 1)));
}

}  // namespace
}  // namespace ahoi::split_drop
