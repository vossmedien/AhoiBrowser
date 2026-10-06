// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Context menu for a ⌘/⇧-click multi-selection of sidebar rows (user
// decision 6 October 2026, Crest community review): move, split, close and
// archive several pages and folders in one step.

#include <algorithm>
#include <memory>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_types.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/toast/ahoi_toast.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "ui/events/event.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

bool German() {
  return base::i18n::GetConfiguredLocale().starts_with("de");
}

std::u16string Text(const char16_t* de, const char16_t* en) {
  return German() ? de : en;
}

}  // namespace

std::vector<base::Uuid> BrowserSidebarHostView::GetMultiSelectionRowOrder() const {
  std::vector<base::Uuid> order =
      tree_view_ ? tree_view_->visible_node_order() : std::vector<base::Uuid>();
  const auto collect = [this, &order](auto&& self, views::View* root) -> void {
    if (!root || !session_bridge_) {
      return;
    }
    if (auto tab = GetOpenTabForView(root)) {
      if (auto id = session_bridge_->FindSharedTreeNodeIdForTab(tab.get());
          id && !std::ranges::contains(order, *id)) {
        order.push_back(*id);
      }
      return;
    }
    for (auto* child : root->children()) {
      self(self, child);
    }
  };
  collect(collect, open_tabs_container_);
  return order;
}

std::optional<base::Uuid>
BrowserSidebarHostView::GetMultiSelectionActiveNode() const {
  return session_bridge_ && tab_strip_model_
             ? session_bridge_->FindSharedTreeNodeIdForTab(
                   tab_strip_model_->GetActiveTab())
             : std::nullopt;
}

void BrowserSidebarHostView::OnMultiSelectionChanged() {
  const auto update = [this](auto&& self, views::View* root) -> void {
    if (!root || !session_bridge_ || !tree_view_) {
      return;
    }
    if (auto tab = GetOpenTabForView(root)) {
      const auto id = session_bridge_->FindSharedTreeNodeIdForTab(tab.get());
      SetOpenTabMultiSelected(root, tree_view_->has_multi_selection(),
                             id && tree_view_->IsMultiSelected(*id));
      return;
    }
    for (auto* child : root->children()) {
      self(self, child);
    }
  };
  update(update, open_tabs_container_);
}

bool BrowserSidebarHostView::OnRuntimeMultiSelect(
    base::WeakPtr<tabs::TabInterface> tab,
    const ui::MouseEvent& event) {
  if (!tree_view_ || !session_bridge_ || !tab) {
    return false;
  }
  const auto id = session_bridge_->FindSharedTreeNodeIdForTab(tab.get());
  if (!id) {
    tree_view_->ClearMultiSelection();
    return false;
  }
  const bool handled = tree_view_->HandleMultiSelectClick(*id, event);
  if (handled) {
    // Escape goes to the existing tree handler, without activating a page.
    tree_view_->RequestFocus();
  }
  return handled;
}

bool BrowserSidebarHostView::ShowMultiSelectionMenu(
    const base::Uuid& node_id,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!tree_view_ || !controller_ || !session_bridge_ || !workspace_service_ ||
      !GetWidget() || context_.scope != ContextMenuScope::kNone) {
    return false;
  }
  const std::vector<base::Uuid> ids = tree_view_->multi_selection();
  if (ids.size() < 2 || !std::ranges::contains(ids, node_id)) {
    return false;
  }
  size_t pages = 0;
  size_t open_tabs = 0;
  std::vector<base::Uuid> temporary;
  for (const base::Uuid& id : ids) {
    const tab_tree::TreeNode* node = controller_->view_model().GetNode(id);
    if (!node || node->type != tab_tree::TreeNodeType::kSavedPage) {
      continue;
    }
    ++pages;
    if (session_bridge_->FindTabByTreeNodeId(id)) {
      ++open_tabs;
    }
    if (node->is_temporary) {
      temporary.push_back(id);
    }
  }

  context_.scope = ContextMenuScope::kMultiSelection;
  context_.node_id = node_id;
  ClearContextPageActionTarget();
  context_.model = std::make_unique<ui::SimpleMenuModel>(this);
  context_.model->AddTitle(base::NumberToString16(ids.size()) +
                           Text(u" Einträge ausgewählt", u" items selected"));
  // Ahoi splits hold two to four panes.
  if (pages == ids.size() && pages <= 4) {
    context_.model->AddItem(kMultiOpenInSplitCommand,
                            Text(u"In Split öffnen", u"Open in split"));
  }
  context_move_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const auto& workspaces = workspace_service_->ordered_workspaces();
  for (size_t index = 0;
       index < workspaces.size() &&
       kMultiMoveToWorkspaceCommandBase + static_cast<int>(index) <
           kCrossLevelMoveCommandBase;
       ++index) {
    if (workspaces[index].id == controller_->view_model().workspace_id()) {
      continue;
    }
    context_move_menu_model_->AddItem(
        kMultiMoveToWorkspaceCommandBase + static_cast<int>(index),
        workspaces[index].name);
  }
  if (context_move_menu_model_->GetItemCount() > 0) {
    context_.model->AddSubMenu(
        kMultiMoveSubmenuCommand,
        Text(u"In Workspace verschieben", u"Move to Workspace"),
        context_move_menu_model_.get());
  }
  if (open_tabs > 0) {
    context_.model->AddItem(
        kMultiCloseTabsCommand,
        Text(u"Offene Tabs schließen", u"Close open tabs"));
  }
  if (!temporary.empty() &&
      session_bridge_->CanArchiveTemporaryPages(temporary)) {
    context_.model->AddItem(kMultiArchiveCommand,
                            Text(u"Archivieren", u"Archive"));
  }
  context_.model->AddSeparator(ui::NORMAL_SEPARATOR);
  context_.model->AddItem(kMultiClearSelectionCommand,
                          Text(u"Auswahl aufheben", u"Clear selection"));

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_.model.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  base::WeakPtr<BrowserSidebarHostView> alive = weak_ptr_factory_.GetWeakPtr();
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
  if (!alive) {
    return true;
  }
  context_menu_runner_.reset();
  context_.model.reset();
  context_move_menu_model_.reset();
  context_.node_id.reset();
  context_.scope = ContextMenuScope::kNone;
  return true;
}

