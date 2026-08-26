// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/media/media_mini_player_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/media/media_mini_player_surface.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace ahoi::media_ui {

namespace {

constexpr int kSurfaceInset = 8;
constexpr int kButtonSize = 24;
constexpr int kIconSize = 16;
constexpr int kTextSpacing = 2;
constexpr int kControlSpacing = 4;
constexpr int kTitleLineHeight = 22;
constexpr int kOriginLineHeight = 18;
constexpr int kScrubberHeight = 18;
constexpr int kSourceIconSize = 20;
constexpr int kSourceIconTrailingInset = 8;

const MediaMiniPlayerSource* FindSelectedSource(
    const MediaMiniPlayerState& state) {
  if (!state.selected_source.has_value()) {
    return nullptr;
  }
  const auto it = std::find_if(
      state.sources.begin(), state.sources.end(), [&](const auto& source) {
        return source.id == *state.selected_source && source.IsRelevant();
      });
  if (it != state.sources.end()) {
    return &*it;
  }
  const auto first_relevant =
      std::find_if(state.sources.begin(), state.sources.end(),
                   [](const auto& source) { return source.IsRelevant(); });
  return first_relevant == state.sources.end() ? nullptr : &*first_relevant;
}

void ConfigureIconButton(views::ImageButton* button,
                         const std::u16string& accessible_name) {
  button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  button->SetMinimumImageSize(gfx::Size(kIconSize, kIconSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetAccessibleName(accessible_name);
  button->SetTooltipText(accessible_name);
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
}

void SetIcon(views::ImageButton* button,
             const gfx::VectorIcon& icon,
             const std::u16string& accessible_name) {
  views::SetImageFromVectorIconWithColor(
      button, icon,
      {visual_style::kText, visual_style::kDisabledIcon, visual_style::kAccent},
      kIconSize);
  button->SetAccessibleName(accessible_name);
  button->SetTooltipText(accessible_name);
}

float PositionFraction(const MediaMiniPlayerSource& source,
                       base::TimeTicks now) {
  if (!source.duration.is_positive()) {
    return 0.0f;
  }
  return std::clamp(static_cast<float>(source.PositionAt(now).InSecondsF() /
                                       source.duration.InSecondsF()),
                    0.0f, 1.0f);
}

}  // namespace

std::unique_ptr<MediaMiniPlayerView> MediaMiniPlayerView::Create(
    MediaMiniPlayerService& service,
    MediaMiniPlayerHost* host,
    MediaMiniPlayerStrings strings,
    base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
        icon_provider) {
  return std::unique_ptr<MediaMiniPlayerView>(new MediaMiniPlayerView(
      service, host, std::move(strings), std::move(icon_provider)));
}

MediaMiniPlayerView::MediaMiniPlayerView(
    MediaMiniPlayerService& service,
    MediaMiniPlayerHost* host,
    MediaMiniPlayerStrings strings,
    base::RepeatingCallback<ui::ImageModel(const MediaMiniPlayerSourceId&)>
        icon_provider)
    : service_(&service),
      host_(host),
      strings_(std::move(strings)),
      icon_provider_(std::move(icon_provider)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kSurfaceInset),
      kControlSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  surface_appearance_ = appearance::AppearanceResolver::Resolve(
      appearance::SurfaceRole::kMiniPlayer, {});
  appearance::ApplySurfaceAppearance(this, surface_appearance_);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(strings_.accessible_name,
                                 ax::mojom::NameFrom::kAttribute);

  auto title_row = std::make_unique<views::View>();
  auto* title_row_layout =
      title_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  title_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_ = title_row->AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(kSourceIconSize, kSourceIconSize));
  favicon_->SetProperty(views::kMarginsKey,
                        gfx::Insets::TLBR(0, 0, 0, kSourceIconTrailingInset));
  favicon_->SetCanProcessEventsWithinSubtree(false);
  favicon_->GetViewAccessibility().SetIsIgnored(true);

