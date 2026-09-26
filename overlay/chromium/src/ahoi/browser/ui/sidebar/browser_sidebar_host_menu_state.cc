// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ui::SimpleMenuModel::Delegate queries for the sidebar's context menus:
// accelerators, checked and enabled state. The menus are built in
// browser_sidebar_host_context_menu.cc (split for the source line budget).

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
#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "ahoi/browser/popup/link_peek.h"
#include "ahoi/browser/navigation/navigation_input_prefs.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
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
#include "base/strings/strcat.h"
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
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
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

bool BrowserSidebarHostView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  std::string shortcut_id;
  const auto position = context_workspace_positions_.find(command_id);
  if (context_menu_scope_ == ContextMenuScope::kWorkspace &&
      position != context_workspace_positions_.end() && position->second < 9) {
    // Cmd+1..9 count the shared switcher's order (handoff 048).
    shortcut_id =
        shortcuts::kWorkspacePrefix + base::NumberToString(position->second + 1);
  } else if (command_id == kToggleFloatingSidebar) {
    shortcut_id = shortcuts::kToggleSidebarFloating;
  } else if (command_id == kToggleSidebarVisibility) {
    shortcut_id = shortcuts::kToggleSidebarVisibility;
  } else {
    return false;
  }
  const std::vector<ui::Accelerator> keys = shortcuts::EffectiveAccelerators(
      shortcuts::ReadOverrides(*browser_->GetProfile()->GetPrefs()),
      shortcut_id);
  if (keys.empty()) {
    return false;
  }
  *accelerator = keys.front();
  return true;
}

bool BrowserSidebarHostView::IsCommandIdChecked(int command_id) const {
  if (command_id >= kArchivePolicyCommandBase &&
      command_id < kArchivePolicyCommandBase + 5) {
    const auto workspace_id = context_archive_workspace_id_;
    const auto* workspace =
        workspace_id ? FindWorkspace(*workspace_id) : nullptr;
    return workspace && static_cast<int>(workspace->archive_policy) ==
                            command_id - kArchivePolicyCommandBase;
  }
  const PrefService* const prefs = browser_->GetProfile()->GetPrefs();
  if (command_id == kToggleWorkspaceSwipe) {
    return prefs->GetBoolean(
        ahoi::navigation_input_prefs::kWorkspaceSwipeEnabled);
  }
  if (command_id == kToggleCmdScrollTabSwitching) {
    return prefs->GetBoolean(ahoi::navigation_input_prefs::kCmdScrollEnabled);
  }
  if (command_id == kToggleMiddleClickAutoscroll) {
    return ahoi::navigation_input_prefs::IsMiddleClickAutoscrollEnabled(*prefs);
  }
  if (command_id == kToggleAutoPeek) {
    return prefs->GetBoolean(popup::kAutoPeekFromSavedPagesPref);
  }
  if (command_id == kTogglePeekOnShiftClick) {
    return prefs->GetBoolean(popup::kPeekOnShiftClickPref);
  }
  if (command_id == kToggleFloatingSidebar) {
    return BrowserView::GetBrowserViewForBrowser(browser_.get())
               ->GetAhoiSidebarPresentationMode() ==
           SidebarPresentationMode::kFloating;
  }
  if (command_id == kSplitSideBySide || command_id == kSplitStacked) {
    tabs::TabInterface* tab = nullptr;
    if (context_menu_scope_ == ContextMenuScope::kOpenTab) {
      tab = context_runtime_tab_.get();
    } else if (context_menu_scope_ == ContextMenuScope::kTree &&
               context_node_id_.has_value()) {
      tab = session_bridge_->FindTabByTreeNodeId(*context_node_id_);
    }
    TabStripModel* model = session_bridge_->FindTabStripModelForTab(tab);
    const split_tabs::SplitTabData* split_data =
        tab && model && tab->GetSplit().has_value()
            ? model->GetSplitData(*tab->GetSplit())
            : nullptr;
    if (!split_data || !split_data->visual_data()) {
      return false;
    }
    const split_tabs::SplitTabLayout layout =
        split_data->visual_data()->split_layout();
    return command_id == kSplitSideBySide
               ? layout == split_tabs::SplitTabLayout::kSideBySide
               : layout == split_tabs::SplitTabLayout::kStacked;
  }
  if (command_id == kToggleNeverSleep) {
    tabs::TabInterface* tab = nullptr;
    if (context_menu_scope_ == ContextMenuScope::kOpenTab) {
      tab = context_runtime_tab_.get();
    } else if (context_menu_scope_ == ContextMenuScope::kTree &&
               context_node_id_.has_value()) {
      tab = session_bridge_->FindTabByTreeNodeId(*context_node_id_);
    }
    return tab && ahoi::memory::IsNeverSleep(tab);
  }
  if (context_menu_scope_ != ContextMenuScope::kWorkspace ||
      command_id < kActivateWorkspaceCommandBase) {
    return false;
  }
  const size_t index =
      static_cast<size_t>(command_id - kActivateWorkspaceCommandBase);
  if (index >= context_workspace_ids_.size() || !window_id_.has_value()) {
    return false;
  }
  return workspace_service_->GetActiveWorkspace(*window_id_) ==
         context_workspace_ids_[index];
}

