// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/navigation_input_prefs.h"

#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::navigation_input_prefs {
namespace {

class NavigationInputPrefsTest : public testing::Test {
 protected:
  NavigationInputPrefsTest() { RegisterProfilePrefs(prefs_.registry()); }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(NavigationInputPrefsTest, DefaultsMatchRuntimeInputContract) {
  const WorkspaceSwipeSettings workspace = ReadWorkspaceSwipeSettings(prefs_);
  EXPECT_TRUE(workspace.enabled);
  EXPECT_FALSE(workspace.reverse_direction);
  EXPECT_FLOAT_EQ(80.0f, workspace.threshold);

  const CmdScrollTabSettings cmd_scroll = ReadCmdScrollTabSettings(prefs_);
  EXPECT_TRUE(cmd_scroll.enabled);
  EXPECT_FLOAT_EQ(24.0f, cmd_scroll.threshold);
  EXPECT_EQ(base::Milliseconds(250), cmd_scroll.minimum_interval);
  EXPECT_TRUE(IsMiddleClickAutoscrollEnabled(prefs_));
}

TEST_F(NavigationInputPrefsTest, ValuesClampAndEveryGestureCanBeDisabled) {
  prefs_.SetBoolean(kWorkspaceSwipeEnabled, false);
  prefs_.SetDouble(kWorkspaceSwipeThreshold, 10000.0);
  prefs_.SetDouble(kWorkspaceSwipeAxisBias, -1.0);
  prefs_.SetBoolean(kCmdScrollEnabled, false);
  prefs_.SetDouble(kCmdScrollThreshold, 0.0);
  prefs_.SetInteger(kCmdScrollMinimumIntervalMs, 9000);
  prefs_.SetBoolean(kMiddleClickAutoscrollEnabled, false);

  const WorkspaceSwipeSettings workspace = ReadWorkspaceSwipeSettings(prefs_);
  EXPECT_FALSE(workspace.enabled);
  EXPECT_FLOAT_EQ(240.0f, workspace.threshold);
  EXPECT_FLOAT_EQ(1.0f, workspace.axis_bias);

  const CmdScrollTabSettings cmd_scroll = ReadCmdScrollTabSettings(prefs_);
  EXPECT_FALSE(cmd_scroll.enabled);
  EXPECT_FLOAT_EQ(4.0f, cmd_scroll.threshold);
  EXPECT_EQ(base::Seconds(2), cmd_scroll.minimum_interval);
  EXPECT_FALSE(IsMiddleClickAutoscrollEnabled(prefs_));
}

}  // namespace
}  // namespace ahoi::navigation_input_prefs
