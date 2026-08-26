// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/media/media_mini_player_view.h"

#include <memory>
#include <utility>

#include "ahoi/browser/ui/media/media_mini_player_surface.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "components/vector_icons/vector_icons.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace ahoi::media_ui {

namespace {

MediaMiniPlayerStrings TestStrings() {
  return {
      .accessible_name = u"Media mini player",
      .play = u"Play",
      .pause = u"Pause",
      .mute = u"Mute",
      .unmute = u"Unmute",
      .picture_in_picture = u"Enter picture-in-picture",
      .exit_picture_in_picture = u"Exit picture-in-picture",
      .previous_source = u"Previous media source",
      .next_source = u"Next media source",
      .expand = u"Show seek controls",
      .collapse = u"Hide seek controls",
      .seek = u"Seek media",
  };
}

class RecordingAdapter final : public MediaMiniPlayerActionAdapter {
 public:
  bool SetPlaying(const MediaMiniPlayerSourceId& source_id,
                  bool playing) override {
    last_source_id = source_id;
    last_playing = playing;
    return true;
  }

  bool SetMuted(const MediaMiniPlayerSourceId& source_id, bool muted) override {
    last_source_id = source_id;
    last_muted = muted;
    return true;
  }

  bool SetPictureInPicture(const MediaMiniPlayerSourceId& source_id,
                           bool in_picture_in_picture) override {
    last_source_id = source_id;
    last_picture_in_picture = in_picture_in_picture;
    return true;
  }

  bool Seek(const MediaMiniPlayerSourceId& source_id,
            base::TimeDelta position) override {
    last_source_id = source_id;
    last_seek = position;
    return true;
  }

  MediaMiniPlayerSourceId last_source_id;
  bool last_playing = false;
  bool last_muted = false;
  bool last_picture_in_picture = false;
  base::TimeDelta last_seek;
};

MediaMiniPlayerSource MakeSource() {
  MediaMiniPlayerSource source;
  source.id = "tab-1";
  source.title = u"A deliberately long title that the view must elide";
  source.origin = url::Origin::Create(GURL("https://example.test"));
  source.playback = MediaMiniPlayerPlaybackState::kPlaying;
  source.position = base::Seconds(30);
  source.duration = base::Minutes(2);
  source.capabilities = {.can_play_pause = true,
                         .can_mute = true,
                         .can_picture_in_picture = true,
                         .can_seek = true};
  return source;
}

class MediaMiniPlayerViewTest : public views::ViewsTestBase {};

TEST_F(MediaMiniPlayerViewTest, EmptyServiceStartsHidden) {
  MediaMiniPlayerService service;
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  EXPECT_FALSE(view->GetVisible());
  EXPECT_EQ(MediaMiniPlayerView::ViewMode::kCompact, view->view_mode());
}

TEST_F(MediaMiniPlayerViewTest, DormantRegisteredTabDoesNotShowPlayer) {
  MediaMiniPlayerService service;
  auto source = MakeSource();
  source.playback = MediaMiniPlayerPlaybackState::kPaused;
  source.capabilities = {};
  source.duration = base::TimeDelta();
  EXPECT_FALSE(source.IsRelevant());
  ASSERT_TRUE(service.RegisterSource(std::move(source)));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  EXPECT_FALSE(view->GetVisible());
}

TEST_F(MediaMiniPlayerViewTest, StateBindsTitleOriginAndExpandableScrubber) {
  MediaMiniPlayerService service;
  ASSERT_TRUE(service.RegisterSource(MakeSource()));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  EXPECT_TRUE(view->GetVisible());
  EXPECT_EQ(u"A deliberately long title that the view must elide",
            view->title_for_testing()->GetText());
  EXPECT_EQ(u"https://example.test", view->origin_for_testing()->GetText());
  EXPECT_FALSE(view->scrubber_for_testing()->GetVisible());

  view->SetViewMode(MediaMiniPlayerView::ViewMode::kExpanded);
  EXPECT_TRUE(view->GetVisible());
  EXPECT_TRUE(view->scrubber_for_testing()->GetVisible());
  EXPECT_NEAR(0.25f, view->scrubber_for_testing()->GetValue(), 0.001f);

  MediaMiniPlayerSource updated = MakeSource();
  updated.title = u"Updated title";
  updated.capabilities.can_seek = false;
  ASSERT_TRUE(service.UpdateSource(std::move(updated)));
  EXPECT_EQ(u"Updated title", view->title_for_testing()->GetText());
  EXPECT_FALSE(view->scrubber_for_testing()->GetVisible());
}

TEST_F(MediaMiniPlayerViewTest, UsesHostFaviconAndRefreshesDecoration) {
  MediaMiniPlayerService service;
  ASSERT_TRUE(service.RegisterSource(MakeSource()));
  bool use_host_favicon = true;
  auto view = MediaMiniPlayerView::Create(
      service, nullptr, TestStrings(),
      base::BindRepeating(
          [](bool* use_host_favicon,
             const MediaMiniPlayerSourceId& source_id) -> ui::ImageModel {
            EXPECT_EQ("tab-1", source_id);
            return *use_host_favicon ? ui::ImageModel::FromVectorIcon(
                                           vector_icons::kSettingsIcon,
                                           visual_style::kAccent, 16)
                                     : ui::ImageModel();
          },
          &use_host_favicon));

  EXPECT_TRUE(view->favicon_for_testing()->GetImageModel().IsVectorIcon());
  EXPECT_EQ(&vector_icons::kSettingsIcon, view->favicon_for_testing()
                                              ->GetImageModel()
                                              .GetVectorIcon()
                                              .vector_icon());

  use_host_favicon = false;
  view->RefreshSourceDecoration();
  EXPECT_EQ(&vector_icons::kPlayArrowIcon, view->favicon_for_testing()
                                               ->GetImageModel()
                                               .GetVectorIcon()
                                               .vector_icon());
}

TEST_F(MediaMiniPlayerViewTest, UsesClearFallbackForOpaqueUntitledSource) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource source = MakeSource();
  source.title.clear();
  source.origin = url::Origin();
  ASSERT_TRUE(service.RegisterSource(std::move(source)));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  EXPECT_EQ(u"Media mini player", view->title_for_testing()->GetText());
  EXPECT_TRUE(view->origin_for_testing()->GetText().empty());
}

