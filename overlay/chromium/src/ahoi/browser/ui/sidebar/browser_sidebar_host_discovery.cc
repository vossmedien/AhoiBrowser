// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_device_tab_commands.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
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
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::OnSidebarDiscoveryPressed(const ui::Event&) {
  // Toggling replaces a sibling surface. Defer until the native Button has
  // finished dispatching so layout never invalidates its current event path.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarHostView::ToggleSidebarDiscovery,
                                weak_ptr_factory_.GetWeakPtr()));
}

void BrowserSidebarHostView::ToggleSidebarDiscovery() {
  if (!discovery_view_) {
    return;
  }
  if (discovery_view_->GetVisible()) {
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
    if (focused && focused != discovery_view_ &&
        !discovery_view_->Contains(focused)) {
      discovery_focus_restore_tracker_.SetView(focused);
    }
  }
  if (tab_preview_controller_) {
    tab_preview_controller_->Hide();
  }
  InvalidateAndCloseGroupRecentBubble();
  ResetDragPresentation();
  media_overlay_view_->SetVisible(false);
  discovery_view_->SetVisible(true);
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
  const bool discovery_had_focus =
      focused &&
      (focused == discovery_view_ || discovery_view_->Contains(focused));
  discovery_view_->Reset();
  discovery_view_->SetVisible(false);
  media_overlay_view_->SetVisible(true);
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
  InvalidateLayout();
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
      if (session_bridge_->GetActiveWorkspaceForWindow(browser_) !=
              node.workspace_id &&
          !session_bridge_->SetActiveWorkspaceForWindow(
              browser_, node.workspace_id,
              WorkspaceActivationSource::kKeyboard)) {
        return false;
      }
      return MaterializeSavedPage(node, /*require_local_model=*/false).valid;
    }
    case CommandItemType::kFolder: {
      const base::Uuid folder_id = base::Uuid::ParseLowercase(item.stable_id);
      return folder_id.is_valid() && RevealFolder(folder_id);
    }
    case CommandItemType::kWorkspace: {
      const base::Uuid workspace_id =
          base::Uuid::ParseLowercase(item.stable_id);
      return workspace_id.is_valid() &&
             session_bridge_->SetActiveWorkspaceForWindow(
                 browser_, workspace_id, WorkspaceActivationSource::kKeyboard);
    }
    case CommandItemType::kDeviceTab: {
      const sync::RemoteTabRecord* const tab = ResolveDeviceTabCommand(
          device_tabs_snapshot_, item.stable_id, base::Time::Now());
      if (!tab) {
        return false;
      }
      OpenRemoteTab(*tab);
      return true;
    }
    case CommandItemType::kHistory:
    case CommandItemType::kBrowserCommand:
      return false;
  }
  return false;
}

bool BrowserSidebarHostView::RestoreSidebarDiscoveryEntry(SessionID entry_id) {
  return discovery_model_ && browser_ &&
         discovery_model_->RestoreRecentlyClosed(
             entry_id, browser_->GetFeatures().live_tab_context());
}

}  // namespace ahoi::sidebar
