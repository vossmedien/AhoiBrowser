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

// views::ContextMenuController:
void BrowserSidebarHostView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (source == workspace_button_) {
    ShowWorkspaceMenu(screen_point, source_type);
    return;
  }
  if (const std::optional<base::Uuid> saved_node_id =
          GetSavedNodeForOpenTabView(source);
      saved_node_id.has_value() &&
      controller_->view_model().GetNode(*saved_node_id)) {
    ShowNodeContextMenu(saved_node_id, screen_point, source_type);
    return;
  }
  if (base::WeakPtr<tabs::TabInterface> open_tab = GetOpenTabForView(source)) {
    ShowOpenTabContextMenu(std::move(open_tab), screen_point, source_type);
  }
}

void BrowserSidebarHostView::ShowOpenTabContextMenu(
    base::WeakPtr<tabs::TabInterface> tab,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!tab || !GetWidget() || context_menu_scope_ != ContextMenuScope::kNone) {
    return;
  }
  context_runtime_tab_ = tab;
  context_menu_scope_ = ContextMenuScope::kOpenTab;
  context_node_id_.reset();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_model_->AddItem(
      kActivateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_OPEN));
  context_menu_model_->AddItem(
      kSaveTemporaryTab,
      l10n_util::GetStringUTF16(IDS_STAR_VIEW_MENU_ADD_BOOKMARK));
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  const bool sleeping = ahoi::memory::IsTabSleeping(tab.get());
  context_menu_model_->AddItem(
      sleeping ? kWakeTab : kSleepTab,
      l10n_util::GetStringUTF16(sleeping ? IDS_AHOI_CONTEXT_WAKE_TAB
                                         : IDS_AHOI_CONTEXT_SLEEP_TAB));
  context_menu_model_->AddCheckItem(
      kToggleNeverSleep,
      l10n_util::GetStringUTF16(ahoi::memory::IsNeverSleep(tab.get())
                                    ? IDS_AHOI_CONTEXT_ALLOW_SLEEP_SITE
                                    : IDS_AHOI_CONTEXT_NEVER_SLEEP_SITE));
  if (tab->GetSplit().has_value()) {
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddCheckItem(
        kSplitSideBySide,
        l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_SIDE_BY_SIDE));
    context_menu_model_->AddCheckItem(
        kSplitStacked, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_STACKED));
    context_menu_model_->AddItem(
        kReverseSplit, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_REVERSE_VIEWS));
    context_menu_model_->AddItem(
        kSeparateSplit,
        l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SEPARATE_VIEWS));
  }
  context_menu_model_->AddItem(
      kCreateGroupAroundNode,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_GROUP_WITH_TAB));
  if (BuildMoveToMenu(nullptr)) {
    context_menu_model_->AddSubMenu(
        kMoveTo, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO),
        context_move_menu_model_.get());
  }
  context_menu_model_->AddItem(
      kDuplicateNode, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_DUPLICATE));
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(
      kCloseRuntimeTab, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_CLOSETAB));

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_menu_model_.reset();
  context_move_menu_model_.reset();
  context_move_submenu_models_.clear();
  context_move_destinations_.clear();
  context_runtime_tab_.reset();
  context_menu_scope_ = ContextMenuScope::kNone;
}

void BrowserSidebarHostView::ShowWorkspaceMenu(
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!GetWidget() || context_menu_scope_ != ContextMenuScope::kNone) {
    return;
  }
  if (!window_id_.has_value()) {
    window_id_ = session_bridge_->GetWindowId(browser_);
  }
  context_menu_scope_ = ContextMenuScope::kWorkspace;
  context_node_id_.reset();
  context_workspace_ids_.clear();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    const int command_id = kActivateWorkspaceCommandBase +
                           static_cast<int>(context_workspace_ids_.size());
    context_workspace_ids_.push_back(workspace.id);
    context_menu_model_->AddCheckItem(command_id, workspace.name);
  }
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(
      kCreateWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_WORKSPACE));
  context_menu_model_->AddItem(
      kDuplicateWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DUPLICATE));
  context_menu_model_->AddItem(
      kEditWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_EDIT_WORKSPACE));
  context_menu_model_->AddItem(
      kCopyAllLinks,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_COPY_ALL_LINKS));
  context_menu_model_->AddItem(
      kDeleteWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DELETE_WORKSPACE));
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddCheckItem(
      kToggleFloatingSidebar,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_FLOATING_SIDEBAR));
  context_menu_model_->AddItem(
      kToggleSidebarVisibility,
      l10n_util::GetStringUTF16(
          browser_->GetBrowserView().GetAhoiSidebarPresentationMode() ==
                  SidebarPresentationMode::kHidden
              ? IDS_AHOI_CONTEXT_SHOW_SIDEBAR
              : IDS_AHOI_CONTEXT_HIDE_SIDEBAR));
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddCheckItem(
      kToggleWorkspaceSwipe,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_WORKSPACE_SWIPE));
  context_menu_model_->AddCheckItem(
      kToggleCmdScrollTabSwitching,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_CMD_SCROLL_TAB_SWITCHING));
  context_menu_model_->AddCheckItem(
      kToggleMiddleClickAutoscroll,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_MIDDLE_CLICK_AUTOSCROLL));
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(
      kCreateRootGroup,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_ROOT_GROUP));
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_menu_model_.reset();
  context_workspace_ids_.clear();
  context_menu_scope_ = ContextMenuScope::kNone;
}

