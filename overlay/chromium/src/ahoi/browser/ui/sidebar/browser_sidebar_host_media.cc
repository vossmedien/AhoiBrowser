// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/appearance/sidebar_page_tint.h"
#include "ahoi/browser/ui/media/media_mini_player_view.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

media_ui::MediaMiniPlayerStrings GetLocalizedMiniPlayerStrings() {
  return {
      .accessible_name =
          l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_MINI_PLAYER_NAME),
      .play = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_PLAY),
      .pause = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_PAUSE),
      .mute = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_MUTE),
      .unmute = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_UNMUTE),
      .picture_in_picture =
          l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_ENTER_PICTURE_IN_PICTURE),
      .exit_picture_in_picture =
          l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_EXIT_PICTURE_IN_PICTURE),
      .previous_source =
          l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_PREVIOUS_SOURCE),
      .next_source = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_NEXT_SOURCE),
      .expand = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_EXPAND),
      .collapse = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_COLLAPSE),
      .seek = l10n_util::GetStringUTF16(IDS_AHOI_MEDIA_SEEK),
  };
}

}  // namespace

std::unique_ptr<SidebarMediaOverlayView>
BrowserSidebarHostView::CreateMiniPlayerOverlay(
    std::unique_ptr<views::ScrollView> scroll_view,
    views::View* scroll_bottom_inset) {
  mini_player_service_ = std::make_unique<MediaMiniPlayerService>();
  mini_player_adapter_ =
      std::make_unique<MediaMiniPlayerChromiumAdapter>(*mini_player_service_);

  auto mini_player = media_ui::MediaMiniPlayerViewFactory::Create(
      *mini_player_service_, this, GetLocalizedMiniPlayerStrings(),
      base::BindRepeating(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             const MediaMiniPlayerSourceId& source_id) {
            return host ? host->GetMiniPlayerFavicon(source_id)
                        : ui::ImageModel();
          },
          weak_ptr_factory_.GetWeakPtr()));
  mini_player_view_ = mini_player.get();
  mini_player_view_->SetViewMode(
      IsMiniPlayerExpanded(*browser_->GetProfile()->GetPrefs())
          ? media_ui::MediaMiniPlayerView::ViewMode::kExpanded
          : media_ui::MediaMiniPlayerView::ViewMode::kCompact);

  auto overlay = std::make_unique<SidebarMediaOverlayView>(
      std::move(scroll_view), std::move(mini_player), scroll_bottom_inset);
  scroll_view_ = overlay->scroll_view();
  return overlay;
}

void BrowserSidebarHostView::OnMiniPlayerExpandedChanged(bool expanded) {
  (void)SetMiniPlayerExpanded(browser_->GetProfile()->GetPrefs(), expanded);
}

void BrowserSidebarHostView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  reduced_motion_ = policy.reduced_motion;
  high_contrast_ = policy.high_contrast;
  reduced_transparency_ = policy.system_reduce_transparency;
  if (reduced_motion_) {
    CancelWorkspaceTransition();
  }
  appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(appearance::SurfaceRole::kSidebar,
                                              policy);
  // A backdrop filter on the full-height docked surface samples Chromium's
  // saturated frame color outside the sidebar bounds on macOS. Keep the
  // docked material translucent, but reserve compositor blur for the smaller
  // floating sidebar where its backdrop is the actual browser content.
  if (surface.uses_glass() &&
      GetPresentationMode(*browser_->GetProfile()->GetPrefs()) ==
          SidebarPresentationMode::kDocked) {
    surface.background_color = ui::kColorSysSurfaceVariant;
    surface.opacity = 0.90f;
    surface.background_blur_sigma = 0.0f;
  }
  surface_corner_radius_ = surface.corner_radius;
  appearance::ApplySurfaceAppearance(
      this, surface, appearance::SurfaceCornerOwnership::kCaller);
  // This surface is rounded in floating mode. Even an opaque theme must keep
  // transparent corner pixels truthful to CoreAnimation; otherwise the layer
  // can substitute the browser background while scrolling or dragging.
  layer()->SetFillsBoundsOpaquely(false);

  // The host owns the only full-size surface. Its scroll/tree children stay
  // transparent, while the overlay resolves its own semantic material.
  if (scroll_view_) {
    scroll_view_->SetBackgroundColor(std::nullopt);
  }
  if (tree_view_) {
    tree_view_->SetBackground(nullptr);
  }
  if (mini_player_view_) {
    mini_player_view_->SetSurfaceAppearance(
        appearance::AppearanceResolver::Resolve(
            appearance::SurfaceRole::kMiniPlayer, policy));
  }
  // Semantic base colors and accessibility policy changed. Snap to the newly
  // resolved endpoint instead of blending through an obsolete theme.
  RefreshPageTint(/*allow_animation=*/false);
  SchedulePaint();
}

