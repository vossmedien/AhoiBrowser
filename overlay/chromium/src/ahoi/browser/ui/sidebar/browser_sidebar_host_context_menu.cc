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
  if (!tab || !GetWidget() || context_.scope != ContextMenuScope::kNone) {
    return;
  }
  context_.runtime_tab = tab;
  context_.scope = ContextMenuScope::kOpenTab;
  context_.node_id.reset();
  const bool has_page_action_target = CaptureContextPageActionTarget(tab.get());
  context_.model = std::make_unique<ui::SimpleMenuModel>(this);
  context_.model->AddItem(
      kActivateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_OPEN));
  context_.model->AddItem(
      kSaveTemporaryTab,
      l10n_util::GetStringUTF16(IDS_STAR_VIEW_MENU_ADD_BOOKMARK));
  if (has_page_action_target) {
    context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
    context_.model->AddItem(
        kCopyActivePageLink,
        l10n_util::GetStringUTF16(IDS_AHOI_COPY_ACTIVE_PAGE_LINK));
    context_.model->AddItem(
        kCopyActivePageMarkdownLink,
        l10n_util::GetStringUTF16(IDS_AHOI_COPY_ACTIVE_PAGE_LINK_AS_MARKDOWN));
    context_.model->AddItem(
        kOpenActivePageInReadingMode,
        l10n_util::GetStringUTF16(
            CanOpenActivePageInReadingMode(browser_)
                ? IDS_AHOI_OPEN_ACTIVE_PAGE_IN_READING_MODE
                : IDS_AHOI_READING_MODE_UNAVAILABLE));
  }
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  const bool sleeping = ahoi::memory::IsTabSleeping(tab.get());
  context_.model->AddItem(
      sleeping ? kWakeTab : kSleepTab,
      l10n_util::GetStringUTF16(sleeping ? IDS_AHOI_CONTEXT_WAKE_TAB
                                         : IDS_AHOI_CONTEXT_SLEEP_TAB));
  context_.model->AddCheckItem(
      kToggleNeverSleep,
      l10n_util::GetStringUTF16(ahoi::memory::IsNeverSleep(tab.get())
                                    ? IDS_AHOI_CONTEXT_ALLOW_SLEEP_SITE
                                    : IDS_AHOI_CONTEXT_NEVER_SLEEP_SITE));
  if (tab->GetSplit().has_value()) {
    context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
    context_.model->AddCheckItem(
        kSplitSideBySide,
        l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_SIDE_BY_SIDE));
    context_.model->AddCheckItem(
        kSplitStacked, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_STACKED));
    context_.model->AddItem(
        kReverseSplit, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_REVERSE_VIEWS));
    context_.model->AddItem(
        kSeparateSplit,
        l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SEPARATE_VIEWS));
  }
  context_.model->AddItem(
      kCreateGroupAroundNode,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_GROUP_WITH_TAB));
  if (BuildMoveToMenu(nullptr)) {
    context_.model->AddSubMenu(
        kMoveTo, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO),
        context_move_menu_model_.get());
  }
  context_.model->AddItem(
      kDuplicateNode, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_DUPLICATE));
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  context_.model->AddItem(
      kCloseRuntimeTab, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_CLOSETAB));
  context_.model->AddItem(
      kArchiveTemporaryTab,
      tab->GetSplit() ? StructureText(u"Split archivieren", u"Archive split")
                      : StructureText(u"Tab archivieren", u"Archive tab"));

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_.model.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_.model.reset();
  context_move_menu_model_.reset();
  context_move_submenu_models_.clear();
  context_.move_destinations.clear();
  context_.runtime_tab.reset();
  ClearContextPageActionTarget();
  context_.scope = ContextMenuScope::kNone;
}