void BrowserSidebarHostView::ShowNodeContextMenu(
    std::optional<base::Uuid> node_id,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  const tab_tree::TreeNode* node =
      node_id.has_value() ? controller_->view_model().GetNode(*node_id)
                          : nullptr;
  if ((node_id.has_value() && !node) || !GetWidget() ||
      context_menu_scope_ != ContextMenuScope::kNone) {
    return;
  }
  context_node_id_ = node_id;
  context_menu_scope_ = ContextMenuScope::kTree;
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  if (!node) {
    context_menu_model_->AddItem(
        kCreateRootGroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_ROOT_GROUP));
  } else if (node->type == tab_tree::TreeNodeType::kSavedPage) {
    context_menu_model_->AddItem(
        kActivateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_OPEN));
    tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
    if (tab && tab->GetSplit().has_value()) {
      context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      context_menu_model_->AddCheckItem(
          kSplitSideBySide,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_SIDE_BY_SIDE));
      context_menu_model_->AddCheckItem(
          kSplitStacked, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_STACKED));
      context_menu_model_->AddItem(
          kReverseSplit,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_REVERSE_VIEWS));
      context_menu_model_->AddItem(
          kSeparateSplit,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SEPARATE_VIEWS));
    }
    if (tab) {
      context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      const bool sleeping = ahoi::memory::IsTabSleeping(tab);
      context_menu_model_->AddItem(
          sleeping ? kWakeTab : kSleepTab,
          l10n_util::GetStringUTF16(sleeping ? IDS_AHOI_CONTEXT_WAKE_TAB
                                             : IDS_AHOI_CONTEXT_SLEEP_TAB));
      context_menu_model_->AddCheckItem(
          kToggleNeverSleep,
          l10n_util::GetStringUTF16(ahoi::memory::IsNeverSleep(tab)
                                        ? IDS_AHOI_CONTEXT_ALLOW_SLEEP_SITE
                                        : IDS_AHOI_CONTEXT_NEVER_SLEEP_SITE));
    }
    context_menu_model_->AddItem(
        kKeepOpenOnly, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_UNPIN_TAB));
    if (tab) {
      context_menu_model_->AddItem(
          kCloseRuntimeTab, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_CLOSETAB));
    }
    context_menu_model_->AddItem(
        kCreateGroupAroundNode,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_GROUP_WITH_TAB));
  } else {
    context_menu_model_->AddItem(
        kToggleGroupExpanded,
        l10n_util::GetStringUTF16(controller_->view_model().IsExpanded(node->id)
                                      ? IDS_AHOI_CONTEXT_COLLAPSE_GROUP
                                      : IDS_AHOI_CONTEXT_EXPAND_GROUP));
    context_menu_model_->AddItem(
        kCreateSubgroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_SUBGROUP));
    context_menu_model_->AddItem(
        kCopyAllLinks,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_COPY_ALL_LINKS));
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddItem(
        kCustomizeGroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_CUSTOMIZE_GROUP));
  }
  if (node) {
    context_menu_model_->AddItem(
        kDuplicateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DUPLICATE));
    if (BuildMoveToMenu(node)) {
      context_menu_model_->AddSubMenu(
          kMoveTo, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO),
          context_move_menu_model_.get());
    }
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddItem(
        kRenameNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_RENAME));
    context_menu_model_->AddItem(
        kDeleteNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO_TRASH));
  }

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_menu_model_.reset();
  context_move_menu_model_.reset();
  context_move_submenu_models_.clear();
  context_move_destinations_.clear();
  context_node_id_.reset();
  context_menu_scope_ = ContextMenuScope::kNone;
}

std::optional<int> BrowserSidebarHostView::AddMoveDestinationCommand(
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> folder_id) {
  // Destination IDs and submenu IDs share the same native menu tree. Keep
  // the two ranges provably disjoint even for unusually large profiles.
  constexpr size_t kMaximumDestinationCount =
      kMoveToWorkspaceSubmenuCommandBase - kMoveToDestinationCommandBase;
  if (context_move_destinations_.size() >= kMaximumDestinationCount) {
    return std::nullopt;
  }
  const int command_id = kMoveToDestinationCommandBase +
                         static_cast<int>(context_move_destinations_.size());
  context_move_destinations_.push_back(
      {.workspace_id = workspace_id, .folder_id = std::move(folder_id)});
  return command_id;
}

