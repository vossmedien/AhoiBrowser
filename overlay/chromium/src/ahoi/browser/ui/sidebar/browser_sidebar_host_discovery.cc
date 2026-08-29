// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <tuple>
#include <utility>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_device_tab_commands.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/core/live_tab_context.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/base_window.h"
#include "ui/events/event.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::OnSidebarDiscoveryPressed(const ui::Event&) {
  // Opening inserts the search controls around the existing primary surface.
  // Defer until the native Button has finished dispatching so the header
  // action cannot invalidate its own focus/layout path.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarHostView::ToggleSidebarDiscovery,
                                weak_ptr_factory_.GetWeakPtr()));
}

void BrowserSidebarHostView::ToggleSidebarDiscovery() {
  if (!discovery_view_) {
    return;
  }
  if (discovery_view_->is_open()) {
    CloseSidebarDiscovery();
  } else {
    OpenSidebarDiscovery();
  }
}

void BrowserSidebarHostView::OpenSidebarDiscovery() {
  if (!discovery_view_ || !media_overlay_view_) {
    return;
  }
  if (views::FocusManager* const focus_manager = GetFocusManager()) {
    views::View* const focused = focus_manager->GetFocusedView();
    if (focused && focused != discovery_view_) {
      discovery_focus_restore_tracker_.SetView(focused);
    }
  }
  if (tab_preview_controller_) {
    tab_preview_controller_->Hide();
  }
  InvalidateAndCloseGroupRecentBubble();
  ResetDragPresentation();
  if (scroll_view_ && scroll_view_->vertical_scroll_bar()) {
    discovery_scroll_offset_ =
        scroll_view_->vertical_scroll_bar()->GetPosition();
  }
  discovery_selection_before_search_ =
      controller_->view_model().selected_node_id();
  discovery_activation_committed_ = false;
  discovery_view_->Open();
  InvalidateLayout();
}

void BrowserSidebarHostView::CloseSidebarDiscovery() {
  if (!discovery_view_ || !media_overlay_view_) {
    return;
  }
  views::FocusManager* const focus_manager = GetFocusManager();
  views::View* const focused =
      focus_manager ? focus_manager->GetFocusedView() : nullptr;
  const bool primary_surface_had_focus =
      focused && (focused == media_overlay_view_ ||
                  media_overlay_view_->Contains(focused));
  const bool discovery_had_focus =
      focused && !primary_surface_had_focus &&
      (focused == discovery_view_ || discovery_view_->Contains(focused));
  const bool activation_committed = discovery_activation_committed_;
  const std::optional<base::Uuid> selection_before_search =
      discovery_selection_before_search_;
  discovery_view_->Close();
  if (!activation_committed && selection_before_search.has_value()) {
    // Close() clears the transient projection synchronously, so the original
    // selection can now be restored even when it was not one of the matches.
    std::ignore = controller_->SelectNode(*selection_before_search);
  }
  if (discovery_had_focus && focus_manager) {
    views::View* const restore = discovery_focus_restore_tracker_.view();
    if (GetWidget() && GetWidget()->IsActive()) {
      if (restore && restore->IsDrawn()) {
        restore->RequestFocus();
      } else if (tree_view_) {
        tree_view_->RequestFocus();
      }
    } else {
      focus_manager->ClearFocus();
    }
  }
  discovery_focus_restore_tracker_.SetView(nullptr);
  if (discovery_scroll_offset_.has_value() && !activation_committed) {
    const int restore_offset = *discovery_scroll_offset_;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<BrowserSidebarHostView> host, int offset) {
              if (host && host->scroll_view_ &&
                  host->scroll_view_->vertical_scroll_bar()) {
                host->scroll_view_->ScrollToPosition(
                    host->scroll_view_->vertical_scroll_bar(), offset);
              }
            },
            weak_ptr_factory_.GetWeakPtr(), restore_offset));
  }
  discovery_scroll_offset_.reset();
  discovery_selection_before_search_.reset();
  discovery_activation_committed_ = false;
  InvalidateLayout();
}

void BrowserSidebarHostView::ScheduleCloseSidebarDiscoveryAfterActivation() {
  if (!discovery_view_ || !discovery_view_->is_open()) {
    return;
  }
  discovery_activation_committed_ = true;
  // Inline rows belong to the primary sidebar and may disappear when the
  // filter clears. Let their current mouse/key dispatch unwind before the
  // projection is restored.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarHostView::CloseSidebarDiscovery,
                                weak_ptr_factory_.GetWeakPtr()));
}

