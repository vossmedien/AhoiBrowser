// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_swipe_tracker.h"

#include <limits>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(WorkspaceSwipeTrackerTest, CommitsAtMostOncePerGesture) {
  WorkspaceSwipeTracker tracker;
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-50.0f, 2.0f),
            WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureUpdate(-40.0f, 1.0f),
            WorkspaceSwipeDecision::kSwitchNext);
  EXPECT_EQ(tracker.OnGestureUpdate(-100.0f, 0.0f),
            WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kConsume);

  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(90.0f, 0.0f),
            WorkspaceSwipeDecision::kSwitchPrevious);
}

TEST(WorkspaceSwipeTrackerTest, RejectsVerticalAndSupportsDirectionSetting) {
  WorkspaceSwipeSettings settings;
  settings.reverse_direction = true;
  WorkspaceSwipeTracker tracker(settings);
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(10.0f, 30.0f),
            WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureUpdate(-100.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);

  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-90.0f, 0.0f),
            WorkspaceSwipeDecision::kSwitchPrevious);
}

TEST(WorkspaceSwipeTrackerTest, DisabledAndInvalidSettingsFailClosed) {
  WorkspaceSwipeTracker tracker;
  WorkspaceSwipeSettings settings;
  settings.enabled = false;
  ASSERT_TRUE(tracker.SetSettings(settings));
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-1000.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);

  settings.threshold = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(tracker.SetSettings(settings));
  EXPECT_FALSE(tracker.settings().enabled);
}

TEST(WorkspaceSwipeTrackerTest, MomentumCannotInitiateWorkspaceSwitch) {
  WorkspaceSwipeTracker tracker;
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-50.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);

  // The first END on macOS can only mean that the direct phase ended; it
  // cannot yet tell us whether an inertial continuation will arrive.
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);

  // The final momentum END resets the sequence.
  EXPECT_EQ(tracker.OnGestureUpdate(-100.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);

  // A fast direct update still commits before the direct phase ends.
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-90.0f, 0.0f),
            WorkspaceSwipeDecision::kSwitchNext);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kConsume);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kConsume);
}

TEST(WorkspaceSwipeTrackerTest, CancelAndNewBeginDiscardPartialDistance) {
  WorkspaceSwipeTracker tracker;
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-60.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);
  tracker.OnGestureCancel();

  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-30.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);

  // A new direct sequence resets a no-momentum END retained by macOS.
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(-30.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);
}

TEST(WorkspaceSwipeTrackerTest, NonFiniteDeltasFailClosed) {
  WorkspaceSwipeTracker tracker;
  tracker.OnGestureBegin();
  EXPECT_EQ(
      tracker.OnGestureUpdate(-100.0f, std::numeric_limits<float>::infinity()),
      WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureUpdate(-100.0f, 0.0f),
            WorkspaceSwipeDecision::kNone);
}

TEST(WorkspaceSwipeTrackerTest, RejectedGestureStaysRejectedThroughMomentum) {
  WorkspaceSwipeTracker tracker;
  tracker.OnGestureBegin();
  EXPECT_EQ(tracker.OnGestureUpdate(10.0f, 30.0f),
            WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnMomentum(), WorkspaceSwipeDecision::kNone);
  EXPECT_EQ(tracker.OnGestureEnd(), WorkspaceSwipeDecision::kNone);
}

}  // namespace
}  // namespace ahoi