  auto text_stack = std::make_unique<views::View>();
  auto* text_layout =
      text_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kTextSpacing));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  title_ = text_stack->AddChildView(std::make_unique<views::Label>());
  title_->SetSubpixelRenderingEnabled(false);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetMaxLines(1);
  title_->SetPreferredSize(gfx::Size(0, kTitleLineHeight));
  title_->SetEnabledColor(visual_style::kText);
  origin_ = text_stack->AddChildView(std::make_unique<views::Label>());
  origin_->SetSubpixelRenderingEnabled(false);
  origin_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  origin_->SetElideBehavior(gfx::ELIDE_TAIL);
  origin_->SetMaxLines(1);
  origin_->SetPreferredSize(gfx::Size(0, kOriginLineHeight));
  origin_->SetEnabledColor(visual_style::kMutedText);
  views::View* const text_stack_ptr =
      title_row->AddChildView(std::move(text_stack));
  title_row_layout->SetFlexForView(text_stack_ptr, 1);

  AddChildView(std::move(title_row));

  auto controls = std::make_unique<views::View>();
  auto* controls_layout =
      controls->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  controls_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  const auto add_control = [&](std::unique_ptr<views::ImageButton> button) {
    views::ImageButton* const button_ptr =
        controls->AddChildView(std::move(button));
    controls_layout->SetFlexForView(button_ptr, 1);
    return button_ptr;
  };

  play_button_ = add_control(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&MediaMiniPlayerView::OnPlayPausePressed,
                          base::Unretained(this)),
      vector_icons::kPlayArrowIcon, kIconSize, visual_style::kText,
      visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureIconButton(play_button_, strings_.play);

  mute_button_ = add_control(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&MediaMiniPlayerView::OnMutePressed,
                          base::Unretained(this)),
      vector_icons::kVolumeUpIcon, kIconSize, visual_style::kText,
      visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureIconButton(mute_button_, strings_.mute);

  picture_in_picture_button_ =
      add_control(views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(&MediaMiniPlayerView::OnPictureInPicturePressed,
                              base::Unretained(this)),
          vector_icons::kPictureInPictureAltIcon, kIconSize,
          visual_style::kText, visual_style::kDisabledIcon,
          visual_style::kAccent));
  ConfigureIconButton(picture_in_picture_button_, strings_.picture_in_picture);

  previous_button_ = add_control(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&MediaMiniPlayerView::OnPreviousPressed,
                          base::Unretained(this)),
      vector_icons::kArrowBackIcon, kIconSize, visual_style::kText,
      visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureIconButton(previous_button_, strings_.previous_source);

  next_button_ = add_control(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&MediaMiniPlayerView::OnNextPressed,
                          base::Unretained(this)),
      vector_icons::kArrowForwardIcon, kIconSize, visual_style::kText,
      visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureIconButton(next_button_, strings_.next_source);

  expand_button_ = add_control(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&MediaMiniPlayerView::OnExpandPressed,
                          base::Unretained(this)),
      vector_icons::kKeyboardArrowDownIcon, kIconSize, visual_style::kText,
      visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureIconButton(expand_button_, strings_.expand);

  AddChildView(std::move(controls));
  scrubber_ = AddChildView(std::make_unique<views::Slider>(this));
  scrubber_->SetPreferredSize(gfx::Size(0, kScrubberHeight));
  scrubber_->SetAccessibleName(strings_.seek);
  scrubber_->SetRenderingStyle(views::Slider::RenderingStyle::kMinimalStyle);

  state_subscription_ = service_->AddStateChangedCallback(base::BindRepeating(
      &MediaMiniPlayerView::OnStateChanged, base::Unretained(this)));
  Refresh(service_->state());
}

MediaMiniPlayerView::~MediaMiniPlayerView() = default;

void MediaMiniPlayerView::AddedToWidget() {
  views::View::AddedToWidget();
  CHECK(!widget_observation_.IsObserving());
  widget_observation_.Observe(GetWidget());
  UpdateProgressTimer();
}

void MediaMiniPlayerView::RemovedFromWidget() {
  widget_observation_.Reset();
  progress_timer_.Stop();
  views::View::RemovedFromWidget();
}

void MediaMiniPlayerView::VisibilityChanged(views::View* starting_from,
                                            bool is_visible) {
  views::View::VisibilityChanged(starting_from, is_visible);
  UpdateProgressTimer();
}

void MediaMiniPlayerView::OnWidgetVisibilityChanged(views::Widget* widget,
                                                    bool visible) {
  CHECK_EQ(widget_observation_.GetSource(), widget);
  if (!visible) {
    progress_timer_.Stop();
    return;
  }
  UpdateProgressTimer();
}