std::set<std::string> BrowserSidebarHostView::ApplySidebarDiscoveryFilter(
    const std::u16string& query,
    const std::vector<SidebarDiscoveryItem>& items) {
  std::set<std::string> consumed_ids;
  sidebar_discovery_query_ = query;
  sidebar_discovery_runtime_tab_handles_.clear();
  sidebar_discovery_device_tab_ids_.clear();

  if (query.empty()) {
    controller_->ClearSearchMatches();
    if (discovery_view_ && discovery_view_->is_open() &&
        !discovery_activation_committed_ &&
        discovery_selection_before_search_.has_value()) {
      std::ignore =
          controller_->SelectNode(*discovery_selection_before_search_);
    }
    RefreshRuntimePresentation(/*refresh_auxiliary=*/false);
    return consumed_ids;
  }

  const std::optional<base::Uuid> active_workspace =
      controller_->view_model().workspace_id();
  std::vector<base::Uuid> tree_match_ids;
  std::vector<std::pair<base::Uuid, std::string>> pending_tree_consumed_items;
  for (const SidebarDiscoveryItem& item : items) {
    if (!item.command.has_value()) {
      continue;
    }
    const CommandItem& command = *item.command;
    switch (command.type) {
      case CommandItemType::kOpenTab: {
        tabs::TabInterface* const tab =
            session_bridge_->FindTabForOpenTabStableId(command.stable_id);
        if (!tab) {
          break;
        }
        const std::optional<base::Uuid> tab_workspace =
            session_bridge_->GetWorkspaceForTab(tab);
        if (active_workspace.has_value() && tab_workspace.has_value() &&
            active_workspace != tab_workspace) {
          break;
        }
        if (const std::optional<base::Uuid> node_id =
                session_bridge_->FindTreeNodeIdForTab(tab);
            node_id.has_value()) {
          tree_match_ids.push_back(*node_id);
          pending_tree_consumed_items.emplace_back(*node_id, item.stable_id);
        } else {
          sidebar_discovery_runtime_tab_handles_.insert(
              tab->GetHandle().raw_value());
          consumed_ids.insert(item.stable_id);
        }
        break;
      }
      case CommandItemType::kSavedPage:
      case CommandItemType::kFolder: {
        const base::Uuid node_id =
            base::Uuid::ParseLowercase(command.stable_id);
        if (!node_id.is_valid() || !active_workspace.has_value()) {
          break;
        }
        tree_match_ids.push_back(node_id);
        pending_tree_consumed_items.emplace_back(node_id, item.stable_id);
        break;
      }
      case CommandItemType::kDeviceTab:
        // Before auxiliary runtime presentation is ready there is no inline
        // remote row to consume this result. Keep it in the supplemental list
        // until the corresponding primary row can actually be rendered.
        if (runtime_auxiliary_ready_ &&
            ResolveDeviceTabCommand(device_tabs_snapshot_, command.stable_id,
                                    base::Time::Now())) {
          sidebar_discovery_device_tab_ids_.insert(command.stable_id);
          consumed_ids.insert(item.stable_id);
        }
        break;
      case CommandItemType::kWorkspace:
      case CommandItemType::kHistory:
      case CommandItemType::kBrowserCommand:
        break;
    }
  }

  const tab_tree::TabTreeStore::Result tree_result =
      controller_->SetSearchMatches(tree_match_ids);
  if (tree_result == tab_tree::TabTreeStore::Result::kOk) {
    for (const auto& [node_id, stable_id] : pending_tree_consumed_items) {
      if (controller_->view_model().IsSearchExactMatch(node_id)) {
        consumed_ids.insert(stable_id);
      }
    }
  } else {
    controller_->ClearSearchMatches();
  }
  RefreshRuntimePresentation(/*refresh_auxiliary=*/false);
  return consumed_ids;
}

