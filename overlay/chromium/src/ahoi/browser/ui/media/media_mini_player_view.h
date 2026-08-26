// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_VIEW_H_
#define AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/media/media_mini_player_service.h"
#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/controls/slider.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class ImageButton;
class ImageView;
class Label;
}  // namespace views

namespace ahoi::media_ui {

// Localized labels are supplied by the host.  This keeps the isolated view
// free from browser-wide resource IDs while allowing every host to use its
// normal translation catalog.
struct MediaMiniPlayerStrings {
  std::u16string accessible_name;
  std::u16string play;
  std::u16string pause;
  std::u16string mute;
  std::u16string unmute;
  std::u16string picture_in_picture;
  std::u16string exit_picture_in_picture;
  std::u16string previous_source;
  std::u16string next_source;
  std::u16string expand;
  std::u16string collapse;
  std::u16string seek;
};

// The host owns browser placement and persistence of the compact/expanded
// presentation. The view never reaches into BrowserView or WebContents.
class MediaMiniPlayerHost {
 public:
  virtual ~MediaMiniPlayerHost() = default;
  virtual void OnMiniPlayerExpandedChanged(bool expanded) = 0;
};

class MediaMiniPlayerView final : public views::View,
                                  public views::SliderListener,
                                  public views::WidgetObserver {
  METADATA_HEADER(MediaMiniPlayerView, views::View)

 public:
  enum class ViewMode {
    kHidden,
    kCompact,
    kExpanded,
  };

  // The returned view observes `service` until it is destroyed. `service` and
  // the host, when present, must outlive the returned view; neither is owned
  // here.
  static std::unique_ptr<MediaMiniPlayerView> Create(
      MediaMiniPlayerService& service,
      MediaMiniPlayerHost* host,
      MediaMiniPlayerStrings strings,
      base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
          icon_provider = {});

  MediaMiniPlayerView(const MediaMiniPlayerView&) = delete;
  MediaMiniPlayerView& operator=(const MediaMiniPlayerView&) = delete;
  ~MediaMiniPlayerView() override;

  void SetViewMode(ViewMode mode);
  ViewMode view_mode() const { return view_mode_; }

  // Hosts can reapply the shared appearance resolver after a policy/theme
  // change. This keeps the view independent from policy lifetime/observers.
  void SetSurfaceAppearance(appearance::SurfaceAppearance appearance);

  // Favicon changes are owned by Chromium's tab UI rather than MediaSession.
  // The browser host calls this after a tab presentation change so the player
  // can update its decoration without manufacturing a media-state mutation.
  void RefreshSourceDecoration();

  // Exposed for focused native UI tests and host smoke tests.  Ownership
  // remains with this view hierarchy.
  views::Label* title_for_testing() const { return title_; }
  views::Label* origin_for_testing() const { return origin_; }
  views::ImageView* favicon_for_testing() const { return favicon_; }
  views::Slider* scrubber_for_testing() const { return scrubber_; }
  views::ImageButton* play_button_for_testing() const { return play_button_; }
  views::ImageButton* mute_button_for_testing() const { return mute_button_; }
  views::ImageButton* picture_in_picture_button_for_testing() const {
    return picture_in_picture_button_;
  }
  views::ImageButton* previous_button_for_testing() const {
    return previous_button_;
  }
  views::ImageButton* next_button_for_testing() const { return next_button_; }
  views::ImageButton* expand_button_for_testing() const {
    return expand_button_;
  }
  bool progress_timer_running_for_testing() const {
    return progress_timer_.IsRunning();
  }

 private:
  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

  // views::WidgetObserver:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  MediaMiniPlayerView(
      MediaMiniPlayerService& service,
      MediaMiniPlayerHost* host,
      MediaMiniPlayerStrings strings,
      base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
          icon_provider);

  void OnStateChanged(const MediaMiniPlayerState& state);
  void Refresh(const MediaMiniPlayerState& state);
  void RefreshControls(const MediaMiniPlayerSource* source);
  void RefreshVisibility(const MediaMiniPlayerState& state,
                         const MediaMiniPlayerSource* source);
  void RefreshPlayButton(const MediaMiniPlayerSource* source);
  void RefreshMuteButton(const MediaMiniPlayerSource* source);
  void RefreshPictureInPictureButton(const MediaMiniPlayerSource* source);
  void RefreshProgress();
  void UpdateProgressTimer();
  void OnPlayPausePressed();
  void OnMutePressed();
  void OnPictureInPicturePressed();
  void OnPreviousPressed();
  void OnNextPressed();
  void OnExpandPressed();

  // views::SliderListener:
  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override;

  raw_ptr<MediaMiniPlayerService> service_;
  raw_ptr<MediaMiniPlayerHost> host_;
  const MediaMiniPlayerStrings strings_;
  base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
      icon_provider_;
  appearance::SurfaceAppearance surface_appearance_;
  base::CallbackListSubscription state_subscription_;
  ViewMode view_mode_ = ViewMode::kCompact;
  std::optional<MediaMiniPlayerSourceId> selected_source_id_;

  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> origin_ = nullptr;
  raw_ptr<views::ImageButton> play_button_ = nullptr;
  raw_ptr<views::ImageButton> mute_button_ = nullptr;
  raw_ptr<views::ImageButton> picture_in_picture_button_ = nullptr;
  raw_ptr<views::ImageButton> previous_button_ = nullptr;
  raw_ptr<views::ImageButton> next_button_ = nullptr;
  raw_ptr<views::ImageButton> expand_button_ = nullptr;
  raw_ptr<views::Slider> scrubber_ = nullptr;
  base::RepeatingTimer progress_timer_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

// A named factory is the only seam required by BrowserView/sidebar hosts.  It
// keeps construction out of those surfaces and makes the mini player easy to
// replace with a different presentation later.
class MediaMiniPlayerViewFactory {
 public:
  static std::unique_ptr<MediaMiniPlayerView> Create(
      MediaMiniPlayerService& service,
      MediaMiniPlayerHost* host,
      MediaMiniPlayerStrings strings,
      base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
          icon_provider = {}) {
    return MediaMiniPlayerView::Create(service, host, std::move(strings),
                                       std::move(icon_provider));
  }
};

}  // namespace ahoi::media_ui

#endif  // AHOI_BROWSER_UI_MEDIA_MEDIA_MINI_PLAYER_VIEW_H_
