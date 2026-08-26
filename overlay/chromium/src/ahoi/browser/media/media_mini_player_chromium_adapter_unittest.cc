// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/media_mini_player_chromium_adapter.h"

#include <vector>

#include "services/media_session/public/mojom/media_session.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

using Action = media_session::mojom::MediaSessionAction;

TEST(MediaMiniPlayerChromiumAdapterTest, MapsAdvertisedCapabilities) {
  const std::vector<Action> actions = {
      Action::kPlay,
      Action::kSetMute,
      Action::kEnterPictureInPicture,
      Action::kSeekTo,
  };
  const MediaMiniPlayerCapabilities capabilities =
      MediaMiniPlayerChromiumAdapter::CapabilitiesForActions(actions);
  EXPECT_TRUE(capabilities.can_play_pause);
  EXPECT_TRUE(capabilities.can_mute);
  EXPECT_TRUE(capabilities.can_picture_in_picture);
  EXPECT_TRUE(capabilities.can_seek);
}

TEST(MediaMiniPlayerChromiumAdapterTest, DoesNotInventCapabilities) {
  const MediaMiniPlayerCapabilities capabilities =
      MediaMiniPlayerChromiumAdapter::CapabilitiesForActions(
          {Action::kNextTrack});
  EXPECT_FALSE(capabilities.can_play_pause);
  EXPECT_FALSE(capabilities.can_mute);
  EXPECT_FALSE(capabilities.can_picture_in_picture);
  EXPECT_FALSE(capabilities.can_seek);
}

TEST(MediaMiniPlayerChromiumAdapterTest, MapsPlaybackState) {
  EXPECT_EQ(
      MediaMiniPlayerChromiumAdapter::PlaybackStateFor(
          media_session::mojom::MediaPlaybackState::kPlaying),
      MediaMiniPlayerPlaybackState::kPlaying);
  EXPECT_EQ(
      MediaMiniPlayerChromiumAdapter::PlaybackStateFor(
          media_session::mojom::MediaPlaybackState::kPaused),
      MediaMiniPlayerPlaybackState::kPaused);
}

TEST(MediaMiniPlayerChromiumAdapterTest, RejectsNullOrEmptyRegistration) {
  MediaMiniPlayerService service;
  MediaMiniPlayerChromiumAdapter adapter(service);
  EXPECT_FALSE(adapter.RegisterWebContents("", nullptr));
  EXPECT_FALSE(adapter.RegisterWebContents("tab-a", nullptr));
  EXPECT_FALSE(adapter.IsRegistered("tab-a"));
}

}  // namespace
}  // namespace ahoi
