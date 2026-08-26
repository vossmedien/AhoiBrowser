// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/media/media_mini_player_view.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
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
  appearance::ApplySurfaceAppearance(
      this, appearance::AppearanceResolver::Resolve(
                appearance::SurfaceRole::kSidebar, policy));
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
  SchedulePaint();
}

}  // namespace ahoi::sidebar