void BrowserSidebarHostView::RefreshPageTint(bool allow_animation) {
  PrefService* const prefs = browser_->GetProfile()->GetPrefs();
  const bool enabled = appearance::IsSidebarPageTintEnabled(*prefs);
  content::WebContents* const contents =
      enabled && !high_contrast_ && tab_strip_model_
          ? tab_strip_model_->GetActiveWebContents()
          : nullptr;
  if (web_contents() != contents) {
    Observe(contents);
  }

  const std::optional<SkColor> theme_color =
      contents ? contents->GetThemeColor() : std::nullopt;
  std::optional<SkColor> favicon_color;
  if (contents) {
    // The driver exposes only the favicon already accepted for the current
    // committed entry. Reading it here triggers no fetch, navigation, or page
    // capture. ResolveSidebarPageTint may use it when a declared theme color is
    // neutral, so the bounded analysis remains local and privacy-safe.
    favicon::ContentFaviconDriver* const favicon_driver =
        favicon::ContentFaviconDriver::FromWebContents(contents);
    if (favicon_driver && favicon_driver->FaviconIsValid()) {
      favicon_color = appearance::ExtractSidebarPageColorFromFavicon(
          favicon_driver->GetFavicon().AsBitmap());
    }
  }
  std::optional<SkColor> sidebar_background_color;
  std::optional<SkColor> sidebar_foreground_color;
  std::optional<SkColor> sidebar_muted_foreground_color;
  if (const ui::ColorProvider* const color_provider = GetColorProvider();
      color_provider && appearance_signal_source_) {
    const appearance::SurfaceAppearance surface =
        appearance::AppearanceResolver::Resolve(
            appearance::SurfaceRole::kSidebar,
            appearance_signal_source_->policy());
    sidebar_background_color =
        color_provider->GetColor(surface.background_color);
    // Guard both primary titles and the weaker metadata/icon token painted
    // directly over this surface. The weakest relevant token determines the
    // maximum safe tint strength.
    sidebar_foreground_color = color_provider->GetColor(visual_style::kText);
    sidebar_muted_foreground_color =
        color_provider->GetColor(visual_style::kMutedText);
  }
  const std::optional<SkColor> resolved_tint =
      appearance::ResolveSidebarPageTint(
          enabled, high_contrast_, theme_color, favicon_color,
          sidebar_background_color, sidebar_foreground_color,
          reduced_transparency_, sidebar_muted_foreground_color);
  sidebar_tint_transition_.SetTarget(
      resolved_tint, allow_animation && !reduced_motion_ && !high_contrast_);
}

void BrowserSidebarHostView::DidChangeThemeColor() {
  RefreshPageTint();
}

void BrowserSidebarHostView::WebContentsDestroyed() {
  // WebContentsObserver clears its binding after this notification. Avoid
  // consulting a WebContents while it is tearing down; the next tab-model
  // selection notification attaches the replacement.
  sidebar_tint_transition_.Reset(std::nullopt);
}

void BrowserSidebarHostView::OnSidebarTintTransitionUpdated() {
  SchedulePaint();
}

void BrowserSidebarHostView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const std::optional<SkColor> tint_color =
      sidebar_tint_transition_.current_color();
  if (!tint_color.has_value()) {
    return;
  }
  cc::PaintFlags tint;
  tint.setAntiAlias(true);
  tint.setStyle(cc::PaintFlags::kFill_Style);
  tint.setColor(*tint_color);
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), surface_corner_radius_,
                        tint);
}

void BrowserSidebarHostView::RefreshMediaTrackers() {
  if (!tab_strip_model_) {
    media_state_subscriptions_.clear();
    media_trackers_.clear();
    RefreshMiniPlayerSources();
    return;
  }

  std::set<int> live_handles;
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab) {
      continue;
    }
    const int handle = tab->GetHandle().raw_value();
    live_handles.insert(handle);
    auto [it, inserted] = media_trackers_.try_emplace(handle, nullptr);
    if (inserted) {
      it->second = std::make_unique<AhoiMediaStateTracker>(tab->GetContents());
      media_state_subscriptions_.insert_or_assign(
          handle, it->second->AddStateChangedCallback(base::BindRepeating(
                      &BrowserSidebarHostView::OnTrackedMediaStateChanged,
                      weak_ptr_factory_.GetWeakPtr())));
    } else if (!it->second->IsTracking(tab->GetContents())) {
      it->second->SetWebContents(tab->GetContents());
    }
  }

  for (auto it = media_trackers_.begin(); it != media_trackers_.end();) {
    if (!live_handles.contains(it->first)) {
      media_state_subscriptions_.erase(it->first);
      it = media_trackers_.erase(it);
    } else {
      ++it;
    }
  }
  RefreshMiniPlayerSources();
}

