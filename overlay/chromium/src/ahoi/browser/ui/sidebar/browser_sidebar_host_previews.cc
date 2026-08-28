// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

constexpr size_t kMaximumSavedThumbnailSnapshots = 64;

gfx::ImageSkia RasterizePreviewIcon(const ui::ImageModel& icon,
                                    const ui::ColorProvider* colors) {
  return !icon.IsEmpty() && (colors || icon.IsImage()) ? icon.Rasterize(colors)
                                                       : gfx::ImageSkia();
}

}  // namespace

void BrowserSidebarHostView::OnTabThumbnailChanged(int runtime_tab_handle) {
  const auto cache = tab_thumbnail_cache_.find(runtime_tab_handle);
  if (tabs::TabInterface* tab = FindRuntimeTab(runtime_tab_handle)) {
    const std::optional<base::Uuid> node_id =
        session_bridge_->FindTreeNodeIdForTab(tab);
    if (node_id.has_value()) {
      const gfx::ImageSkia image =
          cache != tab_thumbnail_cache_.end() && cache->second
              ? cache->second->image()
              : gfx::ImageSkia();
      if (!image.isNull() && !image.size().IsEmpty()) {
        content::WebContents* const contents = tab->GetContents();
        StoreSavedTabThumbnailSnapshot(
            *node_id, contents ? contents->GetLastCommittedURL() : GURL(),
            image);
      } else {
        saved_thumbnail_snapshots_.erase(*node_id);
      }
    }
  }
  if (tab_preview_controller_) {
    tab_preview_controller_->Refresh();
  }
}

std::vector<gfx::ImageSkia>
BrowserSidebarHostView::GetRuntimeTabPreviewThumbnails(
    base::WeakPtr<tabs::TabInterface> tab) const {
  if (!tab ||
      session_bridge_->FindTabStripModelForTab(tab.get()) != tab_strip_model_) {
    return {};
  }
  if (tab->GetSplit().has_value()) {
    const split_tabs::SplitTabData* const split_data =
        tab_strip_model_->GetSplitData(*tab->GetSplit());
    if (split_data) {
      return GetCachedDragThumbnails(split_data->ListTabs());
    }
  }
  return GetCachedDragThumbnails({tab.get()});
}

void BrowserSidebarHostView::OnRuntimeTabHoverChanged(
    base::WeakPtr<tabs::TabInterface> tab,
    views::View* anchor,
    bool hovered) {
  if (!tab_preview_controller_ || !tab || !tab_strip_model_) {
    return;
  }
  if (hovered) {
    InvalidateAndCloseGroupRecentBubble();
    const split_tabs::SplitTabData* const split_data =
        tab->GetSplit().has_value()
            ? tab_strip_model_->GetSplitData(*tab->GetSplit())
            : nullptr;
    const std::vector<tabs::TabInterface*> preview_tabs =
        split_data
            ? split_data->ListTabs()
            : std::vector<tabs::TabInterface*>{tab.get()};
    for (tabs::TabInterface* preview_tab : preview_tabs) {
      if (!preview_tab || !preview_tab->GetContents()) {
        continue;
      }
      const auto cached = tab_thumbnail_cache_.find(
          preview_tab->GetHandle().raw_value());
      if (cached != tab_thumbnail_cache_.end() && cached->second &&
          !cached->second->image().isNull() &&
          !cached->second->image().size().IsEmpty()) {
        continue;
      }
      if (ThumbnailTabHelper* helper =
              ThumbnailTabHelper::FromWebContents(preview_tab->GetContents())) {
        // Capture the already-visible renderer surface on demand. This does
        // not activate, navigate or materialize a closed saved tab.
        helper->CaptureThumbnailOnTabBackgrounded();
      }
    }
  }
  tab_preview_controller_->OnRuntimeTabHover(tab->GetHandle().raw_value(),
                                             anchor, hovered);
}

