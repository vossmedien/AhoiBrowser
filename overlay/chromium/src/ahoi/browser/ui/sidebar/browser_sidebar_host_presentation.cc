// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/memory/tab_sleeping.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_sync_controls.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

bool BrowserSidebarHostView::SetSidebarPresentationMode(
    SidebarPresentationMode mode) {
  if (!browser_ || !browser_->GetProfile() ||
      !SetPresentationMode(browser_->GetProfile()->GetPrefs(), mode)) {
    return false;
  }
  return browser_->GetBrowserView().SetAhoiSidebarPresentationMode(mode);
}

bool BrowserSidebarHostView::ToggleFloatingSidebar() {
  const SidebarPresentationMode current =
      browser_->GetBrowserView().GetAhoiSidebarPresentationMode();
  if (current == SidebarPresentationMode::kHidden) {
    return SetSidebarPresentationMode(
        GetVisibleModeBeforeHidden(*browser_->GetProfile()->GetPrefs()));
  }
  return SetSidebarPresentationMode(current ==
                                            SidebarPresentationMode::kFloating
                                        ? SidebarPresentationMode::kDocked
                                        : SidebarPresentationMode::kFloating);
}

bool BrowserSidebarHostView::ToggleSidebarVisibility() {
  const SidebarPresentationMode current =
      browser_->GetBrowserView().GetAhoiSidebarPresentationMode();
  if (current == SidebarPresentationMode::kHidden) {
    return RestoreSidebar();
  }
  return SetSidebarPresentationMode(SidebarPresentationMode::kHidden);
}

bool BrowserSidebarHostView::RestoreSidebar() {
  if (browser_->GetBrowserView().GetAhoiSidebarPresentationMode() !=
      SidebarPresentationMode::kHidden) {
    return false;
  }
  return SetSidebarPresentationMode(
      GetVisibleModeBeforeHidden(*browser_->GetProfile()->GetPrefs()));
}

void BrowserSidebarHostView::OnSidebarHeaderActionPressed(
    bool toggle_visibility,
    const ui::Event&) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&BrowserSidebarHostView::RunSidebarHeaderAction,
                     weak_ptr_factory_.GetWeakPtr(), toggle_visibility));
}

void BrowserSidebarHostView::RunSidebarHeaderAction(bool toggle_visibility) {
  const SidebarPresentationMode current =
      browser_->GetBrowserView().GetAhoiSidebarPresentationMode();
  if (current == SidebarPresentationMode::kHidden) {
    // The visibility button is also used by the edge-reveal overlay. In that
    // state the persisted mode is already hidden, so reapplying it closes only
    // the temporary overlay. The dock/floating button restores a stable docked
    // surface before it can toggle presentation again.
    std::ignore = SetSidebarPresentationMode(
        toggle_visibility ? SidebarPresentationMode::kHidden
                          : SidebarPresentationMode::kDocked);
    return;
  }
  std::ignore =
      toggle_visibility ? ToggleSidebarVisibility() : ToggleFloatingSidebar();
}

