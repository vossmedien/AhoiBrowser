// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/media_mini_player_service.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

MediaMiniPlayerSource MakeSource(const char* id) {
  MediaMiniPlayerSource source;
  source.id = id;
  source.title = u"Test stream";
  source.origin = url::Origin::Create(GURL("https://example.test"));
  source.playback = MediaMiniPlayerPlaybackState::kPaused;
  source.duration = base::Seconds(120);
  source.capabilities = {
      .can_play_pause = true,
      .can_mute = true,
      .can_picture_in_picture = true,
      .can_seek = true,
  };
  return source;
}

TEST(MediaMiniPlayerSourceTest, ProjectsPlayingPositionWithoutServiceTimer) {
  MediaMiniPlayerSource source = MakeSource("tab-a");
  source.playback = MediaMiniPlayerPlaybackState::kPlaying;
  source.position = base::Seconds(20);
  source.duration = base::Seconds(30);
  source.playback_rate = 2.0;
  source.position_updated_at = base::TimeTicks() + base::Seconds(10);

  EXPECT_EQ(base::Seconds(26),
            source.PositionAt(source.position_updated_at + base::Seconds(3)));
  EXPECT_EQ(source.duration,
            source.PositionAt(source.position_updated_at + base::Minutes(1)));
  source.playback = MediaMiniPlayerPlaybackState::kPaused;
  EXPECT_EQ(base::Seconds(20),
            source.PositionAt(source.position_updated_at + base::Minutes(1)));
}

class FakeActionAdapter final : public MediaMiniPlayerActionAdapter {
 public:
  bool SetPlaying(const MediaMiniPlayerSourceId& source_id,
                  bool playing) override {
    playing_calls.emplace_back(source_id, playing);
    return accept_actions;
  }

  bool SetMuted(const MediaMiniPlayerSourceId& source_id, bool muted) override {
    muted_calls.emplace_back(source_id, muted);
    return accept_actions;
  }

  bool SetPictureInPicture(const MediaMiniPlayerSourceId& source_id,
                           bool in_picture_in_picture) override {
    pip_calls.emplace_back(source_id, in_picture_in_picture);
    return accept_actions;
  }

  bool Seek(const MediaMiniPlayerSourceId& source_id,
            base::TimeDelta position) override {
    seek_calls.emplace_back(source_id, position);
    return accept_actions;
  }

  bool accept_actions = true;
  std::vector<std::pair<MediaMiniPlayerSourceId, bool>> playing_calls;
  std::vector<std::pair<MediaMiniPlayerSourceId, bool>> muted_calls;
  std::vector<std::pair<MediaMiniPlayerSourceId, bool>> pip_calls;
  std::vector<std::pair<MediaMiniPlayerSourceId, base::TimeDelta>> seek_calls;
};

class ServiceStateObserver final {
 public:
  void OnStateChanged(const MediaMiniPlayerState&) { ++notifications; }

  int notifications = 0;
};

TEST(MediaMiniPlayerServiceTest, KeepsExplicitSelectionAndSortedSources) {
  MediaMiniPlayerService service;
  EXPECT_TRUE(service.RegisterSource(MakeSource("tab-b")));
  EXPECT_TRUE(service.RegisterSource(MakeSource("tab-a")));

  ASSERT_TRUE(service.state().selected_source.has_value());
  EXPECT_EQ(*service.state().selected_source, "tab-b");
  ASSERT_EQ(service.state().sources.size(), 2u);
  EXPECT_EQ(service.state().sources[0].id, "tab-a");
  EXPECT_EQ(service.state().sources[1].id, "tab-b");

  MediaMiniPlayerSource updated = MakeSource("tab-b");
  updated.title = u"Updated stream";
  EXPECT_TRUE(service.UpdateSource(std::move(updated)));
  EXPECT_EQ(*service.state().selected_source, "tab-b");
  EXPECT_EQ(service.state().sources[1].title, u"Updated stream");
}

TEST(MediaMiniPlayerServiceTest, RemovedSelectionUsesDeterministicFallback) {
  MediaMiniPlayerService service;
  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-c")));
  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-a")));
  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-b")));

  EXPECT_TRUE(service.UnregisterSource("tab-c"));
  ASSERT_TRUE(service.state().selected_source.has_value());
  EXPECT_EQ(*service.state().selected_source, "tab-a");
  EXPECT_FALSE(service.UnregisterSource("missing"));
}