void MediaMiniPlayerView::SetViewMode(ViewMode mode) {
  if (view_mode_ == mode) {
    return;
  }
  view_mode_ = mode;
  Refresh(service_->state());
  PreferredSizeChanged();
}

void MediaMiniPlayerView::SetSurfaceAppearance(
    appearance::SurfaceAppearance appearance) {
  if (surface_appearance_ == appearance) {
    return;
  }
  surface_appearance_ = appearance;
  appearance::ApplySurfaceAppearance(this, surface_appearance_);
}

void MediaMiniPlayerView::RefreshSourceDecoration() {
  Refresh(service_->state());
}

void MediaMiniPlayerView::OnStateChanged(const MediaMiniPlayerState& state) {
  Refresh(state);
}

void MediaMiniPlayerView::Refresh(const MediaMiniPlayerState& state) {
  const MediaMiniPlayerSource* const source = FindSelectedSource(state);
  selected_source_id_ = source
                            ? std::optional<MediaMiniPlayerSourceId>(source->id)
                            : std::nullopt;
  if (source) {
    ui::ImageModel icon =
        icon_provider_ ? icon_provider_.Run(source->id) : ui::ImageModel();
    if (icon.IsEmpty()) {
      icon = ui::ImageModel::FromVectorIcon(vector_icons::kPlayArrowIcon,
                                            visual_style::kMutedText,
                                            kSourceIconSize);
    }
    favicon_->SetImage(std::move(icon));
    const std::u16string serialized_origin =
        source->origin.opaque() ? std::u16string()
                                : base::UTF8ToUTF16(source->origin.Serialize());
    const std::u16string title =
        !source->title.empty()
            ? source->title
            : (!serialized_origin.empty() ? serialized_origin
                                          : strings_.accessible_name);
    title_->SetText(title);
    origin_->SetText(serialized_origin);
  } else {
    favicon_->SetImage(ui::ImageModel());
    title_->SetText(std::u16string());
    origin_->SetText(std::u16string());
  }
  RefreshControls(source);
  RefreshVisibility(state, source);
  UpdateProgressTimer();
}

void MediaMiniPlayerView::RefreshControls(const MediaMiniPlayerSource* source) {
  RefreshPlayButton(source);
  RefreshMuteButton(source);
  RefreshPictureInPictureButton(source);

  play_button_->SetEnabled(source && source->capabilities.can_play_pause);
  mute_button_->SetEnabled(source && source->capabilities.can_mute);
  picture_in_picture_button_->SetEnabled(
      source && source->capabilities.can_picture_in_picture);
  const bool has_multiple_sources = service_->HasMultipleRelevantSources();
  previous_button_->SetVisible(has_multiple_sources);
  next_button_->SetVisible(has_multiple_sources);
  previous_button_->SetEnabled(has_multiple_sources);
  next_button_->SetEnabled(has_multiple_sources);
  SetIcon(
      expand_button_,
      view_mode_ == ViewMode::kExpanded ? vector_icons::kKeyboardArrowUpIcon
                                        : vector_icons::kKeyboardArrowDownIcon,
      view_mode_ == ViewMode::kExpanded ? strings_.collapse : strings_.expand);

  if (!source || !source->capabilities.can_seek) {
    scrubber_->SetValue(0.0f);
    return;
  }
  scrubber_->SetValue(PositionFraction(*source, base::TimeTicks::Now()));
}

void MediaMiniPlayerView::RefreshVisibility(
    const MediaMiniPlayerState& state,
    const MediaMiniPlayerSource* source) {
  const bool media_presentation_visible =
      view_mode_ != ViewMode::kHidden && source &&
      state.display_mode == MediaMiniPlayerDisplayMode::kMiniPlayer;
  SetVisible(media_presentation_visible);
  scrubber_->SetVisible(media_presentation_visible &&
                        view_mode_ == ViewMode::kExpanded && source &&
                        source->capabilities.can_seek);
}

void MediaMiniPlayerView::RefreshPlayButton(
    const MediaMiniPlayerSource* source) {
  const bool playing =
      source && source->playback == MediaMiniPlayerPlaybackState::kPlaying;
  SetIcon(play_button_,
          playing ? vector_icons::kPauseIcon : vector_icons::kPlayArrowIcon,
          playing ? strings_.pause : strings_.play);
}