void BrowserSidebarHostView::AppendMoveDestinationFolder(
    ui::SimpleMenuModel* parent_menu,
    const base::Uuid& workspace_id,
    const MoveDestinationFolder& folder) {
  CHECK(parent_menu);
  if (folder.children.empty() && folder.selectable) {
    if (const std::optional<int> command_id =
            AddMoveDestinationCommand(workspace_id, folder.id)) {
      parent_menu->AddItem(*command_id, folder.title);
    }
    return;
  }

  auto submenu = std::make_unique<ui::SimpleMenuModel>(this);
  if (folder.selectable) {
    if (const std::optional<int> command_id =
            AddMoveDestinationCommand(workspace_id, folder.id)) {
      submenu->AddItem(*command_id,
                       l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_THIS_GROUP));
    }
  }
  if (submenu->GetItemCount() > 0 && !folder.children.empty()) {
    submenu->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  for (const MoveDestinationFolder& child : folder.children) {
    AppendMoveDestinationFolder(submenu.get(), workspace_id, child);
  }
  if (submenu->GetItemCount() == 0) {
    return;
  }

  const int submenu_command_id =
      kMoveToWorkspaceSubmenuCommandBase +
      static_cast<int>(context_move_submenu_models_.size());
  ui::SimpleMenuModel* raw_submenu = submenu.get();
  context_move_submenu_models_.push_back(std::move(submenu));
  parent_menu->AddSubMenu(submenu_command_id, folder.title, raw_submenu);
}

bool BrowserSidebarHostView::BuildMoveToMenu(const tab_tree::TreeNode* source) {
  context_move_destinations_.clear();
  context_move_submenu_models_.clear();
  context_move_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);

  tab_tree::TabTreeSnapshot snapshot;
  if (session_bridge_->tab_tree_store()->ExportSnapshot(&snapshot) !=
      tab_tree::TabTreeStore::Result::kOk) {
    context_move_menu_model_.reset();
    return false;
  }

  const std::vector<MoveDestinationWorkspace> workspaces =
      BuildMoveDestinationMenuModel(workspace_service_->ordered_workspaces(),
                                    snapshot, source);
  for (const MoveDestinationWorkspace& workspace : workspaces) {
    auto workspace_menu = std::make_unique<ui::SimpleMenuModel>(this);
    if (workspace.root_selectable) {
      if (const std::optional<int> command_id =
              AddMoveDestinationCommand(workspace.id, std::nullopt)) {
        workspace_menu->AddItem(
            *command_id,
            l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_WORKSPACE_ROOT));
      }
    }
    if (workspace_menu->GetItemCount() > 0 && !workspace.folders.empty()) {
      workspace_menu->AddSeparator(ui::NORMAL_SEPARATOR);
    }
    for (const MoveDestinationFolder& folder : workspace.folders) {
      AppendMoveDestinationFolder(workspace_menu.get(), workspace.id, folder);
    }
    if (workspace_menu->GetItemCount() == 0) {
      continue;
    }

    std::u16string workspace_label = workspace.name;
    if (!workspace.icon.empty()) {
      workspace_label = workspace.icon + u"  " + workspace_label;
    }
    const int submenu_command_id =
        kMoveToWorkspaceSubmenuCommandBase +
        static_cast<int>(context_move_submenu_models_.size());
    ui::SimpleMenuModel* raw_workspace_menu = workspace_menu.get();
    context_move_submenu_models_.push_back(std::move(workspace_menu));
    context_move_menu_model_->AddSubMenu(submenu_command_id, workspace_label,
                                         raw_workspace_menu);
  }
  if (context_move_destinations_.empty()) {
    context_move_menu_model_.reset();
    context_move_submenu_models_.clear();
    return false;
  }
  return true;
}

// ui::SimpleMenuModel::Delegate:
bool BrowserSidebarHostView::IsCommandIdChecked(int command_id) const {
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
  if (command_id == kToggleFloatingSidebar) {
    return browser_->GetBrowserView().GetAhoiSidebarPresentationMode() ==
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
      case kDeleteWorkspace:
        return controller_->view_model().workspace_id().has_value() &&
               workspace_service_->ordered_workspaces().size() > 1;
      case kToggleFloatingSidebar:
      case kToggleSidebarVisibility:
      case kToggleWorkspaceSwipe:
      case kToggleCmdScrollTabSwitching:
      case kToggleMiddleClickAutoscroll:
        return true;
      default:
        break;
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