void BrowserSidebarHostView::ClearSidebarDiscoveryPrimarySelection(
    bool restore_tree_selection) {
  if (!sidebar_discovery_primary_selection_.has_value() ||
      *sidebar_discovery_primary_selection_ >=
          sidebar_discovery_primary_results_.size()) {
    sidebar_discovery_primary_selection_.reset();
    return;
  }
  const SidebarDiscoveryPrimaryResult& result =
      sidebar_discovery_primary_results_[*sidebar_discovery_primary_selection_];
  switch (result.kind) {
    case SidebarDiscoveryPrimaryResultKind::kTreeNode:
      if (!restore_tree_selection || discovery_activation_committed_) {
        break;
      }
      if (discovery_selection_before_search_.has_value() &&
          controller_->view_model().GetRowForNode(
              *discovery_selection_before_search_)) {
        std::ignore =
            controller_->SelectNode(*discovery_selection_before_search_);
      } else {
        std::ignore = controller_->SelectNode(std::nullopt);
      }
      break;
    case SidebarDiscoveryPrimaryResultKind::kDeviceTab:
      SetRemoteTabSearchSelected(result.row, false);
      break;
    case SidebarDiscoveryPrimaryResultKind::kRuntimeTab:
      SetOpenTabSearchSelected(result.row, false);
      break;
  }
  sidebar_discovery_primary_selection_.reset();
}

void BrowserSidebarHostView::RebuildSidebarDiscoveryPrimaryResults() {
  sidebar_discovery_primary_results_.clear();
  sidebar_discovery_primary_selection_.reset();
  if (sidebar_discovery_query_.empty() || !tree_view_) {
    return;
  }

  for (const SidebarTreeViewModel::Row& row :
       controller_->view_model().rows()) {
    if (!controller_->view_model().IsSearchExactMatch(row.node_id) ||
        tree_view_->IsRuntimeCompositeSuppressedNode(row.node_id)) {
      continue;
    }
    sidebar_discovery_primary_results_.push_back(
        {.kind = SidebarDiscoveryPrimaryResultKind::kTreeNode,
         .node_id = row.node_id});
  }

  const auto collect_device_rows = [this](auto&& self, views::View* root) {
    if (!root) {
      return;
    }
    if (const std::optional<sync::RemoteTabRecord> tab =
            GetRemoteTabForView(root)) {
      sidebar_discovery_primary_results_.push_back(
          {.kind = SidebarDiscoveryPrimaryResultKind::kDeviceTab,
           .device_tab_stable_id = tab->device_id.AsLowercaseString() + ":" +
                                   tab->id.AsLowercaseString(),
           .row = root});
      return;
    }
    for (views::View* child : root->children()) {
      self(self, child);
    }
  };
  collect_device_rows(collect_device_rows, remote_tabs_container_);

  const auto collect_runtime_rows = [this](auto&& self, views::View* root) {
    if (!root) {
      return;
    }
    if (const base::WeakPtr<tabs::TabInterface> tab = GetOpenTabForView(root)) {
      const std::optional<base::Uuid> saved_node_id =
          GetSavedNodeForOpenTabView(root);
      const bool is_exact_match =
          saved_node_id.has_value()
              ? controller_->view_model().IsSearchExactMatch(*saved_node_id)
              : sidebar_discovery_runtime_tab_handles_.contains(
                    tab->GetHandle().raw_value());
      if (is_exact_match) {
        sidebar_discovery_primary_results_.push_back(
            {.kind = SidebarDiscoveryPrimaryResultKind::kRuntimeTab,
             .runtime_tab_handle = tab->GetHandle().raw_value(),
             .row = root});
      }
      return;
    }
    for (views::View* child : root->children()) {
      self(self, child);
    }
  };
  collect_runtime_rows(collect_runtime_rows, open_tabs_container_);
}