std::string BrowserSidebarHostView::GetMiniPlayerSourceId(
    tabs::TabInterface* tab) const {
  return tab ? base::NumberToString(tab->GetHandle().raw_value())
             : std::string();
}

ui::ImageModel BrowserSidebarHostView::GetMiniPlayerFavicon(
    const MediaMiniPlayerSourceId& source_id) const {
  if (!tab_strip_model_) {
    return ui::ImageModel();
  }
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (tab && GetMiniPlayerSourceId(tab) == source_id) {
      return GetLiveTabFavicon(tab);
    }
  }
  return ui::ImageModel();
}

void BrowserSidebarHostView::RefreshMiniPlayerSources() {
  if (!mini_player_adapter_) {
    mini_player_tab_handles_.clear();
    return;
  }
  if (!tab_strip_model_) {
    for (const int handle : mini_player_tab_handles_) {
      mini_player_adapter_->UnregisterWebContents(base::NumberToString(handle));
    }
    mini_player_tab_handles_.clear();
    return;
  }

  std::set<int> live_handles;
  int presentation_order = 0;
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab || !tab->GetContents()) {
      continue;
    }
    const int handle = tab->GetHandle().raw_value();
    const std::string source_id = GetMiniPlayerSourceId(tab);
    live_handles.insert(handle);
    if (!mini_player_adapter_->IsRegistered(source_id)) {
      mini_player_adapter_->RegisterWebContents(source_id, tab->GetContents(),
                                                presentation_order);
    } else {
      mini_player_adapter_->UpdateWebContents(source_id, tab->GetContents(),
                                              presentation_order);
    }
    ++presentation_order;
  }

  for (auto it = mini_player_tab_handles_.begin();
       it != mini_player_tab_handles_.end();) {
    if (!live_handles.contains(*it)) {
      mini_player_adapter_->UnregisterWebContents(base::NumberToString(*it));
      it = mini_player_tab_handles_.erase(it);
    } else {
      ++it;
    }
  }
  mini_player_tab_handles_.insert(live_handles.begin(), live_handles.end());
  if (mini_player_view_) {
    // Favicon updates are tab presentation changes, not MediaSession changes.
    // Refreshing the decoration here keeps navigation and discarded/restored
    // WebContents truthful without perturbing player state or source choice.
    mini_player_view_->RefreshSourceDecoration();
  }
}

void BrowserSidebarHostView::OnTrackedMediaStateChanged(const AhoiMediaState&) {
  ScheduleRuntimePresentationRefresh();
}

std::optional<tabs::TabAlert> BrowserSidebarHostView::GetMediaAlertForTab(
    tabs::TabInterface* tab) const {
  if (!tab) {
    return std::nullopt;
  }
  const auto tracker = media_trackers_.find(tab->GetHandle().raw_value());
  if (tracker != media_trackers_.end() &&
      tracker->second->state().capture_activity.primary_activity.has_value()) {
    return tracker->second->state().capture_activity.primary_activity;
  }
  if (mini_player_service_) {
    const MediaMiniPlayerSourceId source_id = GetMiniPlayerSourceId(tab);
    const auto source = std::ranges::find_if(
        mini_player_service_->state().sources,
        [&source_id](const MediaMiniPlayerSource& candidate) {
          return candidate.id == source_id;
        });
    if (source != mini_player_service_->state().sources.end()) {
      // WebContents::IsCurrentlyAudible() intentionally turns false while a
      // playing tab is muted. MediaSession playback keeps the muted indicator
      // truthful after Chromium's short "recently audible" grace period.
      const std::optional<tabs::TabAlert> alert =
          GetSidebarMediaAlertForSession(
              source->playback == MediaMiniPlayerPlaybackState::kPlaying,
              source->is_muted, source->is_in_picture_in_picture,
              source->IsRelevant());
      if (alert.has_value()) {
        return alert;
      }
    }
  }
  return tracker == media_trackers_.end()
             ? std::nullopt
             : tracker->second->state().primary_alert;
}

ui::ImageModel BrowserSidebarHostView::GetMediaIndicatorForTab(
    tabs::TabInterface* tab) const {
  return GetSidebarMediaIndicator(GetMediaAlertForTab(tab));
}

std::u16string BrowserSidebarHostView::GetTabAlertStatusText(
    tabs::TabInterface* tab) const {
  const std::optional<tabs::TabAlert> alert = GetMediaAlertForTab(tab);
  return alert.has_value()
             ? tabs::TabAlertController::GetTabAlertStateText(*alert)
             : std::u16string();
}

}  // namespace ahoi::sidebar
