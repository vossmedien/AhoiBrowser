// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_runtime_refresh_gate.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {

TEST(SidebarRuntimeRefreshGateTest, CoalescesPostedRefreshes) {
  SidebarRuntimeRefreshGate gate;

  EXPECT_TRUE(gate.TrySchedule(false));
  EXPECT_TRUE(gate.scheduled_for_testing());
  EXPECT_FALSE(gate.TrySchedule(false));
  EXPECT_TRUE(gate.BeginRefresh(false));
  EXPECT_FALSE(gate.scheduled_for_testing());
  EXPECT_FALSE(gate.deferred_for_testing());
}

TEST(SidebarRuntimeRefreshGateTest, DefersRequestUntilDragEnds) {
  SidebarRuntimeRefreshGate gate;

  EXPECT_FALSE(gate.TrySchedule(true));
  EXPECT_TRUE(gate.deferred_for_testing());
  EXPECT_FALSE(gate.ConsumeDeferredAfterDrag(true));
  EXPECT_TRUE(gate.ConsumeDeferredAfterDrag(false));
  EXPECT_FALSE(gate.deferred_for_testing());
  EXPECT_TRUE(gate.TrySchedule(false));
}

TEST(SidebarRuntimeRefreshGateTest, DefersPostedTaskWhenDragStarts) {
  SidebarRuntimeRefreshGate gate;

  ASSERT_TRUE(gate.TrySchedule(false));
  EXPECT_FALSE(gate.BeginRefresh(true));
  EXPECT_FALSE(gate.scheduled_for_testing());
  EXPECT_TRUE(gate.deferred_for_testing());
  EXPECT_TRUE(gate.ConsumeDeferredAfterDrag(false));
  EXPECT_FALSE(gate.ConsumeDeferredAfterDrag(false));
}

}  // namespace ahoi::sidebar