bool BrowserSidebarHostView::RunMultiSelectionCommand(int command_id) {
  if (context_.scope != ContextMenuScope::kMultiSelection) {
    return false;
  }
  if (!tree_view_ || !controller_ || !session_bridge_) {
    return true;
  }
  const std::vector<base::Uuid> ids = tree_view_->multi_selection();
  if (command_id == kMultiClearSelectionCommand) {
    tree_view_->ClearMultiSelection();
    return true;
  }
  if (command_id == kMultiOpenInSplitCommand) {
    // Each further page joins the split around the first one.
    for (size_t index = 1; index < ids.size(); ++index) {
      if (!CanSplitSavedPages(ids[index], ids.front()) ||
          !SplitSavedPages(ids[index], ids.front())) {
        break;
      }
    }
    tree_view_->ClearMultiSelection();
    return true;
  }
  if (command_id == kMultiCloseTabsCommand) {
    std::vector<base::Uuid> requested;
    for (const base::Uuid& id : ids) {
      if (tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(id)) {
        requested.push_back(id);
        tab->Close();
      }
    }
    // A page with a beforeunload handler keeps its tab until the user
    // answers, and cancelling keeps it open. Count only tabs that are gone and
    // keep the selection while any is still open, so the user can retry.
    size_t closed = 0;
    for (const base::Uuid& id : requested) {
      if (!session_bridge_->FindTabByTreeNodeId(id)) {
        ++closed;
      }
    }
    if (closed == requested.size()) {
      tree_view_->ClearMultiSelection();
    }
    if (closed > 0) {
      toast::Show(browser_, toast::Event::kClosed, std::u16string(), closed);
    }
    return true;
  }
  if (command_id == kMultiArchiveCommand) {
    std::vector<base::Uuid> temporary;
    for (const base::Uuid& id : ids) {
      const tab_tree::TreeNode* node = controller_->view_model().GetNode(id);
      if (node && node->type == tab_tree::TreeNodeType::kSavedPage &&
          node->is_temporary) {
        temporary.push_back(id);
      }
    }
    const size_t count = temporary.size();
    // The selection stays until the archive succeeded, so a failure leaves
    // it intact for another attempt.
    session_bridge_->ArchiveTemporaryPages(
        std::move(temporary),
        base::BindOnce(
            [](base::WeakPtr<BrowserSidebarHostView> view, size_t count,
               bool success) {
              if (!view) {
                return;
              }
              if (success && view->tree_view_) {
                view->tree_view_->ClearMultiSelection();
              }
              view->CompleteArchiveContextTabs(count, success);
            },
            weak_ptr_factory_.GetWeakPtr(), count));
    return true;
  }
  const auto& workspaces = workspace_service_->ordered_workspaces();
  const int index = command_id - kMultiMoveToWorkspaceCommandBase;
  if (index < 0 || static_cast<size_t>(index) >= workspaces.size()) {
    return false;
  }
  const SidebarTreeController::DropTarget target = {
      .workspace_id = workspaces[index].id,
      .target_node_id = std::nullopt,
      .position = SidebarTreeController::DropPosition::kInside};
  const SidebarTreeController::DropExecutionResult result =
      controller_->PerformGroupedDrop(
          ids, target, SidebarTreeController::DropOperation::kMove,
          base::Time::Now());
  tree_view_->ClearMultiSelection();
  if (!result.ok()) {
    OnMutationFailed(result.store_result);
    return true;
  }
  toast::Show(browser_, toast::Event::kMovedToWorkspace,
              workspaces[index].name, ids.size());
  return true;
}

}  // namespace ahoi::sidebar
