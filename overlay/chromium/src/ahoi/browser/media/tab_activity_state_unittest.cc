// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/tab_activity_state.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(AhoiTabActivityStateTest, ProjectsOnlyChromiumCaptureAlerts) {
  for (const tabs::TabAlert alert :
       {tabs::TabAlert::kMediaRecording, tabs::TabAlert::kAudioRecording,
        tabs::TabAlert::kVideoRecording, tabs::TabAlert::kTabCapturing,
        tabs::TabAlert::kDesktopCapturing}) {
    EXPECT_EQ(AhoiTabActivityState::FromChromiumAlert(alert).primary_activity,
              alert);
  }

  EXPECT_FALSE(
      AhoiTabActivityState::FromChromiumAlert(tabs::TabAlert::kAudioPlaying)
          .primary_activity.has_value());
  EXPECT_FALSE(AhoiTabActivityState::FromChromiumAlert(std::nullopt)
                   .primary_activity.has_value());
}

TEST(AhoiTabActivityStateTest, RetainsDistinctCaptureSemantics) {
  EXPECT_NE(
      AhoiTabActivityState::FromChromiumAlert(tabs::TabAlert::kAudioRecording),
      AhoiTabActivityState::FromChromiumAlert(tabs::TabAlert::kVideoRecording));
  EXPECT_NE(
      AhoiTabActivityState::FromChromiumAlert(tabs::TabAlert::kTabCapturing),
      AhoiTabActivityState::FromChromiumAlert(
          tabs::TabAlert::kDesktopCapturing));
}

}  // namespace
}  // namespace ahoi
