// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/media_mini_player_chromium_adapter.h"

#include <vector>

#include "ahoi/browser/media/media_mini_player_service.h"
#include "base/time/time.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

using Action = media_session::mojom::MediaSessionAction;
using Signals = MediaMiniPlayerChromiumAdapter::SessionSignals;
using Projection = MediaMiniPlayerChromiumAdapter::SessionProjection;

Signals ControllableSession(MediaMiniPlayerPlaybackState playback) {
  Signals signals;
  signals.has_session_info = true;
  signals.session_playback = playback;
  signals.session_controllable = true;
  // The actions Chromium adds for a controllable, non-live session.
  signals.actions = {Action::kPlay, Action::kPause, Action::kStop,
                     Action::kSeekTo, Action::kScrubTo};
  return signals;
}

// A short notification sound: Chromium reports the session as playing, but
// with transient audio focus it is not controllable and adds no actions.
Signals TransientChime() {
  Signals signals;
  signals.has_session_info = true;
  signals.session_playback = MediaMiniPlayerPlaybackState::kPlaying;
  signals.tab_audible = true;
  return signals;
}

Projection Project(const Signals& signals) {
  return MediaMiniPlayerChromiumAdapter::ProjectSession(signals);
}

// Mirrors SourceObserver::RefreshSource for a session with a known position.
MediaMiniPlayerSource SourceFor(const char* id,
                                int presentation_order,
                                const Projection& projection) {
  MediaMiniPlayerSource source;
  source.id = id;
  source.presentation_order = presentation_order;
  source.playback = projection.playback;
  source.is_muted = projection.is_muted;
  source.is_in_picture_in_picture = projection.is_in_picture_in_picture;
  if (projection.expose_position) {
    source.duration = base::Seconds(30);
  }
  source.capabilities = projection.capabilities;
  return source;
}

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
  EXPECT_EQ(MediaMiniPlayerChromiumAdapter::PlaybackStateFor(
                media_session::mojom::MediaPlaybackState::kPlaying),
            MediaMiniPlayerPlaybackState::kPlaying);
  EXPECT_EQ(MediaMiniPlayerChromiumAdapter::PlaybackStateFor(
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

TEST(MediaMiniPlayerChromiumAdapterTest, TransientChimeIsNotAMediaSource) {
  const Projection projection = Project(TransientChime());
  EXPECT_EQ(projection.playback, MediaMiniPlayerPlaybackState::kPaused);
  EXPECT_FALSE(projection.expose_position);
  EXPECT_FALSE(projection.keeps_controllable_card);
  EXPECT_EQ(projection.capabilities, MediaMiniPlayerCapabilities());
  EXPECT_FALSE(SourceFor("tab-b", 1, projection).IsRelevant());
}

TEST(MediaMiniPlayerChromiumAdapterTest, EndedChimeSessionStaysIrrelevant) {
  // Once the short sound ends Chromium drops audio focus: the session is
  // inactive and paused, but its info remains known.
  Signals signals = TransientChime();
  signals.session_playback = MediaMiniPlayerPlaybackState::kPaused;
  signals.tab_audible = false;
  const Projection projection = Project(signals);
  EXPECT_EQ(projection.playback, MediaMiniPlayerPlaybackState::kPaused);
  EXPECT_FALSE(projection.capabilities.can_mute);
  EXPECT_FALSE(SourceFor("tab-b", 1, projection).IsRelevant());
}

TEST(MediaMiniPlayerChromiumAdapterTest,
     TransientChimeDoesNotTakeSelectionFromPausedPlayer) {
  MediaMiniPlayerService service;
  const Projection music =
      Project(ControllableSession(MediaMiniPlayerPlaybackState::kPaused));
  ASSERT_TRUE(service.RegisterSource(SourceFor("tab-a", 0, music)));
  ASSERT_TRUE(service.RegisterSource(SourceFor("tab-b", 1, Projection())));
  ASSERT_EQ(service.state().selected_source, "tab-a");

  service.UpdateSource(SourceFor("tab-b", 1, Project(TransientChime())));
  EXPECT_EQ(service.state().selected_source, "tab-a");
  EXPECT_FALSE(service.HasMultipleRelevantSources());

  Signals ended = TransientChime();
  ended.session_playback = MediaMiniPlayerPlaybackState::kPaused;
  ended.tab_audible = false;
  service.UpdateSource(SourceFor("tab-b", 1, Project(ended)));
  EXPECT_EQ(service.state().selected_source, "tab-a");
  EXPECT_FALSE(service.HasMultipleRelevantSources());
}

TEST(MediaMiniPlayerChromiumAdapterTest,
     ControllablePlayerStillTakesSelection) {
  MediaMiniPlayerService service;
  const Projection paused =
      Project(ControllableSession(MediaMiniPlayerPlaybackState::kPaused));
  ASSERT_TRUE(service.RegisterSource(SourceFor("tab-a", 0, paused)));
  ASSERT_TRUE(service.RegisterSource(SourceFor("tab-b", 1, Projection())));

  const Projection playing =
      Project(ControllableSession(MediaMiniPlayerPlaybackState::kPlaying));
  EXPECT_EQ(playing.playback, MediaMiniPlayerPlaybackState::kPlaying);
  EXPECT_TRUE(playing.capabilities.can_play_pause);
  EXPECT_TRUE(playing.capabilities.can_seek);
  service.UpdateSource(SourceFor("tab-b", 1, playing));
  EXPECT_EQ(service.state().selected_source, "tab-b");
}

TEST(MediaMiniPlayerChromiumAdapterTest, PagePlayHandlerMakesSessionOwnCard) {
  // A page's own play handler keeps its session controllable even without
  // Chromium's kGain audio focus.
  Signals signals;
  signals.has_session_info = true;
  signals.actions = {Action::kPlay};
  const Projection projection = Project(signals);
  EXPECT_TRUE(projection.capabilities.can_play_pause);
  EXPECT_TRUE(projection.keeps_controllable_card);
  EXPECT_TRUE(SourceFor("tab-a", 0, projection).IsRelevant());
}

TEST(MediaMiniPlayerChromiumAdapterTest, MutedControllableSessionKeepsCard) {
  Signals signals = ControllableSession(MediaMiniPlayerPlaybackState::kPlaying);
  const Projection before = Project(signals);
  ASSERT_TRUE(before.keeps_controllable_card);

  // Muting the tab removes its players from the MediaSession, so Chromium
  // reports it inactive, not controllable and without play/pause actions.
  signals.session_controllable = false;
  signals.session_playback = MediaMiniPlayerPlaybackState::kPaused;
  signals.actions.clear();
  signals.tab_muted = true;
  signals.was_controllable = before.keeps_controllable_card;
  const Projection muted = Project(signals);
  EXPECT_TRUE(muted.keeps_controllable_card);
  EXPECT_TRUE(muted.is_muted);
  EXPECT_TRUE(muted.capabilities.can_mute);
  EXPECT_FALSE(muted.capabilities.can_play_pause);
  EXPECT_TRUE(SourceFor("tab-a", 0, muted).IsRelevant());

  // The card survives further updates while the tab stays muted.
  signals.was_controllable = muted.keeps_controllable_card;
  EXPECT_TRUE(Project(signals).keeps_controllable_card);

  // Unmuted without a player to control, the card is released.
  signals.tab_muted = false;
  const Projection released = Project(signals);
  EXPECT_FALSE(released.keeps_controllable_card);
  EXPECT_FALSE(SourceFor("tab-a", 0, released).IsRelevant());
}

TEST(MediaMiniPlayerChromiumAdapterTest, MutedChimeDoesNotGainCard) {
  Signals signals = TransientChime();
  signals.tab_muted = true;
  const Projection projection = Project(signals);
  EXPECT_FALSE(projection.keeps_controllable_card);
  EXPECT_FALSE(projection.capabilities.can_mute);
  EXPECT_FALSE(SourceFor("tab-b", 1, projection).IsRelevant());
}

TEST(MediaMiniPlayerChromiumAdapterTest, AudibleTabWithoutSessionPlays) {
  // Web Audio and other sound without a MediaSession keeps the audibility
  // fallback.
  Signals signals;
  signals.tab_audible = true;
  const Projection projection = Project(signals);
  EXPECT_EQ(projection.playback, MediaMiniPlayerPlaybackState::kPlaying);
  EXPECT_TRUE(projection.capabilities.can_mute);
  EXPECT_TRUE(SourceFor("tab-c", 2, projection).IsRelevant());

  // Muting that tab keeps its card even once it reads as inaudible.
  signals.tab_audible = false;
  signals.tab_muted = true;
  signals.was_controllable = projection.keeps_controllable_card;
  const Projection muted = Project(signals);
  EXPECT_EQ(muted.playback, MediaMiniPlayerPlaybackState::kPaused);
  EXPECT_TRUE(muted.capabilities.can_mute);
  EXPECT_TRUE(SourceFor("tab-c", 2, muted).IsRelevant());
}

}  // namespace
}  // namespace ahoi