TEST_F(MediaMiniPlayerViewTest, ButtonsDispatchThroughServiceAdapter) {
  RecordingAdapter adapter;
  MediaMiniPlayerService service(&adapter);
  ASSERT_TRUE(service.RegisterSource(MakeSource()));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  views::test::ButtonTestApi(view->play_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ("tab-1", adapter.last_source_id);
  EXPECT_FALSE(adapter.last_playing);

  views::test::ButtonTestApi(view->mute_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(adapter.last_muted);

  views::test::ButtonTestApi(view->picture_in_picture_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(adapter.last_picture_in_picture);
}

TEST_F(MediaMiniPlayerViewTest, ShowsSourceNavigationOnlyForMultipleSources) {
  MediaMiniPlayerService service;
  ASSERT_TRUE(service.RegisterSource(MakeSource()));
  auto second = MakeSource();
  second.id = "tab-2";
  ASSERT_TRUE(service.RegisterSource(std::move(second)));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  EXPECT_TRUE(view->previous_button_for_testing()->GetVisible());
  EXPECT_TRUE(view->next_button_for_testing()->GetVisible());
  views::test::ButtonTestApi(view->next_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ("tab-2", *service.state().selected_source);

  ASSERT_TRUE(service.UnregisterSource("tab-2"));
  EXPECT_FALSE(view->previous_button_for_testing()->GetVisible());
  EXPECT_FALSE(view->next_button_for_testing()->GetVisible());
}

TEST_F(MediaMiniPlayerViewTest, ExpandButtonMakesSeekReachable) {
  MediaMiniPlayerService service;
  ASSERT_TRUE(service.RegisterSource(MakeSource()));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());
  ASSERT_FALSE(view->scrubber_for_testing()->GetVisible());

  views::test::ButtonTestApi(view->expand_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(MediaMiniPlayerView::ViewMode::kExpanded, view->view_mode());
  EXPECT_TRUE(view->scrubber_for_testing()->GetVisible());

  views::test::ButtonTestApi(view->expand_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(MediaMiniPlayerView::ViewMode::kCompact, view->view_mode());
  EXPECT_FALSE(view->scrubber_for_testing()->GetVisible());
}

TEST_F(MediaMiniPlayerViewTest, ProgressTimerOnlyRunsForVisibleExpandedMedia) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource source = MakeSource();
  source.playback_rate = 1.0;
  source.position_updated_at = base::TimeTicks::Now();
  ASSERT_TRUE(service.RegisterSource(std::move(source)));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());
  MediaMiniPlayerView* const view_ptr = view.get();
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetContentsView(std::move(view));
  widget->Show();

  EXPECT_FALSE(view_ptr->progress_timer_running_for_testing());
  view_ptr->SetViewMode(MediaMiniPlayerView::ViewMode::kExpanded);
  EXPECT_TRUE(view_ptr->progress_timer_running_for_testing());
  view_ptr->SetViewMode(MediaMiniPlayerView::ViewMode::kCompact);
  EXPECT_FALSE(view_ptr->progress_timer_running_for_testing());
  view_ptr->SetViewMode(MediaMiniPlayerView::ViewMode::kExpanded);
  EXPECT_TRUE(view_ptr->progress_timer_running_for_testing());
  widget->Hide();
  EXPECT_FALSE(view_ptr->progress_timer_running_for_testing());
  widget->Show();
  EXPECT_TRUE(view_ptr->progress_timer_running_for_testing());
}

TEST_F(MediaMiniPlayerViewTest, PipPresentationHidesMiniPlayer) {
  MediaMiniPlayerService service;
  MediaMiniPlayerSource source = MakeSource();
  ASSERT_TRUE(service.RegisterSource(source));
  auto view = MediaMiniPlayerView::Create(service, nullptr, TestStrings());

  source.is_in_picture_in_picture = true;
  ASSERT_TRUE(service.UpdateSource(source));
  EXPECT_FALSE(view->GetVisible());
  source.is_in_picture_in_picture = false;
  ASSERT_TRUE(service.UpdateSource(source));
  EXPECT_TRUE(view->GetVisible());

  view->SetViewMode(MediaMiniPlayerView::ViewMode::kHidden);
  EXPECT_FALSE(view->GetVisible());
}

TEST(MediaMiniPlayerSurfaceTest, MiniPlayerUsesSemanticRaisedSurface) {
  EXPECT_EQ(appearance::AppearanceResolver::Resolve(
                appearance::SurfaceRole::kMiniPlayer, {})
                .background_color,
            ColorForSurfaceRole(AhoiAppearanceSurfaceRole::kMiniPlayer));
  EXPECT_EQ(appearance::AppearanceResolver::Resolve(
                appearance::SurfaceRole::kMiniPlayer, {})
                .border_color,
            BorderColorForSurfaceRole(AhoiAppearanceSurfaceRole::kMiniPlayer));
}

}  // namespace

}  // namespace ahoi::media_ui
