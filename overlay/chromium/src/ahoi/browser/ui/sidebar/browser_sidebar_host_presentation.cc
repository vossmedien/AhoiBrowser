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
#include "ahoi/browser/ui/sidebar/sidebar_saved_row_presence.h"
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
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
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
  const bool applied = BrowserView::GetBrowserViewForBrowser(browser_.get())
                           ->SetAhoiSidebarPresentationMode(mode);
  if (applied) {
    SetSidebarHeaderActionToggleState(
        floating_sidebar_button_, mode == SidebarPresentationMode::kFloating);
    if (appearance_signal_source_) {
      OnAppearanceChanged(appearance_signal_source_->policy());
    }
  }
  return applied;
}

bool BrowserSidebarHostView::ToggleFloatingSidebar() {
  const SidebarPresentationMode current =
      BrowserView::GetBrowserViewForBrowser(browser_.get())
          ->GetAhoiSidebarPresentationMode();
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
      BrowserView::GetBrowserViewForBrowser(browser_.get())
          ->GetAhoiSidebarPresentationMode();
  if (current == SidebarPresentationMode::kHidden) {
    return RestoreSidebar();
  }
  return SetSidebarPresentationMode(SidebarPresentationMode::kHidden);
}

bool BrowserSidebarHostView::RestoreSidebar() {
  if (BrowserView::GetBrowserViewForBrowser(browser_.get())
          ->GetAhoiSidebarPresentationMode() !=
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
      BrowserView::GetBrowserViewForBrowser(browser_.get())
          ->GetAhoiSidebarPresentationMode();
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
      discovery_view_->is_open()) {
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

std::u16string BrowserSidebarHostView::GetSavedPageStatusText(
    const tab_tree::TreeNode& node) const {
  auto status =
      GetTabAlertStatusText(session_bridge_->FindTabByTreeNodeId(node.id));
  if (node.is_temporary) {
    if (!status.empty()) {
      status += u" — ";
    }
    status += GetSharedTabOriginText(node.id);
  }
  if (IsSavedPageBookmarked(node)) {
    if (!status.empty()) {
      status += u" — ";
    }
    status +=
        l10n_util::GetStringUTF16(IDS_NTP_MODULES_HISTORY_CLUSTERS_BOOKMARKED);
  }
  return status;
}

bool BrowserSidebarHostView::IsSavedPageBookmarked(
    const tab_tree::TreeNode& node) const {
  if (node.type != tab_tree::TreeNodeType::kSavedPage) {
    return false;
  }
  tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node.id);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return IsUrlBookmarked(contents ? chrome::GetURLToBookmark(contents)
                                  : node.url);
}

void BrowserSidebarHostView::RefreshThumbnailCache() {
  if (!tab_strip_model_) {
    thumbnails_.tab_cache.clear();
    return;
  }

  std::set<int> live_handles;
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (!tab) {
      continue;
    }
    const int handle = tab->GetHandle().raw_value();
    live_handles.insert(handle);
    auto [it, inserted] = thumbnails_.tab_cache.try_emplace(handle, nullptr);
    if (inserted) {
      it->second = std::make_unique<CachedTabThumbnail>(
          base::BindRepeating(&BrowserSidebarHostView::OnTabThumbnailChanged,
                              weak_ptr_factory_.GetWeakPtr(), handle));
    }
    it->second->Observe(tab);
  }
  for (auto it = thumbnails_.tab_cache.begin();
       it != thumbnails_.tab_cache.end();) {
    if (!live_handles.contains(it->first)) {
      it = thumbnails_.tab_cache.erase(it);
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
    const auto it = thumbnails_.tab_cache.find(tab->GetHandle().raw_value());
    if (it != thumbnails_.tab_cache.end() && !it->second->image().isNull() &&
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

void BrowserSidebarHostView::RefreshRuntimePresentation(
    bool refresh_auxiliary) {
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
  // Search selection stores row pointers. Preserve its stable identity, then
  // clear the old visual state while those rows are still alive. Async favicon,
  // tab and sync updates must not reset a user's current keyboard position.
  std::optional<SidebarDiscoveryPrimaryResult> primary_result_before_refresh;
  if (discovery_state_.primary_selection.has_value() &&
      *discovery_state_.primary_selection <
          discovery_state_.primary_results.size()) {
    primary_result_before_refresh = discovery_state_.primary_results
        [*discovery_state_.primary_selection];
    primary_result_before_refresh->row = nullptr;
  }
  ClearSidebarDiscoveryPrimarySelection(/*restore_tree_selection=*/false);
  if (runtime_auxiliary_ready_ && refresh_auxiliary) {
    RefreshThumbnailCache();
    RefreshMediaTrackers();
    PublishLocalDeviceTabs();
    PublishDeviceTabCommands();
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
  const bool search_active = !discovery_state_.query.empty();
  const auto is_search_match_tab = [this,
                                    search_active](tabs::TabInterface* tab) {
    if (!search_active) {
      return true;
    }
    if (!tab) {
      return false;
    }
    if (const std::optional<base::Uuid> saved_node_id =
            session_bridge_->FindTreeNodeIdForTab(tab);
        saved_node_id.has_value()) {
      return controller_->view_model().IsSearchMatch(*saved_node_id);
    }
    return discovery_state_.runtime_tab_handles.contains(
        tab->GetHandle().raw_value());
  };
  const auto create_open_tab_row = [this,
                                    search_active](tabs::TabInterface* tab) {
    const std::optional<base::Uuid> saved_node_id =
        session_bridge_->FindTreeNodeIdForTab(tab);
    const auto shared_id = session_bridge_->FindSharedTreeNodeIdForTab(tab);
    auto status = GetTabAlertStatusText(tab);
    ui::ImageModel origin_badge;
    if (!saved_node_id && shared_id) {
      origin_badge = GetSharedTabOriginIcon(*shared_id);
      if (!status.empty()) {
        status += u" — ";
      }
      status += GetSharedTabOriginText(*shared_id);
    }
    const bool bookmarked =
        tab->GetContents() &&
        IsUrlBookmarked(chrome::GetURLToBookmark(tab->GetContents()));
    if (bookmarked) {
      if (!status.empty()) {
        status += u" — ";
      }
      status += l10n_util::GetStringUTF16(
          IDS_NTP_MODULES_HISTORY_CLUSTERS_BOOKMARKED);
    }
    return CreateOpenTabRowView(
        tab, saved_node_id, GetLiveTabFavicon(tab), GetMediaAlertForTab(tab),
        std::move(status), tab == tab_strip_model_->GetActiveTab(),
        ahoi::memory::IsTabSleeping(tab), /*drag_enabled=*/!search_active,
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
        this, std::move(origin_badge), bookmarked);
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
  // Temporary pages now have real tree identities too. Their live row remains
  // in the existing temporary/split section, never duplicated above it.
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (is_visible_temporary_tab(tab)) {
      if (const auto id = session_bridge_->FindSharedTreeNodeIdForTab(tab)) {
        mixed_split_saved_nodes.insert(*id);
      }
    }
  }
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
        if (search_active &&
            !std::ranges::any_of(split_tabs, is_search_match_tab)) {
          // A split is one visual and interaction unit. Mark its temporary
          // members as visited even when the complete unit is filtered out so
          // a later pane cannot leak back as a detached ordinary row.
          for (tabs::TabInterface* split_tab : split_tabs) {
            if (is_visible_temporary_tab(split_tab)) {
              presented_temporary_tabs.insert(split_tab);
              presented_temporary_handles.insert(
                  split_tab->GetHandle().raw_value());
            }
          }
          continue;
        }
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
        const std::optional<split_tabs::SplitTabId> split_id = tab->GetSplit();
        CHECK(split_id.has_value());
        open_tabs_container_->AddChildView(CreateOpenTabSplitRowView(
            std::move(split_rows), *split_data->visual_data(),
            base::BindRepeating(
                [](base::WeakPtr<BrowserSidebarHostView> host,
                   split_tabs::SplitTabId id, size_t divider_index,
                   double ratio, bool done_resizing) {
                  return host && host->ResizeSidebarSplit(id, divider_index,
                                                          ratio, done_resizing);
                },
                weak_ptr_factory_.GetWeakPtr(), *split_id)));
        continue;
      }
    }

    presented_temporary_tabs.insert(tab);
    presented_temporary_handles.insert(tab_handle);
    if (!is_search_match_tab(tab)) {
      continue;
    }
    open_tabs_container_->AddChildView(create_open_tab_row(tab));
  }
  // A closed temporary tab of an unrestored session is not a saved row; see
  // ShouldHideClosedTemporaryPageRow. Hiding it is presentation-only, so a
  // later session restore that binds its tab brings it back under "Open tabs".
  const SidebarTreeViewModel& tree_model = controller_->view_model();
  for (const SidebarTreeViewModel::Row& row : tree_model.rows()) {
    const tab_tree::TreeNode* const node = tree_model.GetNode(row.node_id);
    if (!node || !node->is_temporary) {
      continue;
    }
    const std::optional<base::Uuid> creator =
        profile_sync_service_
            ? profile_sync_service_->GetSharedTabProvenance(node->id)
                  .creation_device
            : std::nullopt;
    const SavedRowRuntimeFacts facts{
        .has_live_tab =
            session_bridge_->FindTabByTreeNodeId(node->id) != nullptr,
        .archived = session_bridge_->tab_tree_store() &&
                    session_bridge_->tab_tree_store()->IsNodeArchived(node->id),
        .created_on_other_device =
            creator.has_value() &&
            *creator != profile_sync_service_->local_device_id()};
    if (ShouldHideClosedTemporaryPageRow(*node, facts)) {
      mixed_split_saved_nodes.insert(node->id);
    }
  }
  // Suppression is presentation-only and is recalculated from authoritative
  // SplitTabData on every refresh. Ending or reclassifying a split therefore
  // restores the exact persistent saved-page proxies without a store write.
  tree_view_->SetRuntimeCompositeSuppressedNodes(
      std::move(mixed_split_saved_nodes));
  const bool has_open_tabs = !open_tabs_container_->children().empty();
  // The action closes every temporary tab in the workspace, including rows
  // hidden by the active filter. Do not present that destructive global
  // action beside a partial search projection.
  open_tabs_header_->SetVisible(has_open_tabs && !search_active);
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
  RebuildSidebarDiscoveryPrimaryResults();
  bool primary_selection_restored = false;
  if (primary_result_before_refresh.has_value()) {
    for (size_t index = 0; index < discovery_state_.primary_results.size();
         ++index) {
      const SidebarDiscoveryPrimaryResult& candidate =
          discovery_state_.primary_results[index];
      const bool same_identity =
          candidate.kind == primary_result_before_refresh->kind &&
          ((candidate.kind == SidebarDiscoveryPrimaryResultKind::kTreeNode &&
            candidate.node_id == primary_result_before_refresh->node_id) ||
           (candidate.kind == SidebarDiscoveryPrimaryResultKind::kDeviceTab &&
            candidate.device_tab_stable_id ==
                primary_result_before_refresh->device_tab_stable_id) ||
           (candidate.kind == SidebarDiscoveryPrimaryResultKind::kRuntimeTab &&
            candidate.runtime_tab_handle ==
                primary_result_before_refresh->runtime_tab_handle));
      if (!same_identity) {
        continue;
      }
      discovery_state_.primary_selection = index;
      switch (candidate.kind) {
        case SidebarDiscoveryPrimaryResultKind::kTreeNode:
          primary_selection_restored =
              controller_->SelectNode(candidate.node_id);
          break;
        case SidebarDiscoveryPrimaryResultKind::kDeviceTab:
          SetRemoteTabSearchSelected(candidate.row, true);
          primary_selection_restored = candidate.row != nullptr;
          break;
        case SidebarDiscoveryPrimaryResultKind::kRuntimeTab:
          SetOpenTabSearchSelected(candidate.row, true);
          primary_selection_restored = candidate.row != nullptr;
          break;
      }
      if (!primary_selection_restored) {
        discovery_state_.primary_selection.reset();
      }
      break;
    }
  }
  if (!primary_selection_restored && discovery_view_) {
    if (primary_result_before_refresh.has_value() &&
        primary_result_before_refresh->kind ==
            SidebarDiscoveryPrimaryResultKind::kTreeNode) {
      std::ignore = controller_->SelectNode(std::nullopt);
    }
    discovery_view_->InvalidatePrimaryResultSelection();
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

}  // namespace ahoi::sidebar