TEST(MediaMiniPlayerServiceTest, SourceNavigationUsesBrowserPresentationOrder) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource third = MakeSource("tab-1");
  third.presentation_order = 2;
  MediaMiniPlayerSource first = MakeSource("tab-9");
  first.presentation_order = 0;
  MediaMiniPlayerSource second = MakeSource("tab-5");
  second.presentation_order = 1;
  ASSERT_TRUE(service.RegisterSource(std::move(third)));
  ASSERT_TRUE(service.RegisterSource(std::move(first)));
  ASSERT_TRUE(service.RegisterSource(std::move(second)));

  ASSERT_EQ(service.state().sources.size(), 3u);
  EXPECT_EQ(service.state().sources[0].id, "tab-9");
  EXPECT_EQ(service.state().sources[1].id, "tab-5");
  EXPECT_EQ(service.state().sources[2].id, "tab-1");
  ASSERT_TRUE(service.SelectSource("tab-9"));
  EXPECT_TRUE(service.SelectNextSource());
  EXPECT_EQ(*service.state().selected_source, "tab-5");
}

TEST(MediaMiniPlayerServiceTest,
     SelectsRelevantSourcesAndCyclesDeterministically) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource dormant = MakeSource("tab-a");
  dormant.capabilities = {};
  dormant.duration = base::TimeDelta();
  ASSERT_TRUE(service.RegisterSource(std::move(dormant)));
  EXPECT_FALSE(service.state().sources[0].IsRelevant());

  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-b")));
  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-c")));
  EXPECT_EQ(*service.state().selected_source, "tab-b");
  EXPECT_TRUE(service.HasMultipleRelevantSources());

  EXPECT_TRUE(service.SelectNextSource());
  EXPECT_EQ(*service.state().selected_source, "tab-c");
  EXPECT_TRUE(service.SelectPreviousSource());
  EXPECT_EQ(*service.state().selected_source, "tab-b");
  EXPECT_TRUE(service.SelectPreviousSource());
  EXPECT_EQ(*service.state().selected_source, "tab-c");
  auto stopped = MakeSource("tab-c");
  stopped.playback = MediaMiniPlayerPlaybackState::kPaused;
  stopped.capabilities = {};
  stopped.duration = base::TimeDelta();
  ASSERT_TRUE(service.UpdateSource(std::move(stopped)));
  EXPECT_EQ(*service.state().selected_source, "tab-b");
  EXPECT_FALSE(service.HasSource("missing"));
}

TEST(MediaMiniPlayerServiceTest, FollowsTheSourceThatStartsPlaying) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource first = MakeSource("tab-a");
  MediaMiniPlayerSource second = MakeSource("tab-b");
  second.presentation_order = 1;
  ASSERT_TRUE(service.RegisterSource(first));
  ASSERT_TRUE(service.RegisterSource(second));
  ASSERT_EQ("tab-a", *service.state().selected_source);

  second.playback = MediaMiniPlayerPlaybackState::kPlaying;
  ASSERT_TRUE(service.UpdateSource(second));
  EXPECT_EQ("tab-b", *service.state().selected_source);

  // A second playing source must not steal explicit control from the source
  // that is already producing media.
  first.playback = MediaMiniPlayerPlaybackState::kPlaying;
  ASSERT_TRUE(service.UpdateSource(first));
  EXPECT_EQ("tab-b", *service.state().selected_source);

  // Once the selected source pauses, the remaining playing source becomes
  // authoritative without unregistering either tab.
  second.playback = MediaMiniPlayerPlaybackState::kPaused;
  ASSERT_TRUE(service.UpdateSource(second));
  EXPECT_EQ("tab-a", *service.state().selected_source);
}

TEST(MediaMiniPlayerServiceTest, DoesNotNotifyForEquivalentChanges) {
  MediaMiniPlayerService service;
  ServiceStateObserver observer;
  auto subscription = service.AddStateChangedCallback(base::BindRepeating(
      &ServiceStateObserver::OnStateChanged, base::Unretained(&observer)));

  MediaMiniPlayerSource source = MakeSource("tab-a");
  ASSERT_TRUE(service.RegisterSource(source));
  EXPECT_FALSE(service.UpdateSource(source));
  EXPECT_FALSE(service.SelectSource("tab-a"));
  EXPECT_EQ(observer.notifications, 1);
}

