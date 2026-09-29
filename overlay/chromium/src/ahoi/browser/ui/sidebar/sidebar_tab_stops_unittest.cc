// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_stops.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {

namespace {

// Saved rows 1 and 2, a collapsed folder hiding tab 3, a saved split of 4
// and 5, then the temporary rows 6 and 7.
SidebarTabStops BuildSidebar() {
  SidebarTabStops stops;
  stops.AddStop({1});
  stops.AddStop({2});
  stops.AddHiddenTabs({3});
  stops.AddStop({4, 5});
  stops.AddStop({6});
  stops.AddStop({7});
  return stops;
}

}  // namespace

TEST(SidebarTabStopsTest, StepsInSidebarOrderAndWraps) {
  const SidebarTabStops stops = BuildSidebar();
  ASSERT_EQ(5u, stops.stop_count());

  EXPECT_EQ(2, stops.Step(1, 1));
  EXPECT_EQ(4, stops.Step(2, 1));
  EXPECT_EQ(6, stops.Step(4, 1));
  EXPECT_EQ(1, stops.Step(7, 1));
  EXPECT_EQ(7, stops.Step(1, -1));
  EXPECT_EQ(2, stops.Step(4, -1));
  EXPECT_EQ(6, stops.Step(1, 3));
  EXPECT_EQ(7, stops.Step(1, -6));
}

TEST(SidebarTabStopsTest, SplitIsOneStopEnteredAtItsFirstPane) {
  const SidebarTabStops stops = BuildSidebar();

  // From either pane the next stop follows the whole split.
  EXPECT_EQ(6, stops.Step(5, 1));
  EXPECT_EQ(2, stops.Step(5, -1));
  // Stepping into the split always lands on its first pane.
  EXPECT_EQ(4, stops.Step(6, -1));
  EXPECT_EQ(4, stops.Nth(1, 2));
  // Selecting the split from its second pane keeps that pane focused.
  EXPECT_EQ(std::nullopt, stops.Nth(5, 2));
}

TEST(SidebarTabStopsTest, CollapsedFolderIsSkippedButAnchorsItsTabs) {
  const SidebarTabStops stops = BuildSidebar();

  // Tab 3 is not a stop: numbering and stepping pass over it.
  EXPECT_EQ(4, stops.Nth(std::nullopt, 2));
  EXPECT_EQ(4, stops.Step(2, 1));
  // A shown tab inside the collapsed folder steps from where the folder
  // sits, between saved row 2 and the split.
  EXPECT_EQ(4, stops.Step(3, 1));
  EXPECT_EQ(2, stops.Step(3, -1));
  EXPECT_EQ(6, stops.Step(3, 2));
  EXPECT_EQ(1, stops.Step(3, -2));
}

TEST(SidebarTabStopsTest, HiddenTabsAtTheEndsWrap) {
  SidebarTabStops stops;
  stops.AddHiddenTabs({10});
  stops.AddStop({1});
  stops.AddStop({2});
  stops.AddHiddenTabs({20});

  EXPECT_EQ(1, stops.Step(10, 1));
  EXPECT_EQ(2, stops.Step(10, -1));
  EXPECT_EQ(1, stops.Step(20, 1));
  EXPECT_EQ(2, stops.Step(20, -1));
}

TEST(SidebarTabStopsTest, UnknownActiveTabStepsFromTheEnds) {
  const SidebarTabStops stops = BuildSidebar();

  EXPECT_EQ(1, stops.Step(99, 1));
  EXPECT_EQ(7, stops.Step(99, -1));
  EXPECT_EQ(1, stops.Step(std::nullopt, 1));
  EXPECT_EQ(7, stops.Step(std::nullopt, -1));
}

TEST(SidebarTabStopsTest, NumberedStopsCountStopsOnly) {
  const SidebarTabStops stops = BuildSidebar();

  EXPECT_EQ(1, stops.Nth(7, 0));
  EXPECT_EQ(2, stops.Nth(7, 1));
  EXPECT_EQ(6, stops.Nth(7, 3));
  EXPECT_EQ(std::nullopt, stops.Nth(7, 4));
  EXPECT_EQ(std::nullopt, stops.Nth(1, 5));
  EXPECT_EQ(7, stops.Last(1));
  EXPECT_EQ(std::nullopt, stops.Last(7));
}

TEST(SidebarTabStopsTest, SingleStopIsANoOp) {
  SidebarTabStops stops;
  stops.AddStop({4, 5});

  EXPECT_EQ(std::nullopt, stops.Step(4, 1));
  EXPECT_EQ(std::nullopt, stops.Step(5, -1));
  EXPECT_EQ(std::nullopt, stops.Last(5));
  EXPECT_EQ(4, stops.Step(99, 1));
}

TEST(SidebarTabStopsTest, EmptySidebarHasNothingToActivate) {
  SidebarTabStops stops;
  stops.AddStop({});
  stops.AddHiddenTabs({3});

  EXPECT_EQ(0u, stops.stop_count());
  EXPECT_EQ(std::nullopt, stops.Step(3, 1));
  EXPECT_EQ(std::nullopt, stops.Step(std::nullopt, -1));
  EXPECT_EQ(std::nullopt, stops.Nth(std::nullopt, 0));
  EXPECT_EQ(std::nullopt, stops.Last(std::nullopt));
  EXPECT_EQ(std::nullopt, BuildSidebar().Step(1, 0));
}

}  // namespace ahoi::sidebar