bool BrowserSidebarHostView::HandleSidebarDiscoveryPrimaryResult(
    SidebarDiscoveryView::PrimaryResultAction action) {
  const auto select_result = [this](size_t index) {
    if (index >= sidebar_discovery_primary_results_.size()) {
      return false;
    }
    ClearSidebarDiscoveryPrimarySelection();
    sidebar_discovery_primary_selection_ = index;
    const SidebarDiscoveryPrimaryResult& result =
        sidebar_discovery_primary_results_[index];
    switch (result.kind) {
      case SidebarDiscoveryPrimaryResultKind::kTreeNode:
        return controller_->SelectNode(result.node_id);
      case SidebarDiscoveryPrimaryResultKind::kDeviceTab:
        SetRemoteTabSearchSelected(result.row, true);
        if (result.row) {
          result.row->ScrollViewToVisible();
        }
        return result.row != nullptr;
      case SidebarDiscoveryPrimaryResultKind::kRuntimeTab:
        SetOpenTabSearchSelected(result.row, true);
        if (result.row) {
          result.row->ScrollViewToVisible();
        }
        return result.row != nullptr;
    }
    return false;
  };

  switch (action) {
    case SidebarDiscoveryView::PrimaryResultAction::kClearSelection:
      ClearSidebarDiscoveryPrimarySelection();
      return true;
    case SidebarDiscoveryView::PrimaryResultAction::kSelectFirst:
      return !sidebar_discovery_primary_results_.empty() && select_result(0u);
    case SidebarDiscoveryView::PrimaryResultAction::kSelectLast:
      return !sidebar_discovery_primary_results_.empty() &&
             select_result(sidebar_discovery_primary_results_.size() - 1u);
    case SidebarDiscoveryView::PrimaryResultAction::kSelectNext:
      return sidebar_discovery_primary_selection_.has_value() &&
             *sidebar_discovery_primary_selection_ + 1u <
                 sidebar_discovery_primary_results_.size() &&
             select_result(*sidebar_discovery_primary_selection_ + 1u);
    case SidebarDiscoveryView::PrimaryResultAction::kSelectPrevious:
      return sidebar_discovery_primary_selection_.has_value() &&
             *sidebar_discovery_primary_selection_ > 0u &&
             select_result(*sidebar_discovery_primary_selection_ - 1u);
    case SidebarDiscoveryView::PrimaryResultAction::kActivateSelection:
      break;
  }

  if (!sidebar_discovery_primary_selection_.has_value() ||
      *sidebar_discovery_primary_selection_ >=
          sidebar_discovery_primary_results_.size()) {
    return false;
  }
  const SidebarDiscoveryPrimaryResult result =
      sidebar_discovery_primary_results_[*sidebar_discovery_primary_selection_];
  switch (result.kind) {
    case SidebarDiscoveryPrimaryResultKind::kTreeNode: {
      const tab_tree::TreeNode* node =
          controller_->view_model().GetNode(result.node_id);
      if (!node) {
        return false;
      }
      if (node->type == tab_tree::TreeNodeType::kFolder) {
        ActivateFolderSearchResult(*node);
      } else {
        ActivateSavedPage(*node);
      }
      return true;
    }
    case SidebarDiscoveryPrimaryResultKind::kDeviceTab: {
      const sync::RemoteTabRecord* const tab = ResolveDeviceTabCommand(
          device_tabs_snapshot_, result.device_tab_stable_id,
          base::Time::Now());
      if (!tab) {
        return false;
      }
      discovery_activation_committed_ = true;
      if (!OpenRemoteTab(*tab)) {
        discovery_activation_committed_ = false;
        return false;
      }
      ScheduleCloseSidebarDiscoveryAfterActivation();
      return true;
    }
    case SidebarDiscoveryPrimaryResultKind::kRuntimeTab: {
      if (!tab_strip_model_) {
        return false;
      }
      tabs::TabInterface* matched_tab = nullptr;
      for (tabs::TabInterface* candidate : *tab_strip_model_) {
        if (candidate &&
            candidate->GetHandle().raw_value() == result.runtime_tab_handle) {
          matched_tab = candidate;
          break;
        }
      }
      if (!matched_tab) {
        return false;
      }
      ActivateRuntimeTab(matched_tab->GetWeakPtr());
      return true;
    }
  }
  return false;
}