void MediaMiniPlayerView::RefreshMuteButton(
    const MediaMiniPlayerSource* source) {
  const bool muted = source && source->is_muted;
  SetIcon(mute_button_,
          muted ? vector_icons::kVolumeOffIcon : vector_icons::kVolumeUpIcon,
          muted ? strings_.unmute : strings_.mute);
}

void MediaMiniPlayerView::RefreshPictureInPictureButton(
    const MediaMiniPlayerSource* source) {
  const bool in_picture_in_picture = source && source->is_in_picture_in_picture;
  SetIcon(picture_in_picture_button_,
          in_picture_in_picture ? vector_icons::kPipExitIcon
                                : vector_icons::kPictureInPictureAltIcon,
          in_picture_in_picture ? strings_.exit_picture_in_picture
                                : strings_.picture_in_picture);
}

void MediaMiniPlayerView::RefreshProgress() {
  if (!selected_source_id_) {
    return;
  }
  const auto source = std::ranges::find_if(
      service_->state().sources, [&](const MediaMiniPlayerSource& candidate) {
        return candidate.id == *selected_source_id_;
      });
  if (source == service_->state().sources.end() ||
      !source->duration.is_positive()) {
    return;
  }
  scrubber_->SetValue(PositionFraction(*source, base::TimeTicks::Now()));
}

void MediaMiniPlayerView::UpdateProgressTimer() {
  const auto source =
      selected_source_id_
          ? std::ranges::find_if(service_->state().sources,
                                 [&](const MediaMiniPlayerSource& candidate) {
                                   return candidate.id == *selected_source_id_;
                                 })
          : service_->state().sources.end();
  const bool should_run =
      GetWidget() && GetWidget()->IsVisible() && IsDrawn() && GetVisible() &&
      view_mode_ == ViewMode::kExpanded &&
      source != service_->state().sources.end() &&
      source->playback == MediaMiniPlayerPlaybackState::kPlaying &&
      source->playback_rate > 0.0 && source->capabilities.can_seek &&
      source->duration.is_positive();
  if (!should_run) {
    progress_timer_.Stop();
    return;
  }
  if (!progress_timer_.IsRunning()) {
    progress_timer_.Start(
        FROM_HERE, base::Seconds(1),
        base::BindRepeating(&MediaMiniPlayerView::RefreshProgress,
                            base::Unretained(this)));
  }
}

void MediaMiniPlayerView::OnPlayPausePressed() {
  if (selected_source_id_) {
    service_->DispatchPlayPause(*selected_source_id_);
  }
}

void MediaMiniPlayerView::OnMutePressed() {
  if (selected_source_id_) {
    service_->DispatchMute(*selected_source_id_);
  }
}

void MediaMiniPlayerView::OnPictureInPicturePressed() {
  if (selected_source_id_) {
    service_->DispatchPictureInPicture(*selected_source_id_);
  }
}

void MediaMiniPlayerView::OnPreviousPressed() {
  service_->SelectPreviousSource();
}

void MediaMiniPlayerView::OnNextPressed() {
  service_->SelectNextSource();
}

void MediaMiniPlayerView::OnExpandPressed() {
  const bool expanded = view_mode_ != ViewMode::kExpanded;
  SetViewMode(expanded ? ViewMode::kExpanded : ViewMode::kCompact);
  if (host_) {
    host_->OnMiniPlayerExpandedChanged(expanded);
  }
}

void MediaMiniPlayerView::SliderValueChanged(views::Slider* sender,
                                             float value,
                                             float old_value,
                                             views::SliderChangeReason reason) {
  if (reason != views::SliderChangeReason::kByUser || sender != scrubber_ ||
      !selected_source_id_) {
    return;
  }
  const auto it = std::find_if(
      service_->state().sources.begin(), service_->state().sources.end(),
      [&](const auto& source) { return source.id == *selected_source_id_; });
  if (it == service_->state().sources.end() || !it->duration.is_positive()) {
    return;
  }
  service_->DispatchSeek(
      *selected_source_id_,
      base::Seconds(it->duration.InSecondsF() * std::clamp(value, 0.0f, 1.0f)));
}

BEGIN_METADATA(MediaMiniPlayerView)
END_METADATA

}  // namespace ahoi::media_ui