void BrowserSidebarHostView::OnSavedPageHoverChanged(const base::Uuid& node_id,
                                                     views::View* anchor,
                                                     bool hovered) {
  if (!tab_preview_controller_) {
    return;
  }
  if (hovered) {
    InvalidateAndCloseGroupRecentBubble();
    if (tabs::TabInterface* live_tab =
            session_bridge_->FindTabByTreeNodeId(node_id)) {
      if (content::WebContents* contents = live_tab->GetContents()) {
        const auto cached =
            tab_thumbnail_cache_.find(live_tab->GetHandle().raw_value());
        const bool needs_thumbnail =
            cached == tab_thumbnail_cache_.end() || !cached->second ||
            cached->second->image().isNull() ||
            cached->second->image().size().IsEmpty();
        if (needs_thumbnail) {
          if (ThumbnailTabHelper* helper =
                  ThumbnailTabHelper::FromWebContents(contents)) {
            helper->CaptureThumbnailOnTabBackgrounded();
          }
        }
      }
    }
  }
  tab_preview_controller_->OnSavedPageHover(node_id, anchor, hovered);
}

std::optional<SidebarTabPreviewData>
BrowserSidebarHostView::ResolveTabPreviewData(
    const SidebarTabPreviewTarget& target) {
  if (target.kind == SidebarTabPreviewTarget::Kind::kSavedPage) {
    const tab_tree::TreeNode* const node =
        controller_->view_model().GetNode(target.saved_node_id);
    if (!node || node->type != tab_tree::TreeNodeType::kSavedPage) {
      return std::nullopt;
    }
    const ui::ImageModel icon = GetSavedPageIcon(*node);
    return SidebarTabPreviewData{
        .title = node->title.empty() ? base::UTF8ToUTF16(node->url.spec())
                                     : node->title,
        .favicon = RasterizePreviewIcon(icon, GetColorProvider()),
        .thumbnails = GetSavedPageDragThumbnails(node->id)};
  }

  tabs::TabInterface* const tab = FindRuntimeTab(target.runtime_tab_handle);
  if (!tab ||
      session_bridge_->FindTabStripModelForTab(tab) != tab_strip_model_) {
    return std::nullopt;
  }
  const ui::ImageModel icon = GetLiveTabFavicon(tab);
  return SidebarTabPreviewData{
      .title = tab->GetTitle().empty() ? l10n_util::GetStringUTF16(IDS_NEW_TAB)
                                       : tab->GetTitle(),
      .favicon = RasterizePreviewIcon(icon, GetColorProvider()),
      .thumbnails = GetRuntimeTabPreviewThumbnails(tab->GetWeakPtr())};
}

bool BrowserSidebarHostView::ValidateTabPreviewAnchor(
    const SidebarTabPreviewTarget& target,
    const views::View* anchor) const {
  if (!anchor || anchor->GetWidget() != GetWidget()) {
    return false;
  }
  if (target.kind == SidebarTabPreviewTarget::Kind::kSavedPage) {
    const auto* const row = views::AsViewClass<SidebarTreeRowView>(anchor);
    return row && row->is_bound() && row->node_id() == target.saved_node_id;
  }
  base::WeakPtr<tabs::TabInterface> tab =
      GetOpenTabForView(const_cast<views::View*>(anchor));
  return tab && tab->GetHandle().raw_value() == target.runtime_tab_handle &&
         session_bridge_->FindTabStripModelForTab(tab.get()) ==
             tab_strip_model_;
}

void BrowserSidebarHostView::StoreSavedTabThumbnailSnapshot(
    const base::Uuid& node_id,
    const GURL& url,
    const gfx::ImageSkia& image) {
  if (!node_id.is_valid() || !url.is_valid() || url.is_empty() ||
      image.isNull() || image.size().IsEmpty() ||
      browser_->GetProfile()->IsOffTheRecord()) {
    return;
  }
  saved_thumbnail_snapshots_[node_id] = {
      .url = url, .image = image, .recency = ++saved_thumbnail_recency_};
  if (saved_thumbnail_snapshots_.size() <= kMaximumSavedThumbnailSnapshots) {
    return;
  }
  const auto oldest = std::ranges::min_element(
      saved_thumbnail_snapshots_, {},
      [](const auto& entry) { return entry.second.recency; });
  if (oldest != saved_thumbnail_snapshots_.end()) {
    saved_thumbnail_snapshots_.erase(oldest);
  }
}

}  // namespace ahoi::sidebar
