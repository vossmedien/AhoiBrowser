// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/memory/tab_sleeping.h"
#include "ahoi/browser/navigation/navigation_input_prefs.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
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
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_service.h"
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

void BrowserSidebarHostView::ExecuteCommand(int command_id, int) {
  if (context_menu_scope_ == ContextMenuScope::kNone) {
    return;
  }
  if ((context_menu_scope_ == ContextMenuScope::kTree ||
       context_menu_scope_ == ContextMenuScope::kOpenTab) &&
      command_id >= kMoveToDestinationCommandBase &&
      command_id < kMoveToWorkspaceSubmenuCommandBase) {
    const size_t index =
        static_cast<size_t>(command_id - kMoveToDestinationCommandBase);
    if (index >= context_move_destinations_.size()) {
      return;
    }
    const ContextMoveDestination& destination =
        context_move_destinations_[index];
    const SidebarTreeController::DropTarget drop_target = {
        .workspace_id = destination.workspace_id,
        .target_node_id = destination.folder_id,
        .position = SidebarTreeController::DropPosition::kInside};

    if (context_menu_scope_ == ContextMenuScope::kOpenTab) {
      tabs::TabInterface* tab = context_runtime_tab_.get();
      if (!tab) {
        return;
      }
      const bool was_active = tab_strip_model_->GetActiveTab() == tab;
      if (SaveTemporaryTabAtDrop(tab->GetHandle().raw_value(), drop_target,
                                 nullptr) &&
          was_active) {
        ActivateWorkspace(destination.workspace_id);
      }
      return;
    }

    if (!context_node_id_.has_value()) {
      return;
    }
    const std::vector<base::Uuid> source_ids =
        GetMoveGroupNodeIds(*context_node_id_);
    const bool moved_active_tab =
        std::ranges::any_of(source_ids, [this](const base::Uuid& node_id) {
          return session_bridge_->FindTabByTreeNodeId(node_id) ==
                 tab_strip_model_->GetActiveTab();
        });
    SidebarTreeController::DropExecutionResult result =
        controller_->PerformGroupedDrop(
            source_ids, drop_target,
            SidebarTreeController::DropOperation::kMove, base::Time::Now());
    if (!result.ok()) {
      OnMutationFailed(result.store_result);
      return;
    }
    if (destination.folder_id.has_value() &&
        controller_->view_model().workspace_id() == destination.workspace_id) {
      std::ignore = controller_->ExpandNode(*destination.folder_id);
    }
    if (moved_active_tab) {
      ActivateWorkspace(destination.workspace_id);
    }
    return;
  }
  if (context_menu_scope_ == ContextMenuScope::kWorkspace) {
    PrefService* const prefs = browser_->GetProfile()->GetPrefs();
    if (command_id == kToggleWorkspaceSwipe) {
      prefs->SetBoolean(
          ahoi::navigation_input_prefs::kWorkspaceSwipeEnabled,
          !prefs->GetBoolean(
              ahoi::navigation_input_prefs::kWorkspaceSwipeEnabled));
      return;
    }
    if (command_id == kToggleCmdScrollTabSwitching) {
      prefs->SetBoolean(
          ahoi::navigation_input_prefs::kCmdScrollEnabled,
          !prefs->GetBoolean(ahoi::navigation_input_prefs::kCmdScrollEnabled));
      return;
    }
    if (command_id == kToggleMiddleClickAutoscroll) {
      prefs->SetBoolean(
          ahoi::navigation_input_prefs::kMiddleClickAutoscrollEnabled,
          !ahoi::navigation_input_prefs::IsMiddleClickAutoscrollEnabled(
              *prefs));
      return;
    }
    if (command_id == kToggleFloatingSidebar) {
      ToggleFloatingSidebar();
      return;
    }
    if (command_id == kToggleSidebarVisibility) {
      ToggleSidebarVisibility();
      return;
    }
    if (command_id == kCreateRootGroup) {
      ShowCreateRootGroupDialog();
      return;
    }
    if (command_id == kCreateWorkspace) {
      ShowWorkspaceDialog(PendingWorkspaceAction::kCreate, std::nullopt);
      return;
    }
    const std::optional<base::Uuid> active_workspace_id =
        controller_->view_model().workspace_id();
    if (command_id == kDuplicateWorkspace && active_workspace_id.has_value()) {
      ShowWorkspaceDialog(PendingWorkspaceAction::kDuplicate,
                          *active_workspace_id);
      return;
    }
    if (command_id == kCopyAllLinks && active_workspace_id.has_value()) {
      CopyAllLinksInWorkspace(*active_workspace_id);
      return;
    }
    if (command_id == kEditWorkspace && active_workspace_id.has_value()) {
      ShowWorkspaceDialog(PendingWorkspaceAction::kEdit, *active_workspace_id);
      return;
    }
    if (command_id == kDeleteWorkspace && active_workspace_id.has_value()) {
      ShowWorkspaceDialog(PendingWorkspaceAction::kDelete,
                          *active_workspace_id);
      return;
    }
    if (command_id < kActivateWorkspaceCommandBase || !window_id_.has_value()) {
      return;
    }
    const size_t index =
        static_cast<size_t>(command_id - kActivateWorkspaceCommandBase);
    if (index < context_workspace_ids_.size()) {
      std::ignore = session_bridge_->SetActiveWorkspaceForWindow(
          browser_, context_workspace_ids_[index],
          WorkspaceActivationSource::kSidebar);
    }
    return;
  }
  if (context_menu_scope_ == ContextMenuScope::kOpenTab) {
    tabs::TabInterface* tab = context_runtime_tab_.get();
    if (!tab) {
      return;
    }
    const int runtime_handle = tab->GetHandle().raw_value();
    switch (command_id) {
      case kActivateNode:
        ActivateRuntimeTab(tab->GetWeakPtr());
        return;
      case kSaveTemporaryTab:
        std::ignore = SaveTemporaryTabAtWorkspaceRoot(runtime_handle, nullptr);
        return;
      case kCreateGroupAroundNode:
        ShowCreateGroupDialogForTemporaryTab(runtime_handle);
        return;
      case kSplitSideBySide:
      case kSplitStacked:
      case kReverseSplit:
      case kSeparateSplit: {
        TabStripModel* model = session_bridge_->FindTabStripModelForTab(tab);
        if (model && tab->GetSplit().has_value()) {
          const split_tabs::SplitTabId split_id = *tab->GetSplit();
          if (command_id == kSplitSideBySide) {
            model->UpdateSplitLayout(split_id,
                                     split_tabs::SplitTabLayout::kSideBySide);
          } else if (command_id == kSplitStacked) {
            model->UpdateSplitLayout(split_id,
                                     split_tabs::SplitTabLayout::kStacked);
          } else if (command_id == kReverseSplit) {
            model->ReverseTabsInSplit(split_id);
          } else {
            model->RemoveSplit(split_id);
          }
        }
        return;
      }
      case kDuplicateNode: {
        // Use Chromium's native clone path instead of navigating the current
        // URL. The clone preserves history/form state and is inserted next to
        // the source as a temporary tab; it deliberately does not mutate or
        // extend an existing split group.
        const int index = tab_strip_model_->GetIndexOfTab(tab);
        if (index >= 0) {
          chrome::DuplicateTabAt(browser_, index);
        }
        return;
      }
      case kCloseRuntimeTab:
        tab->Close();
        return;
      case kSleepTab:
        if (ahoi::memory::SleepTab(tab)) {
          ScheduleRuntimePresentationRefresh();
        }
        return;
      case kWakeTab:
        if (ahoi::memory::WakeTab(tab)) {
          ScheduleRuntimePresentationRefresh();
        }
        return;
      case kToggleNeverSleep:
        if (ahoi::memory::SetNeverSleep(tab,
                                        !ahoi::memory::IsNeverSleep(tab))) {
          ScheduleRuntimePresentationRefresh();
        }
        return;
      default:
        return;
    }
  }
  if (!context_node_id_.has_value()) {
    if (command_id == kCreateRootGroup) {
      ShowCreateRootGroupDialog();
    }
    return;
  }
  const base::Uuid node_id = *context_node_id_;
  const tab_tree::TreeNode* node = controller_->view_model().GetNode(node_id);
  if (!node) {
    return;
  }
  switch (command_id) {
    case kActivateNode:
      ActivateSavedPage(*node);
      return;
    case kToggleGroupExpanded:
      if (node->type != tab_tree::TreeNodeType::kFolder) {
        return;
      }
      if (controller_->view_model().IsExpanded(node_id)) {
        std::ignore = controller_->CollapseNode(node_id);
      } else {
        const tab_tree::TabTreeStore::Result result =
            controller_->ExpandNode(node_id);
        if (result != tab_tree::TabTreeStore::Result::kOk) {
          OnMutationFailed(result);
        }
      }
      return;
    case kDuplicateNode: {
      const bool duplicated_folder =
          node->type == tab_tree::TreeNodeType::kFolder;
      SidebarTreeController::DropExecutionResult result =
          controller_->PerformDrop(
              node_id,
              {.workspace_id = node->workspace_id,
               .target_node_id = node_id,
               .position = SidebarTreeController::DropPosition::kAfter},
              SidebarTreeController::DropOperation::kCopy, base::Time::Now());
      if (!result.ok() || !result.copied_root_id.has_value()) {
        OnMutationFailed(result.store_result);
        return;
      }
      std::ignore = controller_->SelectNode(*result.copied_root_id);
      if (duplicated_folder) {
        std::ignore = controller_->ExpandNode(*result.copied_root_id);
      }
      return;
    }
    case kRenameNode:
      std::ignore = controller_->SelectNode(node_id);
      if (tree_view_) {
        tree_view_->BeginRenameSelectedNode();
      }
      return;
    case kCreateSubgroup:
      ShowCreateSubgroupDialog(node_id);
      return;
    case kCustomizeGroup:
      ShowGroupCustomizationDialog(node_id);
      return;
    case kCopyAllLinks:
      CopyAllLinksInGroup(node_id);
      return;
    case kCreateGroupAroundNode:
      ShowCreateGroupDialog(node_id);
      return;
    case kKeepOpenOnly:
      std::ignore = MakeSavedPageTemporary(node_id);
      return;
    case kSeparateSplit: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node_id);
      TabStripModel* model = session_bridge_->FindTabStripModelForTab(tab);
      if (tab && model && tab->GetSplit().has_value()) {
        model->RemoveSplit(*tab->GetSplit());
      }
      return;
    }
    case kSplitSideBySide:
    case kSplitStacked:
    case kReverseSplit: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node_id);
      TabStripModel* model = session_bridge_->FindTabStripModelForTab(tab);
      if (tab && model && tab->GetSplit().has_value()) {
        const split_tabs::SplitTabId split_id = *tab->GetSplit();
        if (command_id == kSplitSideBySide) {
          model->UpdateSplitLayout(split_id,
                                   split_tabs::SplitTabLayout::kSideBySide);
        } else if (command_id == kSplitStacked) {
          model->UpdateSplitLayout(split_id,
                                   split_tabs::SplitTabLayout::kStacked);
        } else {
          model->ReverseTabsInSplit(split_id);
        }
      }
      return;
    }
    case kDeleteNode: {
      const tab_tree::TabTreeStore::Result result =
          controller_->DeleteNode(node_id, base::Time::Now());
      if (result != tab_tree::TabTreeStore::Result::kOk) {
        OnMutationFailed(result);
      }
      return;
    }
    case kCloseRuntimeTab:
      if (tabs::TabInterface* tab =
              session_bridge_->FindTabByTreeNodeId(node_id)) {
        tab->Close();
      }
      return;
    case kSleepTab:
      if (tabs::TabInterface* tab =
              session_bridge_->FindTabByTreeNodeId(node_id)) {
        if (ahoi::memory::SleepTab(tab)) {
          ScheduleRuntimePresentationRefresh();
        }
      }
      return;
    case kWakeTab:
      if (tabs::TabInterface* tab =
              session_bridge_->FindTabByTreeNodeId(node_id)) {
        if (ahoi::memory::WakeTab(tab)) {
          ScheduleRuntimePresentationRefresh();
        }
      }
      return;
    case kToggleNeverSleep:
      if (tabs::TabInterface* tab =
              session_bridge_->FindTabByTreeNodeId(node_id)) {
        if (ahoi::memory::SetNeverSleep(tab,
                                        !ahoi::memory::IsNeverSleep(tab))) {
          ScheduleRuntimePresentationRefresh();
        }
      }
      return;
    case kCreateRootGroup:
    case kSaveTemporaryTab:
    case kMoveTo:
      return;
    default:
      return;
  }
}

}  // namespace ahoi::sidebar
