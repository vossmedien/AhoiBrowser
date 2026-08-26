// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/ahoi_media_state.h"

#include <optional>

#include "base/functional/bind.h"
#include "components/tabs/public/tab_alert.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(AhoiMediaStateTest, MapsSignalsAndPriority) {
  const AhoiMediaState state =
      AhoiMediaState::FromSignals(true, true, true, true);
  EXPECT_TRUE(state.audio_playing);
  EXPECT_TRUE(state.audio_muted);
  EXPECT_TRUE(state.audio_muting);
  EXPECT_TRUE(state.picture_in_picture);
  EXPECT_EQ(state.primary_alert, tabs::TabAlert::kPipPlaying);

  EXPECT_EQ(AhoiMediaState::FromSignals(true, true, true, false).primary_alert,
            tabs::TabAlert::kAudioMuting);
  EXPECT_EQ(
      AhoiMediaState::FromSignals(true, false, false, false).primary_alert,
      tabs::TabAlert::kAudioPlaying);
}

TEST(AhoiMediaStateTest, EmptySignalsHaveNoPrimaryAlert) {
  const AhoiMediaState state =
      AhoiMediaState::FromSignals(false, true, false, false);
  EXPECT_FALSE(state.audio_playing);
  EXPECT_TRUE(state.audio_muted);
  EXPECT_FALSE(state.audio_muting);
  EXPECT_FALSE(state.picture_in_picture);
  EXPECT_FALSE(state.primary_alert.has_value());

  AhoiMediaStateTracker tracker;
  EXPECT_EQ(tracker.state(), AhoiMediaState());
  tracker.SetWebContents(nullptr);
  EXPECT_EQ(tracker.state(), AhoiMediaState());
}

TEST(AhoiMediaStateModelTest, NotifiesOnlyForActualChanges) {
  AhoiMediaStateModel model;
  int notifications = 0;
  std::optional<AhoiMediaState> last_state;
  auto subscription = model.AddStateChangedCallback(base::BindRepeating(
      [](int* notifications, std::optional<AhoiMediaState>* last_state,
         const AhoiMediaState& state) {
        ++*notifications;
        *last_state = state;
      },
      &notifications, &last_state));

  const AhoiMediaState playing =
      AhoiMediaState::FromSignals(true, false, false, false);
  EXPECT_TRUE(model.Update(playing));
  EXPECT_FALSE(model.Update(playing));
  EXPECT_EQ(notifications, 1);
  ASSERT_TRUE(last_state.has_value());
  EXPECT_EQ(*last_state, playing);

  EXPECT_TRUE(model.Reset());
  EXPECT_FALSE(model.Reset());
  EXPECT_EQ(notifications, 2);
}

TEST(AhoiMediaStateTest, MutingRequiresRecentAudioForIndicator) {
  const AhoiMediaState muted_without_audio =
      AhoiMediaState::FromSignals(false, true, false, false);
  EXPECT_FALSE(muted_without_audio.audio_muting);
  EXPECT_FALSE(muted_without_audio.primary_alert.has_value());

  const AhoiMediaState muted_after_audio =
      AhoiMediaState::FromSignals(false, true, true, false);
  EXPECT_TRUE(muted_after_audio.audio_muting);
  EXPECT_EQ(muted_after_audio.primary_alert, tabs::TabAlert::kAudioMuting);
}

TEST(AhoiMediaStateTest, ChromiumCaptureActivityOutranksPlayback) {
  EXPECT_EQ(AhoiMediaState::FromSignals(
                /*audio_playing=*/true, /*audio_muted=*/false,
                /*recently_audible=*/true, /*picture_in_picture=*/true,
                tabs::TabAlert::kAudioRecording)
                .primary_alert,
            tabs::TabAlert::kAudioRecording);
  EXPECT_EQ(AhoiMediaState::FromSignals(
                /*audio_playing=*/true, /*audio_muted=*/false,
                /*recently_audible=*/true, /*picture_in_picture=*/false,
                tabs::TabAlert::kDesktopCapturing)
                .capture_activity.primary_activity,
            tabs::TabAlert::kDesktopCapturing);
}

}  // namespace
}  // namespace ahoi
