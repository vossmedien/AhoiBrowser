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
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
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
  const appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(appearance::SurfaceRole::kSidebar,
                                              policy);
  surface_corner_radius_ = surface.corner_radius;
  appearance::ApplySurfaceAppearance(this, surface);
  // This surface is rounded in floating mode. Even an opaque theme must keep
  // transparent corner pixels truthful to CoreAnimation; otherwise the layer
  // can substitute the browser background while scrolling or dragging.
  layer()->SetFillsBoundsOpaquely(false);

  // The host owns the only full-size surface. Its scroll/tree children stay
  // transparent, while the overlay resolves its own semantic material.
  if (scroll_view_) {
    scroll_view_->SetBackground(nullptr);
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

}  // namespace ahoi::sidebar
