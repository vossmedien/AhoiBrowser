// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>
#include <tuple>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::sidebar {

// ADR 0012 section 2 (handoff 082): the command bar's "In Workspace
// verschieben". Same paths as the context menu's "Move to" with the
// Workspace root as destination: the selected folder, else the active saved
// page (with its split group), else the active temporary tab.
bool BrowserSidebarHostView::MoveSelectionToWorkspace(
    const base::Uuid& workspace_id,
    bool dry_run) {
  if (!session_bridge_ || !workspace_service_ || !controller_ ||
      !tab_strip_model_ || !workspace_id.is_valid() ||
      controller_->view_model().workspace_id() == workspace_id ||
      std::ranges::none_of(workspace_service_->ordered_workspaces(),
                           [&workspace_id](const auto& workspace) {
                             return workspace.id == workspace_id;
                           })) {
    return false;
  }
  const SidebarTreeController::DropTarget drop_target = {
      .workspace_id = workspace_id,
      .target_node_id = std::nullopt,
      .position = SidebarTreeController::DropPosition::kInside};
  tabs::TabInterface* const active = tab_strip_model_->GetActiveTab();

  std::optional<base::Uuid> node_id;
  if (const std::optional<base::Uuid>& selected =
          controller_->view_model().selected_node_id()) {
    const tab_tree::TreeNode* node =
        controller_->view_model().GetNode(*selected);
    if (node && node->type == tab_tree::TreeNodeType::kFolder) {
      node_id = *selected;
    }
  }
  if (!node_id && active) {
    // Saved rows only; a temporary tab takes the save path below.
    node_id = session_bridge_->FindTreeNodeIdForTab(active);
  }

  if (node_id) {
    if (dry_run) {
      return true;
    }
    const std::vector<base::Uuid> source_ids = GetMoveGroupNodeIds(*node_id);
    if (source_ids.empty()) {
      OnMutationFailed(tab_tree::TabTreeStore::Result::kInvalidArgument);
      return false;
    }
    const bool moved_active_tab =
        active &&
        std::ranges::any_of(source_ids, [this, active](const base::Uuid& id) {
          return session_bridge_->FindTabByTreeNodeId(id) == active;
        });
    const SidebarTreeController::DropExecutionResult result =
        controller_->PerformGroupedDrop(
            source_ids, drop_target,
            SidebarTreeController::DropOperation::kMove, base::Time::Now());
    if (!result.ok()) {
      OnMutationFailed(result.store_result);
      return false;
    }
    if (moved_active_tab) {
      // Handoff 011 S1: the window's Workspace follows the moved active tab.
      std::ignore = session_bridge_->SetActiveWorkspaceForWindow(
          browser_, workspace_id, WorkspaceActivationSource::kKeyboard);
    }
    return true;
  }

  if (!active || !FindTemporaryTab(active->GetHandle().raw_value())) {
    return false;
  }
  if (dry_run) {
    return true;
  }
  if (!SaveTemporaryTabAtDrop(active->GetHandle().raw_value(), drop_target,
                              nullptr)) {
    return false;
  }
  std::ignore = session_bridge_->SetActiveWorkspaceForWindow(
      browser_, workspace_id, WorkspaceActivationSource::kKeyboard);
  return true;
}

}  // namespace ahoi::sidebar