TEST(MediaMiniPlayerServiceTest, DispatchesCapabilitiesThroughAdapter) {
  FakeActionAdapter adapter;
  MediaMiniPlayerService service(&adapter);
  MediaMiniPlayerSource source = MakeSource("tab-a");
  ASSERT_TRUE(service.RegisterSource(source));

  EXPECT_TRUE(service.DispatchPlayPause("tab-a"));
  ASSERT_EQ(adapter.playing_calls.size(), 1u);
  EXPECT_EQ(adapter.playing_calls[0],
            std::make_pair(MediaMiniPlayerSourceId("tab-a"), true));

  source.playback = MediaMiniPlayerPlaybackState::kPlaying;
  source.is_muted = true;
  source.is_in_picture_in_picture = true;
  ASSERT_TRUE(service.UpdateSource(source));
  EXPECT_TRUE(service.DispatchPlayPause("tab-a"));
  EXPECT_TRUE(service.DispatchMute("tab-a"));
  EXPECT_TRUE(service.DispatchPictureInPicture("tab-a"));
  EXPECT_TRUE(service.DispatchSeek("tab-a", base::Seconds(42)));
  EXPECT_TRUE(service.DispatchSeek("tab-a", base::Seconds(999)));
  EXPECT_FALSE(service.DispatchSeek("tab-a", base::Seconds(-1)));

  ASSERT_EQ(adapter.playing_calls.size(), 2u);
  EXPECT_EQ(adapter.playing_calls[1],
            std::make_pair(MediaMiniPlayerSourceId("tab-a"), false));
  ASSERT_EQ(adapter.muted_calls.size(), 1u);
  EXPECT_EQ(adapter.muted_calls[0],
            std::make_pair(MediaMiniPlayerSourceId("tab-a"), false));
  ASSERT_EQ(adapter.pip_calls.size(), 1u);
  EXPECT_EQ(adapter.pip_calls[0],
            std::make_pair(MediaMiniPlayerSourceId("tab-a"), false));
  ASSERT_EQ(adapter.seek_calls.size(), 2u);
  EXPECT_EQ(adapter.seek_calls[0].first, "tab-a");
  EXPECT_EQ(adapter.seek_calls[0].second, base::Seconds(42));
  EXPECT_EQ(adapter.seek_calls[1].second, source.duration);
}

TEST(MediaMiniPlayerServiceTest, RejectsUnavailableActions) {
  FakeActionAdapter adapter;
  MediaMiniPlayerService service(&adapter);
  MediaMiniPlayerSource source = MakeSource("tab-a");
  source.capabilities = {};
  ASSERT_TRUE(service.RegisterSource(source));

  EXPECT_FALSE(service.DispatchPlayPause("tab-a"));
  EXPECT_FALSE(service.DispatchMute("tab-a"));
  EXPECT_FALSE(service.DispatchPictureInPicture("tab-a"));
  EXPECT_FALSE(service.DispatchSeek("tab-a", base::Seconds(1)));
  EXPECT_TRUE(adapter.playing_calls.empty());
  EXPECT_FALSE(service.DispatchPlayPause("missing"));
}

TEST(MediaMiniPlayerServiceTest, SelectedSourceDrivesPipPresentation) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource selected = MakeSource("tab-a");
  MediaMiniPlayerSource other = MakeSource("tab-b");
  ASSERT_TRUE(service.RegisterSource(selected));
  ASSERT_TRUE(service.RegisterSource(other));

  other.is_in_picture_in_picture = true;
  ASSERT_TRUE(service.UpdateSource(other));
  EXPECT_EQ(service.state().display_mode,
            MediaMiniPlayerDisplayMode::kMiniPlayer);

  ASSERT_TRUE(service.SelectSource("tab-b"));
  EXPECT_EQ(service.state().display_mode,
            MediaMiniPlayerDisplayMode::kPictureInPicture);

  other.is_in_picture_in_picture = false;
  ASSERT_TRUE(service.UpdateSource(other));
  EXPECT_EQ(service.state().display_mode,
            MediaMiniPlayerDisplayMode::kMiniPlayer);
  EXPECT_EQ(*service.state().selected_source, "tab-b");
}

TEST(MediaMiniPlayerServiceTest, RejectsEmptyAndDuplicateSourceIds) {
  MediaMiniPlayerService service;
  EXPECT_FALSE(service.RegisterSource(MakeSource("")));
  ASSERT_TRUE(service.RegisterSource(MakeSource("tab-a")));
  EXPECT_FALSE(service.RegisterSource(MakeSource("tab-a")));
  EXPECT_FALSE(service.UpdateSource(MakeSource("missing")));
}

}  // namespace
}  // namespace ahoi