bool BrowserSidebarHostView::IsCommandIdEnabled(int command_id) const {
  if (command_id == kCopyActivePageLink ||
      command_id == kCopyActivePageMarkdownLink) {
    return IsContextPageActionTargetCurrent() &&
           CanCopyActivePageLink(browser_);
  }
  if (command_id == kOpenActivePageInReadingMode) {
    return IsContextPageActionTargetCurrent() &&
           CanOpenActivePageInReadingMode(browser_);
  }
  if (command_id == kArchiveTemporaryTab)
    return session_bridge_->CanArchiveTemporaryPages(ContextArchiveNodes());
  if (command_id == kArchiveList)
    return context_menu_scope_ == ContextMenuScope::kWorkspace;
  if (command_id == kArchivePolicy ||
      (command_id >= kArchivePolicyCommandBase &&
       command_id < kArchivePolicyCommandBase + 5))
    return context_menu_scope_ == ContextMenuScope::kWorkspace &&
           controller_->view_model().workspace_id().has_value();
  if (command_id == kGoToSavedHome || command_id == kSetSavedHome) {
    if (!context_node_id_)
      return false;
    const auto* node = controller_->view_model().GetNode(*context_node_id_);
    auto* tab = session_bridge_->FindTabByTreeNodeId(*context_node_id_);
    return node && !node->is_temporary &&
           (!tab || session_bridge_->FindTabStripModelForTab(tab) ==
                        tab_strip_model_) &&
           (command_id == kGoToSavedHome
                ? tab_tree::GetSharedHomeTarget(*node).has_value()
                : tab != nullptr);
  }
  if (context_menu_scope_ == ContextMenuScope::kArchive) {
    if (!context_archive_id_)
      return false;
    if (command_id == kRestoreArchiveOriginal)
      return true;
    if (command_id == kRestoreArchiveElsewhere)
      return !context_move_destinations_.empty();
    if (command_id >= kMoveToWorkspaceSubmenuCommandBase)
      return static_cast<size_t>(command_id -
                                 kMoveToWorkspaceSubmenuCommandBase) <
             context_move_submenu_models_.size();
    if (command_id >= kMoveToDestinationCommandBase)
      return static_cast<size_t>(command_id - kMoveToDestinationCommandBase) <
             context_move_destinations_.size();
    return false;
  }
  if (command_id == kMoveTo) {
    return (context_menu_scope_ == ContextMenuScope::kTree ||
            context_menu_scope_ == ContextMenuScope::kOpenTab) &&
           !context_move_destinations_.empty();
  }
  if (command_id >= kMoveToWorkspaceSubmenuCommandBase) {
    const size_t index =
        static_cast<size_t>(command_id - kMoveToWorkspaceSubmenuCommandBase);
    return (context_menu_scope_ == ContextMenuScope::kTree ||
            context_menu_scope_ == ContextMenuScope::kOpenTab) &&
           index < context_move_submenu_models_.size();
  }
  if (command_id >= kMoveToDestinationCommandBase) {
    const size_t index =
        static_cast<size_t>(command_id - kMoveToDestinationCommandBase);
    return ((context_menu_scope_ == ContextMenuScope::kTree &&
             context_node_id_.has_value()) ||
            (context_menu_scope_ == ContextMenuScope::kOpenTab &&
             context_runtime_tab_)) &&
           index < context_move_destinations_.size();
  }
  if (context_menu_scope_ == ContextMenuScope::kNone) {
    return false;
  }
  if (context_menu_scope_ == ContextMenuScope::kWorkspace) {
    switch (command_id) {
      case kCreateRootGroup:
      case kCreateWorkspace:
        return true;
      case kDuplicateWorkspace:
      case kCopyAllLinks:
      case kEditWorkspace:
        return controller_->view_model().workspace_id().has_value();
      case kConvertWorkspaceToIsolated:
        // The main Profile keeps at least one Workspace (handoff 052).
        return controller_->view_model().workspace_id().has_value() &&
               workspace_service_->ordered_workspaces().size() > 1 &&
               !session::IsIsolatedWorkspaceProfile(browser_->GetProfile());
      case kDeleteWorkspace:
        // A fully separated Workspace is its Profile's only Workspace;
        // deleting it deletes that Profile (ADR 0011).
        return controller_->view_model().workspace_id().has_value() &&
               (workspace_service_->ordered_workspaces().size() > 1 ||
                session::IsIsolatedWorkspaceProfile(browser_->GetProfile()));
      case kToggleFloatingSidebar:
      case kToggleSidebarVisibility:
      case kToggleWorkspaceSwipe:
      case kToggleCmdScrollTabSwitching:
      case kToggleMiddleClickAutoscroll:
      case kToggleAutoPeek:
      case kTogglePeekOnShiftClick:
        return true;
      default:
        break;
    }
    if (command_id == kOpenMainWorkspacesCommand) {
      return context_offers_main_workspaces_;
    }
    if (command_id >= kOpenMainWorkspaceCommandBase &&
        command_id < kOpenIsolatedWorkspaceCommandBase) {
      return static_cast<size_t>(command_id - kOpenMainWorkspaceCommandBase) <
             context_main_workspace_ids_.size();
    }
    if (command_id >= kOpenIsolatedWorkspaceCommandBase &&
        command_id < kActivateWorkspaceCommandBase) {
      return static_cast<size_t>(command_id -
                                 kOpenIsolatedWorkspaceCommandBase) <
             context_isolated_workspace_dirs_.size();
    }
    if (command_id < kActivateWorkspaceCommandBase) {
      return false;
    }
    const size_t index =
        static_cast<size_t>(command_id - kActivateWorkspaceCommandBase);
    return index < context_workspace_ids_.size();
  }
  if (context_menu_scope_ == ContextMenuScope::kOpenTab) {
    if (!context_runtime_tab_) {
      return false;
    }
    switch (command_id) {
      case kActivateNode:
      case kSaveTemporaryTab:
      case kCreateGroupAroundNode:
      case kDuplicateNode:
      case kCloseRuntimeTab:
      case kMoveTo:
        return true;
      case kToggleNeverSleep:
        return !ahoi::memory::GetNeverSleepKey(context_runtime_tab_->GetURL())
                    .empty();
      case kSleepTab:
        return ahoi::memory::CanSleepTab(context_runtime_tab_.get());
      case kWakeTab:
        return ahoi::memory::IsTabSleeping(context_runtime_tab_.get());
      case kSplitSideBySide:
      case kSplitStacked:
      case kReverseSplit:
      case kSeparateSplit:
        return context_runtime_tab_->GetSplit().has_value();
      default:
        return false;
    }
  }
  if (!context_node_id_.has_value()) {
    return command_id == kCreateRootGroup;
  }
  const tab_tree::TreeNode* node =
      controller_->view_model().GetNode(*context_node_id_);
  if (!node) {
    return false;
  }
  switch (command_id) {
    case kActivateNode:
    case kCreateGroupAroundNode:
    case kKeepOpenOnly:
      return node->type == tab_tree::TreeNodeType::kSavedPage;
    case kSeparateSplit: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
      return node->type == tab_tree::TreeNodeType::kSavedPage && tab &&
             tab->GetSplit().has_value();
    }
    case kSplitSideBySide:
    case kSplitStacked:
    case kReverseSplit: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
      return node->type == tab_tree::TreeNodeType::kSavedPage && tab &&
             tab->GetSplit().has_value();
    }
    case kToggleGroupExpanded:
    case kCreateSubgroup:
    case kCustomizeGroup:
    case kCopyAllLinks:
      return node->type == tab_tree::TreeNodeType::kFolder;
    case kDuplicateNode:
    case kRenameNode:
    case kDeleteNode:
      return true;
    case kCloseRuntimeTab:
      return node->type == tab_tree::TreeNodeType::kSavedPage &&
             session_bridge_->FindTabByTreeNodeId(node->id);
    case kSleepTab: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
      return node->type == tab_tree::TreeNodeType::kSavedPage &&
             ahoi::memory::CanSleepTab(tab);
    }
    case kWakeTab: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
      return node->type == tab_tree::TreeNodeType::kSavedPage &&
             ahoi::memory::IsTabSleeping(tab);
    }
    case kToggleNeverSleep: {
      tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
      return node->type == tab_tree::TreeNodeType::kSavedPage && tab &&
             !ahoi::memory::GetNeverSleepKey(tab->GetURL()).empty();
    }
    case kSaveTemporaryTab:
    case kMoveTo:
      return false;
    case kCreateRootGroup:
    case kCreateWorkspace:
    case kDuplicateWorkspace:
    case kEditWorkspace:
    case kDeleteWorkspace:
      return false;
    default:
      return false;
  }
}

}  // namespace ahoi::sidebar