bool BrowserSidebarHostView::ActivateSidebarDiscoveryCommand(
    const CommandItem& item) {
  if (!session_bridge_ || !browser_) {
    return false;
  }
  switch (item.type) {
    case CommandItemType::kOpenTab: {
      tabs::TabInterface* const tab =
          session_bridge_->FindTabForOpenTabStableId(item.stable_id);
      TabStripModel* const model =
          session_bridge_->FindTabStripModelForTab(tab);
      if (!tab || !model) {
        return false;
      }
      const int index = model->GetIndexOfTab(tab);
      BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
      if (index < 0 || !window || !window->GetWindow()) {
        return false;
      }
      discovery_activation_committed_ = true;
      const base::WeakPtr<tabs::TabInterface> weak_tab = tab->GetWeakPtr();
      model->ActivateTabAt(
          index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kKeyboard));
      if (weak_tab) {
        window = weak_tab->GetBrowserWindowInterface();
        if (window && window->GetWindow()) {
          window->GetWindow()->Activate();
        }
      }
      return true;
    }
    case CommandItemType::kSavedPage: {
      const base::Uuid node_id = base::Uuid::ParseLowercase(item.stable_id);
      tab_tree::TreeNode node;
      if (!node_id.is_valid() ||
          session_bridge_->tab_tree_store()->GetNode(node_id, &node) !=
              tab_tree::TabTreeStore::Result::kOk ||
          node.tombstone || node.type != tab_tree::TreeNodeType::kSavedPage ||
          !node.url.is_valid() || node.url.is_empty()) {
        return false;
      }
      discovery_activation_committed_ = true;
      if (session_bridge_->GetActiveWorkspaceForWindow(browser_) !=
              node.workspace_id &&
          !session_bridge_->SetActiveWorkspaceForWindow(
              browser_, node.workspace_id,
              WorkspaceActivationSource::kKeyboard)) {
        if (discovery_view_ && discovery_view_->is_open()) {
          discovery_activation_committed_ = false;
        }
        return false;
      }
      const bool activated =
          MaterializeSavedPage(node, /*require_local_model=*/false).valid;
      if (!activated && discovery_view_ && discovery_view_->is_open()) {
        discovery_activation_committed_ = false;
      }
      return activated;
    }
    case CommandItemType::kFolder: {
      const base::Uuid folder_id = base::Uuid::ParseLowercase(item.stable_id);
      if (!folder_id.is_valid()) {
        return false;
      }
      // Search forces ancestor paths open without mutating normal expansion.
      // Restore the real tree before revealing the selected folder.
      discovery_activation_committed_ = true;
      CloseSidebarDiscovery();
      return RevealFolder(folder_id);
    }
    case CommandItemType::kWorkspace: {
      const base::Uuid workspace_id =
          base::Uuid::ParseLowercase(item.stable_id);
      if (!workspace_id.is_valid()) {
        return false;
      }
      discovery_activation_committed_ = true;
      const bool activated = session_bridge_->SetActiveWorkspaceForWindow(
          browser_, workspace_id, WorkspaceActivationSource::kKeyboard);
      if (!activated && discovery_view_ && discovery_view_->is_open()) {
        discovery_activation_committed_ = false;
      }
      return activated;
    }
    case CommandItemType::kDeviceTab: {
      const sync::RemoteTabRecord* const tab = ResolveDeviceTabCommand(
          device_tabs_snapshot_, item.stable_id, base::Time::Now());
      if (!tab) {
        return false;
      }
      discovery_activation_committed_ = true;
      const bool opened = OpenRemoteTab(*tab);
      if (!opened && discovery_view_ && discovery_view_->is_open()) {
        discovery_activation_committed_ = false;
      }
      return opened;
    }
    case CommandItemType::kHistory:
    case CommandItemType::kBrowserCommand:
      return false;
  }
  return false;
}

bool BrowserSidebarHostView::RestoreSidebarDiscoveryEntry(SessionID entry_id) {
  if (!discovery_model_ || !browser_) {
    return false;
  }
  discovery_activation_committed_ = true;
  const bool restored = discovery_model_->RestoreRecentlyClosed(
      entry_id, browser_->GetFeatures().live_tab_context());
  if (!restored && discovery_view_ && discovery_view_->is_open()) {
    discovery_activation_committed_ = false;
  }
  return restored;
}

void BrowserSidebarHostView::ActivateFolderSearchResult(
    const tab_tree::TreeNode& node) {
  if (!discovery_view_ || !discovery_view_->is_open() ||
      node.type != tab_tree::TreeNodeType::kFolder || !node.id.is_valid()) {
    return;
  }
  discovery_activation_committed_ = true;
  // The clicked row is part of the transient projection. Let its current
  // event unwind, restore the normal tree, then reveal and select the folder.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> host, base::Uuid folder_id) {
            if (!host) {
              return;
            }
            host->CloseSidebarDiscovery();
            std::ignore = host->RevealFolder(folder_id);
          },
          weak_ptr_factory_.GetWeakPtr(), node.id));
}

}  // namespace ahoi::sidebar