void BrowserSidebarHostView::ShowWorkspaceMenu(
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!GetWidget() || context_.scope != ContextMenuScope::kNone) {
    return;
  }
  if (!window_id_.has_value()) {
    window_id_ = session_bridge_->GetWindowId(browser_);
  }
  context_.scope = ContextMenuScope::kWorkspace;
  context_.node_id.reset();
  ClearContextPageActionTarget();
  context_.workspace_ids.clear();
  context_.model = std::make_unique<ui::SimpleMenuModel>(this);
  // ADR 0011: every non-shared Workspace states its level. The level is part
  // of the title because native macOS menus do not show minor text.
  const std::u16string own_sessions_level =
      StructureText(u"Eigene Website-Sitzungen", u"Own website sessions");
  const std::u16string isolated_level =
      StructureText(u"Vollständig getrennt", u"Fully separated");
  const auto with_level = [](const std::u16string& name,
                             const std::u16string& level) {
    return base::StrCat({name, u" – ", level});
  };
  const bool isolated_profile =
      session::IsIsolatedWorkspaceProfile(browser_->GetProfile());
  context_.isolated_workspace_dirs.clear();
  context_.main_workspace_ids.clear();
  context_.workspace_positions.clear();
  context_.offers_main_workspaces = false;
  // ADR 0011 step 2 (handoff 048): one list in the process-wide order. The
  // check mark shows this window's Workspace; items of another Profile hand
  // this window's frame over to that Profile's window.
  const std::vector<SwitcherWorkspace> switcher = SwitcherWorkspaces();
  for (size_t position = 0; position < switcher.size(); ++position) {
    const SwitcherWorkspace& workspace = switcher[position];
    const std::u16string title =
        !workspace.key.profile_dir.empty()
            ? with_level(workspace.name, isolated_level)
        : workspace.own_website_sessions
            ? with_level(workspace.name, own_sessions_level)
            : workspace.name;
    int command_id = 0;
    if (workspace.own) {
      command_id = kActivateWorkspaceCommandBase +
                   static_cast<int>(context_.workspace_ids.size());
      context_.workspace_ids.push_back(workspace.key.workspace_id);
      context_.model->AddCheckItem(command_id, title);
    } else if (workspace.key.profile_dir.empty()) {
      if (context_.main_workspace_ids.size() >= 99) {
        continue;
      }
      command_id = kOpenMainWorkspaceCommandBase +
                   static_cast<int>(context_.main_workspace_ids.size());
      context_.main_workspace_ids.push_back(workspace.key.workspace_id);
      context_.model->AddItem(command_id, title);
    } else {
      if (context_.isolated_workspace_dirs.size() >= 99) {
        continue;
      }
      command_id = kOpenIsolatedWorkspaceCommandBase +
                   static_cast<int>(context_.isolated_workspace_dirs.size());
      context_.isolated_workspace_dirs.push_back(workspace.key.profile_dir);
      context_.model->AddItem(command_id, title);
    }
    context_.workspace_positions.emplace(command_id, position);
  }
  // In a separated window whose main Profile is not loaded, its Workspaces
  // are not known yet; this entry loads it.
  if (isolated_profile && !session::GetLoadedMainProfile()) {
    context_.offers_main_workspaces = true;
    context_.model->AddItem(
        kOpenMainWorkspacesCommand,
        StructureText(u"Haupt-Workspaces öffnen", u"Open main Workspaces"));
  }
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  if (!isolated_profile) {
    context_.model->AddItem(
        kCreateWorkspace,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_WORKSPACE));
    context_.model->AddItem(
        kDuplicateWorkspace,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DUPLICATE));
  }
  context_.model->AddItem(
      kEditWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_EDIT_WORKSPACE));
  context_.model->AddItem(
      kCopyAllLinks,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_COPY_ALL_LINKS));
  if (!isolated_profile) {
    context_.model->AddItem(
        kConvertWorkspaceToIsolated,
        StructureText(u"In vollständig getrennten Workspace umwandeln …",
                      u"Convert to fully separated Workspace …"));
  }
  // ADR 0012 (handoff 080): merge into another Workspace of this Profile.
  const std::optional<base::Uuid> shown_workspace =
      controller_->view_model().workspace_id();
  context_move_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t index = 0; index < context_.workspace_ids.size() && index < 99;
       ++index) {
    const tab_tree::Workspace* target =
        FindWorkspace(context_.workspace_ids[index]);
    if (target && context_.workspace_ids[index] != shown_workspace) {
      context_move_menu_model_->AddItem(
          kMergeWorkspaceCommandBase + static_cast<int>(index), target->name);
    }
  }
  if (shown_workspace && context_move_menu_model_->GetItemCount() > 0) {
    context_.model->AddSubMenu(
        kMergeWorkspace, StructureText(u"Zusammenführen mit", u"Merge into"),
        context_move_menu_model_.get());
  }
  context_.model->AddItem(
      kDeleteWorkspace,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DELETE_WORKSPACE));
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  BuildArchiveMenus();
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  context_.model->AddCheckItem(
      kToggleFloatingSidebar,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_FLOATING_SIDEBAR));
  context_.model->AddItem(
      kToggleSidebarVisibility,
      l10n_util::GetStringUTF16(
          BrowserView::GetBrowserViewForBrowser(browser_.get())
                      ->GetAhoiSidebarPresentationMode() ==
                  SidebarPresentationMode::kHidden
              ? IDS_AHOI_CONTEXT_SHOW_SIDEBAR
              : IDS_AHOI_CONTEXT_HIDE_SIDEBAR));
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  context_.model->AddCheckItem(
      kToggleWorkspaceSwipe,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_WORKSPACE_SWIPE));
  context_.model->AddCheckItem(
      kToggleCmdScrollTabSwitching,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_CMD_SCROLL_TAB_SWITCHING));
  context_.model->AddCheckItem(
      kToggleMiddleClickAutoscroll,
      l10n_util::GetStringUTF16(IDS_AHOI_NAVIGATION_MIDDLE_CLICK_AUTOSCROLL));
  context_.model->AddCheckItem(
      kToggleAutoPeek,
      StructureText(u"Links gespeicherter Seiten zu anderen Websites als "
                    u"Vorschau öffnen",
                    u"Preview links from saved pages to other sites"));
  context_.model->AddCheckItem(
      kTogglePeekOnShiftClick,
      StructureText(u"⇧-Klick auf Links öffnet eine Vorschau",
                    u"Shift-click on links opens a preview"));
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  context_.model->AddItem(
      kCreateRootGroup,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_ROOT_GROUP));
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_.model.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_.model.reset();
  context_move_menu_model_.reset();
  context_.workspace_ids.clear();
  context_.isolated_workspace_dirs.clear();
  context_.main_workspace_ids.clear();
  context_.offers_main_workspaces = false;
  context_.archive_policy_model.reset();
  context_.archive_workspace_id.reset();
  context_.scope = ContextMenuScope::kNone;
}