bool BrowserSidebarHostView::OnKeyPressed(const ui::KeyEvent& event) {
  const int flags = event.flags();
  const bool command_or_control =
      (flags & ui::EF_COMMAND_DOWN) || (flags & ui::EF_CONTROL_DOWN);
  const bool shift = flags & ui::EF_SHIFT_DOWN;
  if (event.key_code() == ui::VKEY_ESCAPE && discovery_view_ &&
      discovery_view_->GetVisible()) {
    return discovery_view_->CloseOrClear();
  }
  if (command_or_control && shift && event.key_code() == ui::VKEY_S) {
    return ToggleFloatingSidebar();
  }
  if (command_or_control && shift && event.key_code() == ui::VKEY_H) {
    return ToggleSidebarVisibility();
  }
  return views::View::OnKeyPressed(event);
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

std::u16string BrowserSidebarHostView::GetSavedPageStatusText(
    const tab_tree::TreeNode& node) const {
  return GetTabAlertStatusText(session_bridge_->FindTabByTreeNodeId(node.id));
}

void BrowserSidebarHostView::RefreshThumbnailCache() {
  if (!tab_strip_model_) {
    tab_thumbnail_cache_.clear();
    return;
  }

  std::set<int> live_handles;
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab) {
      continue;
    }
    const int handle = tab->GetHandle().raw_value();
    live_handles.insert(handle);
    auto [it, inserted] = tab_thumbnail_cache_.try_emplace(handle, nullptr);
    if (inserted) {
      it->second = std::make_unique<CachedTabThumbnail>(
          base::BindRepeating(&BrowserSidebarHostView::OnTabThumbnailChanged,
                              weak_ptr_factory_.GetWeakPtr(), handle));
    }
    it->second->Observe(tab);
  }
  for (auto it = tab_thumbnail_cache_.begin();
       it != tab_thumbnail_cache_.end();) {
    if (!live_handles.contains(it->first)) {
      it = tab_thumbnail_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<gfx::ImageSkia> BrowserSidebarHostView::GetCachedDragThumbnails(
    const std::vector<tabs::TabInterface*>& tabs) const {
  std::vector<gfx::ImageSkia> thumbnails;
  thumbnails.reserve(tabs.size());
  for (tabs::TabInterface* tab : tabs) {
    if (!tab) {
      thumbnails.emplace_back();
      continue;
    }
    const auto it = tab_thumbnail_cache_.find(tab->GetHandle().raw_value());
    if (it != tab_thumbnail_cache_.end() && !it->second->image().isNull() &&
        !it->second->image().size().IsEmpty()) {
      thumbnails.push_back(it->second->image());
    } else {
      // Preserve the split member count even while one member still awaits
      // an asynchronous thumbnail; the preview header can then show a
      // truthful multi-tab indicator and use the favicon/title fallback.
      thumbnails.emplace_back();
    }
  }
  return thumbnails;
}

void BrowserSidebarHostView::RefreshRuntimePresentation() {
  if (!runtime_refresh_gate_.BeginRefresh(IsSidebarDragActive())) {
    // Thumbnail capture, media state and remote-tab updates are asynchronous.
    // Rebuilding here would RemoveAllChildViews(), destroy the native drag
    // source and make the group/split targets flash away mid-gesture.
    return;
  }
  // A synchronous first projection invalidates constructor-time refresh tasks;
  // their callbacks carry the older generation and become harmless no-ops.
  ++runtime_refresh_generation_;
  if (!tab_strip_model_ || !open_tabs_container_ || !open_tabs_header_) {
    return;
  }
  if (runtime_auxiliary_ready_) {
    RefreshThumbnailCache();
    RefreshMediaTrackers();
    PublishLocalDeviceTabs();
  }
  open_tabs_container_->RemoveAllChildViews();
  const std::optional<base::Uuid> active_workspace =
      controller_->view_model().workspace_id();

  const auto is_visible_temporary_tab =
      [this, &active_workspace](tabs::TabInterface* tab) {
        if (!tab || session_bridge_->FindTreeNodeIdForTab(tab).has_value()) {
          return false;
        }
        const std::optional<base::Uuid> tab_workspace =
            session_bridge_->GetWorkspaceForTab(tab);
        return !active_workspace.has_value() || !tab_workspace.has_value() ||
               active_workspace == tab_workspace;
      };
  const auto is_visible_workspace_tab =
      [this, &active_workspace](tabs::TabInterface* tab) {
        if (!tab) {
          return false;
        }
        const std::optional<base::Uuid> tab_workspace =
            session_bridge_->GetWorkspaceForTab(tab);
        return !active_workspace.has_value() || !tab_workspace.has_value() ||
               active_workspace == tab_workspace;
      };
  const auto create_open_tab_row = [this](tabs::TabInterface* tab) {
    const std::optional<base::Uuid> saved_node_id =
        session_bridge_->FindTreeNodeIdForTab(tab);
    return CreateOpenTabRowView(
        tab, saved_node_id, GetLiveTabFavicon(tab), GetMediaAlertForTab(tab),
        GetTabAlertStatusText(tab), tab == tab_strip_model_->GetActiveTab(),
        ahoi::memory::IsTabSleeping(tab),
        base::BindRepeating(&BrowserSidebarHostView::ActivateRuntimeTab,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(&BrowserSidebarHostView::CloseRuntimeTab,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               base::WeakPtr<tabs::TabInterface> tab) {
              return host ? host->GetRuntimeTabPreviewThumbnails(tab)
                          : std::vector<gfx::ImageSkia>();
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(&BrowserSidebarHostView::OnRuntimeTabHoverChanged,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(&BrowserSidebarHostView::OnSidebarDragStateChanged,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            &BrowserSidebarHostView::OnTemporaryTabDragStateChanged,
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            &BrowserSidebarHostView::ClaimDropTargetPresentation,
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               std::optional<base::Uuid> source_node_id,
               std::optional<int> source_runtime_handle,
               base::WeakPtr<tabs::TabInterface> target,
               OpenTabDropPosition position) {
              return host && host->CanDropOnRuntimeTab(source_node_id,
                                                       source_runtime_handle,
                                                       target, position);
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               std::optional<base::Uuid> source_node_id,
               std::optional<int> source_runtime_handle,
               base::WeakPtr<tabs::TabInterface> target,
               OpenTabDropPosition position) {
              return host && host->DropOnRuntimeTab(source_node_id,
                                                    source_runtime_handle,
                                                    target, position);
            },
            weak_ptr_factory_.GetWeakPtr()),
        this);
  };

  // Rebuild temporary and mixed split rows directly from Chromium's
  // authoritative SplitTabData. Walking the tab strip keeps split and
  // ordinary rows in Chromium pane order without adjacency inference.
  std::set<tabs::TabInterface*> presented_temporary_tabs;
  // A native TabInterface pointer is normally stable, but a drag can overlap
  // a WebContents/session reconciliation frame where a wrapper is rebound.
  // Keep the process-local handle as the final identity guard so one runtime
  // tab cannot briefly render twice in the temporary section during a move.
  std::set<int> presented_temporary_handles;
  std::set<base::Uuid> mixed_split_saved_nodes;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(index);
    if (!is_visible_temporary_tab(tab)) {
      continue;
    }
    const int tab_handle = tab->GetHandle().raw_value();
    if (presented_temporary_tabs.contains(tab) ||
        presented_temporary_handles.contains(tab_handle)) {
      continue;
    }

    const split_tabs::SplitTabData* split_data =
        tab->GetSplit().has_value()
            ? tab_strip_model_->GetSplitData(*tab->GetSplit())
            : nullptr;
    if (split_data && split_data->visual_data()) {
      const std::vector<tabs::TabInterface*> split_tabs =
          split_data->ListTabs();
      size_t saved_member_count = 0;
      for (tabs::TabInterface* pane : split_tabs) {
        saved_member_count +=
            session_bridge_->FindTreeNodeIdForTab(pane).has_value() ? 1u : 0u;
      }
      const bool all_members_are_visible_temporary =
          split_tabs.size() >= 2 &&
          std::ranges::all_of(split_tabs, is_visible_temporary_tab);
      const bool is_visible_mixed_split =
          split_tabs.size() >= 2 && saved_member_count > 0 &&
          saved_member_count < split_tabs.size() &&
          std::ranges::all_of(split_tabs, is_visible_workspace_tab);
      if (all_members_are_visible_temporary || is_visible_mixed_split) {
        std::vector<std::unique_ptr<views::View>> split_rows;
        split_rows.reserve(split_tabs.size());
        for (tabs::TabInterface* split_tab : split_tabs) {
          split_rows.push_back(create_open_tab_row(split_tab));
          if (const std::optional<base::Uuid> saved_node_id =
                  session_bridge_->FindTreeNodeIdForTab(split_tab);
              saved_node_id.has_value()) {
            mixed_split_saved_nodes.insert(*saved_node_id);
          } else {
            presented_temporary_tabs.insert(split_tab);
            presented_temporary_handles.insert(
                split_tab->GetHandle().raw_value());
          }
        }
        open_tabs_container_->AddChildView(CreateOpenTabSplitRowView(
            std::move(split_rows), *split_data->visual_data()));
        continue;
      }
    }

    presented_temporary_tabs.insert(tab);
    presented_temporary_handles.insert(tab_handle);
    open_tabs_container_->AddChildView(create_open_tab_row(tab));
  }
  // Suppression is presentation-only and is recalculated from authoritative
  // SplitTabData on every refresh. Ending or reclassifying a split therefore
  // restores the exact persistent saved-page proxies without a store write.
  tree_view_->SetRuntimeCompositeSuppressedNodes(
      std::move(mixed_split_saved_nodes));
  const bool has_open_tabs = !open_tabs_container_->children().empty();
  open_tabs_header_->SetVisible(has_open_tabs);
  open_tabs_container_->SetVisible(true);
  open_tabs_container_->InvalidateLayout();
  if (runtime_auxiliary_ready_) {
    RefreshRemoteTabPresentation();
  }
  scroll_view_->InvalidateLayout();
  if (tree_view_) {
    // Bindings between saved nodes and live tabs can settle one task after a
    // split observer callback. Recompute visual rows (including preferred
    // height) on every coalesced runtime refresh so the actual SplitTabData
    // always wins over callback timing.
    tree_view_->OnSplitGroupsChanged();
  }
  PreferredSizeChanged();
}

ui::ImageModel BrowserSidebarHostView::GetFaviconForUrl(const GURL& page_url) {
  auto cached = favicon_cache_.find(page_url);
  if (cached != favicon_cache_.end()) {
    return cached->second;
  }
  if (favicon_service_ && page_url.is_valid() && !page_url.is_empty() &&
      requested_favicon_urls_.insert(page_url).second) {
    favicon_service_->GetFaviconImageForPageURL(
        page_url,
        base::BindOnce(&BrowserSidebarHostView::OnFaviconAvailable,
                       weak_ptr_factory_.GetWeakPtr(), page_url),
        &favicon_task_tracker_);
  }
  return ui::ImageModel();
}

void BrowserSidebarHostView::ActivateRuntimeTab(
    base::WeakPtr<tabs::TabInterface> tab) {
  if (!tab || !tab_strip_model_) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab.get());
  if (index >= 0) {
    tab_strip_model_->ActivateTabAt(
        index, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kMouse));
  }
}

bool BrowserSidebarHostView::ActivateRelativeRuntimeTab(int delta) {
  if (!tab_strip_model_ || tab_strip_model_->empty() || delta == 0) {
    return false;
  }

  const std::optional<base::Uuid> active_workspace =
      controller_->view_model().workspace_id();
  tabs::TabInterface* const active_tab = tab_strip_model_->GetActiveTab();
  std::vector<tabs::TabInterface*> workspace_tabs;
  workspace_tabs.reserve(tab_strip_model_->count());
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab) {
      continue;
    }
    const std::optional<base::Uuid> tab_workspace =
        session_bridge_->GetWorkspaceForTab(tab);
    if (!active_workspace.has_value() || tab_workspace == active_workspace ||
        (tab == active_tab && !tab_workspace.has_value())) {
      workspace_tabs.push_back(tab);
    }
  }
  if (workspace_tabs.empty()) {
    return false;
  }

  auto current = std::ranges::find(workspace_tabs, active_tab);
  size_t target_index = delta > 0 ? 0u : workspace_tabs.size() - 1u;
  if (current != workspace_tabs.end()) {
    const size_t current_index =
        static_cast<size_t>(std::distance(workspace_tabs.begin(), current));
    target_index = delta > 0 ? (current_index + 1u) % workspace_tabs.size()
                             : (current_index + workspace_tabs.size() - 1u) %
                                   workspace_tabs.size();
  }

  tabs::TabInterface* const target = workspace_tabs[target_index];
  const int tab_strip_index = tab_strip_model_->GetIndexOfTab(target);
  if (tab_strip_index < 0) {
    return false;
  }
  tab_strip_model_->ActivateTabAt(
      tab_strip_index, TabStripUserGestureDetails(
                           TabStripUserGestureDetails::GestureType::kWheel));
  return true;
}

void BrowserSidebarHostView::CloseRuntimeTab(
    base::WeakPtr<tabs::TabInterface> tab) {
  if (tab) {
    tab->Close();
  }
}

void BrowserSidebarHostView::CloseAllTemporaryTabs(const ui::Event&) {
  if (!tab_strip_model_) {
    return;
  }
  const std::optional<base::Uuid> active_workspace =
      controller_->view_model().workspace_id();
  std::vector<base::WeakPtr<tabs::TabInterface>> tabs_to_close;
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab || session_bridge_->FindTreeNodeIdForTab(tab).has_value()) {
      continue;
    }
    const std::optional<base::Uuid> tab_workspace =
        session_bridge_->GetWorkspaceForTab(tab);
    if (!active_workspace.has_value() || !tab_workspace.has_value() ||
        active_workspace == tab_workspace) {
      tabs_to_close.push_back(tab->GetWeakPtr());
    }
  }

  // Ahoi intentionally keeps the browser window and workspace alive when the
  // last temporary tab is closed. The empty native surface is owned by
  // BrowserView; never create a synthetic replacement WebContents here.
  for (auto tab : tabs_to_close) {
    if (tab) {
      tab->Close();
    }
  }
}

}  // namespace ahoi::sidebar
