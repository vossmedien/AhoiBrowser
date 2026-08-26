// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"

#include "components/tabs/public/tab_alert.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {
namespace {

TEST(SidebarMediaIndicatorTest, MapsAudibleMutedAndPipStates) {
  EXPECT_FALSE(
      GetSidebarMediaIndicator(tabs::TabAlert::kAudioPlaying).IsEmpty());
  EXPECT_FALSE(
      GetSidebarMediaIndicator(tabs::TabAlert::kAudioMuting).IsEmpty());
  EXPECT_FALSE(GetSidebarMediaIndicator(tabs::TabAlert::kPipPlaying).IsEmpty());
  EXPECT_TRUE(GetSidebarMediaIndicator(std::nullopt).IsEmpty());
}

TEST(SidebarMediaIndicatorTest, MapsChromiumCaptureActivityStates) {
  for (const tabs::TabAlert alert :
       {tabs::TabAlert::kMediaRecording, tabs::TabAlert::kAudioRecording,
        tabs::TabAlert::kVideoRecording, tabs::TabAlert::kTabCapturing,
        tabs::TabAlert::kDesktopCapturing}) {
    EXPECT_FALSE(GetSidebarMediaIndicator(alert).IsEmpty());
  }
  EXPECT_TRUE(
      GetSidebarMediaIndicator(tabs::TabAlert::kUsbConnected).IsEmpty());
}

TEST(SidebarMediaIndicatorTest, MediaSessionKeepsMutedPlaybackVisible) {
  EXPECT_EQ(GetSidebarMediaAlertForSession(/*playing=*/true, /*muted=*/true,
                                           /*picture_in_picture=*/false,
                                           /*relevant=*/true),
            tabs::TabAlert::kAudioMuting);
  EXPECT_EQ(GetSidebarMediaAlertForSession(/*playing=*/false, /*muted=*/false,
                                           /*picture_in_picture=*/true,
                                           /*relevant=*/true),
            tabs::TabAlert::kPipPlaying);
  EXPECT_FALSE(GetSidebarMediaAlertForSession(
                   /*playing=*/false, /*muted=*/true,
                   /*picture_in_picture=*/false, /*relevant=*/false)
                   .has_value());
}

TEST(SidebarMediaIndicatorTest, HoverActionNeverCoversMediaStatus) {
  const SidebarTabTrailingLayout layout =
      GetSidebarTabTrailingLayout(240, 32, true);
  EXPECT_FALSE(layout.media_indicator.IsEmpty());
  EXPECT_FALSE(layout.hover_action.IsEmpty());
  EXPECT_FALSE(layout.media_indicator.Intersects(layout.hover_action));
  EXPECT_LE(layout.title.right(), layout.media_indicator.x());
}

TEST(SidebarMediaIndicatorTest, NarrowSplitCellLayoutRemainsBounded) {
  const SidebarTabTrailingLayout layout =
      GetSidebarTabTrailingLayout(92, 24, true);
  EXPECT_GE(layout.title.width(), 0);
  EXPECT_GE(layout.media_indicator.x(), 0);
  EXPECT_GE(layout.hover_action.x(), 0);
  EXPECT_FALSE(layout.media_indicator.Intersects(layout.hover_action));
}

}  // namespace
}  // namespace ahoi::sidebar