void BrowserSidebarHostView::ShowNodeContextMenu(
    std::optional<base::Uuid> node_id,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  // Search rows are a transient projection. Mutating their normal-tree
  // context menu would either fail in the controller or act on hidden
  // siblings, so keep this state navigation-only.
  if (!discovery_state_.query.empty()) {
    return;
  }
  const tab_tree::TreeNode* node =
      node_id.has_value() ? controller_->view_model().GetNode(*node_id)
                          : nullptr;
  if ((node_id.has_value() && !node) || !GetWidget() ||
      context_.scope != ContextMenuScope::kNone) {
    return;
  }
  context_.node_id = node_id;
  context_.scope = ContextMenuScope::kTree;
  ClearContextPageActionTarget();
  context_.model = std::make_unique<ui::SimpleMenuModel>(this);
  if (!node) {
    context_.model->AddItem(
        kCreateRootGroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_ROOT_GROUP));
  } else if (node->type == tab_tree::TreeNodeType::kSavedPage) {
    context_.model->AddItem(
        kActivateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_OPEN));
    tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node->id);
    if (!node->is_temporary) {
      context_.model->AddItem(
          kGoToSavedHome,
          StructureText(u"Zur Ausgangsadresse", u"Go to Home address"));
      context_.model->AddItem(
          kSetSavedHome,
          StructureText(u"Aktuelle Seite als Ausgangsadresse setzen",
                        u"Set current page as Home address"));
    } else {
      context_.model->AddItem(
          kArchiveTemporaryTab, StructureText(u"Archivieren (inklusive Split)",
                                              u"Archive (including split)"));
    }
    if (CaptureContextPageActionTarget(tab)) {
      context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
      context_.model->AddItem(
          kCopyActivePageLink,
          l10n_util::GetStringUTF16(IDS_AHOI_COPY_ACTIVE_PAGE_LINK));
      context_.model->AddItem(
          kCopyActivePageMarkdownLink,
          l10n_util::GetStringUTF16(
              IDS_AHOI_COPY_ACTIVE_PAGE_LINK_AS_MARKDOWN));
      context_.model->AddItem(
          kOpenActivePageInReadingMode,
          l10n_util::GetStringUTF16(
              CanOpenActivePageInReadingMode(browser_)
                  ? IDS_AHOI_OPEN_ACTIVE_PAGE_IN_READING_MODE
                  : IDS_AHOI_READING_MODE_UNAVAILABLE));
    }
    if (tab && tab->GetSplit().has_value()) {
      context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
      context_.model->AddCheckItem(
          kSplitSideBySide,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_SIDE_BY_SIDE));
      context_.model->AddCheckItem(
          kSplitStacked, l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SHOW_STACKED));
      context_.model->AddItem(
          kReverseSplit,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_REVERSE_VIEWS));
      context_.model->AddItem(
          kSeparateSplit,
          l10n_util::GetStringUTF16(IDS_SPLIT_TAB_SEPARATE_VIEWS));
    }
    if (tab) {
      context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
      const bool sleeping = ahoi::memory::IsTabSleeping(tab);
      context_.model->AddItem(
          sleeping ? kWakeTab : kSleepTab,
          l10n_util::GetStringUTF16(sleeping ? IDS_AHOI_CONTEXT_WAKE_TAB
                                             : IDS_AHOI_CONTEXT_SLEEP_TAB));
      context_.model->AddCheckItem(
          kToggleNeverSleep,
          l10n_util::GetStringUTF16(ahoi::memory::IsNeverSleep(tab)
                                        ? IDS_AHOI_CONTEXT_ALLOW_SLEEP_SITE
                                        : IDS_AHOI_CONTEXT_NEVER_SLEEP_SITE));
    }
    context_.model->AddItem(
        kKeepOpenOnly, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_UNPIN_TAB));
    if (tab) {
      context_.model->AddItem(
          kCloseRuntimeTab, l10n_util::GetStringUTF16(IDS_TAB_CXMENU_CLOSETAB));
    }
    context_.model->AddItem(
        kCreateGroupAroundNode,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_GROUP_WITH_TAB));
  } else {
    context_.model->AddItem(
        kToggleGroupExpanded,
        l10n_util::GetStringUTF16(controller_->view_model().IsExpanded(node->id)
                                      ? IDS_AHOI_CONTEXT_COLLAPSE_GROUP
                                      : IDS_AHOI_CONTEXT_EXPAND_GROUP));
    context_.model->AddItem(
        kCreateSubgroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_NEW_SUBGROUP));
    context_.model->AddItem(
        kCopyAllLinks,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_COPY_ALL_LINKS));
    context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
    context_.model->AddItem(
        kCustomizeGroup,
        l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_CUSTOMIZE_GROUP));
  }
  if (node) {
    context_.model->AddItem(
        kDuplicateNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_DUPLICATE));
    if (BuildMoveToMenu(node)) {
      context_.model->AddSubMenu(
          kMoveTo, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO),
          context_move_menu_model_.get());
    }
    context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
    context_.model->AddItem(
        kRenameNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_RENAME));
    context_.model->AddItem(
        kDeleteNode, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_MOVE_TO_TRASH));
  }

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_.model.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  context_menu_runner_.reset();
  context_.model.reset();
  context_move_menu_model_.reset();
  context_move_submenu_models_.clear();
  context_.move_destinations.clear();
  context_.node_id.reset();
  ClearContextPageActionTarget();
  context_.scope = ContextMenuScope::kNone;
}

std::optional<int> BrowserSidebarHostView::AddMoveDestinationCommand(
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> folder_id) {
  // Destination IDs and submenu IDs share the same native menu tree. Keep
  // the two ranges provably disjoint even for unusually large profiles.
  constexpr size_t kMaximumDestinationCount =
      kMoveToWorkspaceSubmenuCommandBase - kMoveToDestinationCommandBase;
  if (context_.move_destinations.size() >= kMaximumDestinationCount) {
    return std::nullopt;
  }
  const int command_id = kMoveToDestinationCommandBase +
                         static_cast<int>(context_.move_destinations.size());
  context_.move_destinations.push_back(
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
  context_.move_destinations.clear();
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
  if (context_.move_destinations.empty()) {
    context_move_menu_model_.reset();
    context_move_submenu_models_.clear();
    return false;
  }
  return true;
}

// ui::SimpleMenuModel::Delegate:
}  // namespace ahoi::sidebar
