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
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
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
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_collection.h"
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

void BrowserSidebarHostView::OnWorkspacePressed(const ui::Event&) {
  ShowWorkspaceMenu(workspace_button_->GetBoundsInScreen().bottom_left(),
                    ui::mojom::MenuSourceType::kMouse);
}

void BrowserSidebarHostView::RunBrowserCommand(int command_id,
                                               const ui::Event&) {
  // Browser commands may synchronously activate a new tab, rebuild sidebar
  // presentation, or close the window. Let Button finish dispatching first and
  // guard the eventual command against host destruction.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarHostView::ExecuteBrowserCommand,
                                weak_ptr_factory_.GetWeakPtr(), command_id));
}

void BrowserSidebarHostView::ExecuteBrowserCommand(int command_id) {
  // Chromium disables some BrowserCommandController commands while the tab
  // strip has no active tab. Ahoi deliberately supports a zero-tab window,
  // so the footer's primary action must use the tab creation primitive
  // directly instead of depending on command-controller enablement.
  if (command_id == IDC_NEW_TAB) {
    chrome::NewTab(browser_, NewTabTypes::kNewTabCommand);
    return;
  }
  chrome::ExecuteCommand(browser_, command_id);
}

BrowserSidebarSplitDropSource BrowserSidebarHostView::ResolveSplitDropSource(
    const drag::SidebarTabDragPayload& payload,
    bool activate_saved_page) {
  if (!payload.is_valid() || !session_bridge_ || !tab_strip_model_) {
    return {};
  }
  if (payload.runtime_tab_handle.has_value()) {
    tabs::TabInterface* tab = FindRuntimeTab(*payload.runtime_tab_handle);
    const bool local = tab && session_bridge_->FindTabStripModelForTab(tab) ==
                                  tab_strip_model_;
    return {
        .valid = local,
        .tab = local ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()};
  }

  tab_tree::TreeNode node;
  if (!payload.saved_node_id.has_value() ||
      session_bridge_->tab_tree_store()->GetNode(*payload.saved_node_id,
                                                 &node) !=
          tab_tree::TabTreeStore::Result::kOk ||
      node.tombstone || node.type != tab_tree::TreeNodeType::kSavedPage) {
    return {};
  }

  tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node.id);
  if (tab &&
      session_bridge_->FindTabStripModelForTab(tab) != tab_strip_model_) {
    // Content drops are window-local. Activating a saved tab owned by another
    // window would steal focus before the local split API rejects it.
    return {};
  }
  if (!tab && activate_saved_page) {
    BrowserSidebarSplitDropSource materialized =
        MaterializeSavedPage(node, /*require_local_model=*/true);
    if (!materialized.valid || !materialized.tab ||
        session_bridge_->FindTabStripModelForTab(materialized.tab.get()) !=
            tab_strip_model_) {
      if (materialized.rollback) {
        std::move(materialized.rollback).Run();
      }
      return {};
    }
    return materialized;
  }
  return {.valid = true,
          .tab = tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()};
}

// SidebarTreeViewDelegate:
void BrowserSidebarHostView::ActivateSavedPage(const tab_tree::TreeNode& node) {
  // A direct user activation commits the materialization. Transactional drop
  // callers retain and run the returned rollback closure only on failure.
  discovery_activation_committed_ = true;
  if (MaterializeSavedPage(node, /*require_local_model=*/false).valid) {
    ScheduleCloseSidebarDiscoveryAfterActivation();
  } else if (discovery_view_ && discovery_view_->is_open()) {
    discovery_activation_committed_ = false;
  }
}

BrowserSidebarSplitDropSource BrowserSidebarHostView::MaterializeSavedPage(
    const tab_tree::TreeNode& node,
    bool require_local_model) {
  const base::WeakPtr<BrowserSidebarHostView> weak_host =
      weak_ptr_factory_.GetWeakPtr();
  const base::WeakPtr<tabs::TabInterface> active_before =
      tab_strip_model_ && tab_strip_model_->GetActiveTab()
          ? tab_strip_model_->GetActiveTab()->GetWeakPtr()
          : base::WeakPtr<tabs::TabInterface>();
  const auto make_rollback = [weak_host, active_before](
                                 base::WeakPtr<tabs::TabInterface> opened_tab) {
    return base::BindOnce(
        [](base::WeakPtr<BrowserSidebarHostView> host,
           base::WeakPtr<tabs::TabInterface> prior_active,
           base::WeakPtr<tabs::TabInterface> owned_tab) {
          // Restore focus before closing the transaction-owned foreground
          // tab. Close is deliberately the last operation: its synchronous
          // callbacks may tear down the host or window.
          if (host && host->tab_strip_model_ && prior_active &&
              host->tab_strip_model_->GetIndexOfTab(prior_active.get()) >= 0 &&
              host->tab_strip_model_->GetActiveTab() != prior_active.get()) {
            host->tab_strip_model_->ActivateTab(prior_active.get());
          }
          if (owned_tab) {
            owned_tab->Close();
          }
        },
        weak_host, active_before, std::move(opened_tab));
  };

  if (tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node.id)) {
    const base::WeakPtr<tabs::TabInterface> weak_tab = tab->GetWeakPtr();
    TabStripModel* model = session_bridge_->FindTabStripModelForTab(tab);
    const int index = model ? model->GetIndexOfTab(tab) : -1;
    if (require_local_model && model != tab_strip_model_) {
      return {};
    }
    if (model && index >= 0) {
      model->ActivateTabAt(
          index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kMouse));
      if (!weak_host || !weak_tab) {
        return {};
      }
      BrowserWindowInterface* const window =
          weak_tab->GetBrowserWindowInterface();
      if (window && window->GetWindow()) {
        window->GetWindow()->Activate();
      }
      if (!weak_host || !weak_tab) {
        return {};
      }
      return {.valid = true,
              .tab = weak_tab,
              .rollback = make_rollback(base::WeakPtr<tabs::TabInterface>())};
    }
  }

  Browser* const navigation_browser = browser_;
  NavigateParams params(navigation_browser, node.url,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  ::Navigate(&params);
  tabs::TabInterface* const opened_tab =
      tabs::TabInterface::MaybeGetFromContents(
          params.navigated_or_inserted_contents);
  const base::WeakPtr<tabs::TabInterface> weak_opened_tab =
      opened_tab ? opened_tab->GetWeakPtr()
                 : base::WeakPtr<tabs::TabInterface>();
  if (!weak_host) {
    if (weak_opened_tab) {
      weak_opened_tab->Close();
    }
    return {};
  }
  if (weak_opened_tab && weak_host->session_bridge_->BindTreeNodeToTab(
                             node, weak_opened_tab.get())) {
    if (!weak_host || !weak_opened_tab) {
      if (weak_opened_tab) {
        weak_opened_tab->Close();
      }
      return {};
    }
    return {.valid = true,
            .tab = weak_opened_tab,
            .rollback = make_rollback(weak_opened_tab)};
  }

  if (!weak_host) {
    if (weak_opened_tab) {
      weak_opened_tab->Close();
    }
    return {};
  }

  // URL matching deliberately leaves chrome://newtab temporary, so the exact
  // durable UUID must be bound synchronously here. If another activation won
  // the race, retain that authoritative tab and retire only the tab created by
  // this call; repeated clicks must never multiply one saved "New Tab" row.
  if (tabs::TabInterface* const existing =
          weak_host->session_bridge_->FindTabByTreeNodeId(node.id)) {
    const base::WeakPtr<tabs::TabInterface> weak_existing =
        existing->GetWeakPtr();
    TabStripModel* const model =
        weak_host->session_bridge_->FindTabStripModelForTab(existing);
    const int index = model ? model->GetIndexOfTab(existing) : -1;
    if (require_local_model && model != weak_host->tab_strip_model_) {
      if (weak_opened_tab && weak_opened_tab.get() != weak_existing.get()) {
        weak_opened_tab->Close();
      }
      return {};
    }
    if (model && index >= 0) {
      model->ActivateTabAt(
          index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kMouse));
    }
    if (!weak_host || !weak_existing) {
      if (weak_opened_tab && weak_opened_tab.get() != weak_existing.get()) {
        weak_opened_tab->Close();
      }
      return {};
    }
    // Activate the authoritative winner before retiring only the duplicate
    // created by this call. A failed outer drop may restore `active_before`,
    // but must never close this independently bound winner.
    BrowserSidebarSplitDropSource result{
        .valid = true,
        .tab = weak_existing,
        .rollback = make_rollback(base::WeakPtr<tabs::TabInterface>())};
    if (weak_opened_tab && weak_opened_tab.get() != weak_existing.get()) {
      weak_opened_tab->Close();
    }
    if (!weak_host || !weak_existing) {
      return {};
    }
    return result;
  }

  // Binding failure without an authoritative winner must fail closed. This is
  // the only tab created by this activation, so retiring it cannot disturb an
  // existing session and prevents every retry from adding another orphan.
  if (weak_opened_tab) {
    base::OnceClosure rollback = make_rollback(weak_opened_tab);
    std::move(rollback).Run();
  }
  return {};
}

bool BrowserSidebarHostView::CanSplitSavedPages(
    const base::Uuid& source_node_id,
    const base::Uuid& target_node_id) const {
  if (source_node_id == target_node_id || !controller_ || !session_bridge_ ||
      !tab_strip_model_) {
    return false;
  }
  const tab_tree::TreeNode* const source_node =
      controller_->view_model().GetNode(source_node_id);
  const tab_tree::TreeNode* const target_node =
      controller_->view_model().GetNode(target_node_id);
  if (!source_node || !target_node || source_node->tombstone ||
      target_node->tombstone ||
      source_node->type != tab_tree::TreeNodeType::kSavedPage ||
      target_node->type != tab_tree::TreeNodeType::kSavedPage) {
    return false;
  }

  tabs::TabInterface* source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  if (source == target && source) {
    return false;
  }

  // Hover validation must not open closed saved pages. A committed drop below
  // activates only the missing participants and rolls them back if Chromium
  // rejects the split transaction. Already-live participants must belong to
  // this exact window; silently moving a tab from another window is unsafe.
  if ((source &&
       session_bridge_->FindTabStripModelForTab(source) != tab_strip_model_) ||
      (target &&
       session_bridge_->FindTabStripModelForTab(target) != tab_strip_model_) ||
      (source && source->IsSplit())) {
    return false;
  }
  if (!target || !target->IsSplit()) {
    return true;
  }
  const split_tabs::SplitTabData* split_data =
      tab_strip_model_->GetSplitData(*target->GetSplit());
  // Respect Chromium's collection invariant without introducing an Ahoi-only
  // lower cap.
  return split_data &&
         split_data->ListTabs().size() < tabs::SplitTabCollection::kMaxTabs;
}

bool BrowserSidebarHostView::SplitSavedPages(const base::Uuid& source_node_id,
                                             const base::Uuid& target_node_id) {
  if (!CanSplitSavedPages(source_node_id, target_node_id)) {
    return false;
  }

  BrowserSidebarSplitDropSource target_resolution = ResolveSplitDropSource(
      drag::SidebarTabDragPayload{.saved_node_id = target_node_id},
      /*activate_saved_page=*/true);
  base::ScopedClosureRunner rollback_target(
      std::move(target_resolution.rollback));
  base::WeakPtr<tabs::TabInterface> target = target_resolution.tab;
  BrowserSidebarSplitDropSource source_resolution = ResolveSplitDropSource(
      drag::SidebarTabDragPayload{.saved_node_id = source_node_id},
      /*activate_saved_page=*/true);
  base::ScopedClosureRunner rollback_source(
      std::move(source_resolution.rollback));
  base::WeakPtr<tabs::TabInterface> source = source_resolution.tab;

  if (!source_resolution.valid || !target_resolution.valid || !source ||
      !target || !CanSplitSavedPages(source_node_id, target_node_id)) {
    return false;
  }
  const int source_index = tab_strip_model_->GetIndexOfTab(source.get());
  const int target_index = tab_strip_model_->GetIndexOfTab(target.get());
  if (source_index < 0 || target_index < 0) {
    return false;
  }
  const bool created =
      tab_strip_model_
          ->CreateOrAddToSplitFromDrop(source_index, target_index,
                                       split_tabs::SplitTabArrangement::kLinear)
          .has_value();
  if (!created) {
    return false;
  }
  rollback_source.ReplaceClosure(base::OnceClosure());
  rollback_target.ReplaceClosure(base::OnceClosure());
  if (created && tree_view_) {
    tree_view_->OnSplitGroupsChanged();
  }
  ScheduleRuntimePresentationRefresh();
  return true;
}

bool BrowserSidebarHostView::CanReorderSavedSplitPanes(
    const base::Uuid& source_node_id,
    const base::Uuid& target_node_id) const {
  tabs::TabInterface* source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  return source && target && source != target &&
         source->GetSplit().has_value() &&
         source->GetSplit() == target->GetSplit() &&
         session_bridge_->FindTabStripModelForTab(source) == tab_strip_model_ &&
         session_bridge_->FindTabStripModelForTab(target) == tab_strip_model_;
}

bool BrowserSidebarHostView::ReorderSavedSplitPanes(
    const base::Uuid& source_node_id,
    const base::Uuid& target_node_id) {
  if (!CanReorderSavedSplitPanes(source_node_id, target_node_id)) {
    return false;
  }
  tabs::TabInterface* source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  const bool reordered = tab_strip_model_->ReorderTabInSplit(source, target);
  if (reordered) {
    OnSidebarDragStateChanged(std::nullopt);
    ScheduleRuntimePresentationRefresh();
  }
  return reordered;
}

std::vector<std::vector<base::Uuid>>
BrowserSidebarHostView::GetSplitSavedPageGroups() const {
  std::vector<std::vector<base::Uuid>> groups;
  if (!tab_strip_model_ || !session_bridge_) {
    return groups;
  }
  for (const split_tabs::SplitTabId split_id : tab_strip_model_->ListSplits()) {
    const split_tabs::SplitTabData* split_data =
        tab_strip_model_->GetSplitData(split_id);
    if (!split_data) {
      continue;
    }
    std::vector<base::Uuid> node_ids;
    node_ids.reserve(split_data->ListTabs().size());
    for (tabs::TabInterface* tab : split_data->ListTabs()) {
      const std::optional<base::Uuid> node_id =
          session_bridge_->FindTreeNodeIdForTab(tab);
      if (node_id.has_value()) {
        node_ids.push_back(*node_id);
      }
    }
    if (node_ids.size() >= 2) {
      groups.push_back(std::move(node_ids));
    }
  }
  return groups;
}

std::optional<split_tabs::SplitTabVisualData>
BrowserSidebarHostView::GetSplitSavedPageVisualData(
    const std::vector<base::Uuid>& node_ids) const {
  if (node_ids.size() < 2 || !tab_strip_model_ || !session_bridge_) {
    return std::nullopt;
  }
  tabs::TabInterface* first =
      session_bridge_->FindTabByTreeNodeId(node_ids.front());
  if (!first || !first->GetSplit().has_value()) {
    return std::nullopt;
  }
  TabStripModel* model = session_bridge_->FindTabStripModelForTab(first);
  const split_tabs::SplitTabData* split_data =
      model ? model->GetSplitData(*first->GetSplit()) : nullptr;
  if (!split_data || split_data->ListTabs().size() != node_ids.size()) {
    return std::nullopt;
  }
  const std::vector<tabs::TabInterface*> split_tabs = split_data->ListTabs();
  for (size_t index = 0; index < split_tabs.size(); ++index) {
    if (session_bridge_->FindTreeNodeIdForTab(split_tabs[index]) !=
        std::optional<base::Uuid>(node_ids[index])) {
      return std::nullopt;
    }
  }
  return *split_data->visual_data();
}

std::vector<base::Uuid> BrowserSidebarHostView::GetMoveGroupNodeIds(
    const base::Uuid& source_node_id) const {
  tabs::TabInterface* source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  if (!source || !source->IsSplit()) {
    return {source_node_id};
  }
  TabStripModel* model = session_bridge_->FindTabStripModelForTab(source);
  const split_tabs::SplitTabData* split_data =
      model ? model->GetSplitData(*source->GetSplit()) : nullptr;
  if (!split_data) {
    return {source_node_id};
  }
  std::vector<base::Uuid> node_ids;
  node_ids.reserve(split_data->ListTabs().size());
  for (tabs::TabInterface* tab : split_data->ListTabs()) {
    const std::optional<base::Uuid> node_id =
        session_bridge_->FindTreeNodeIdForTab(tab);
    if (!node_id.has_value()) {
      return {source_node_id};
    }
    node_ids.push_back(*node_id);
  }
  return node_ids.size() > 1 ? node_ids
                             : std::vector<base::Uuid>{source_node_id};
}

bool BrowserSidebarHostView::CanExtractSavedSplitPaneForDrop(
    const base::Uuid& source_node_id,
    const std::optional<base::Uuid>& target_node_id) const {
  tabs::TabInterface* const source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  if (!source || !source->GetSplit().has_value() ||
      session_bridge_->FindTabStripModelForTab(source) != tab_strip_model_) {
    return false;
  }
  if (!target_node_id.has_value()) {
    return true;
  }
  tabs::TabInterface* const target =
      session_bridge_->FindTabByTreeNodeId(*target_node_id);
  return !target || target->GetSplit() != source->GetSplit();
}

bool BrowserSidebarHostView::ExtractSavedSplitPaneAfterDrop(
    const base::Uuid& source_node_id) {
  tabs::TabInterface* const source =
      session_bridge_->FindTabByTreeNodeId(source_node_id);
  if (!source || !source->GetSplit().has_value() ||
      session_bridge_->FindTabStripModelForTab(source) != tab_strip_model_ ||
      !ExtractTabFromSplitPreservingRemainder(tab_strip_model_, source)) {
    return false;
  }
  ScheduleRuntimePresentationRefresh();
  return true;
}

bool BrowserSidebarHostView::CanSaveTemporaryTab(
    int runtime_tab_handle,
    const SidebarTreeController::DropTarget& target) {
  return FindTemporaryTab(runtime_tab_handle) &&
         controller_->ValidateNewSavedPageDrop(target) ==
             SidebarTreeController::DropValidationResult::kAllowed;
}

bool BrowserSidebarHostView::SaveTemporaryTab(
    int runtime_tab_handle,
    const SidebarTreeController::DropTarget& target) {
  return SaveTemporaryTabAtDrop(runtime_tab_handle, target, nullptr);
}

bool BrowserSidebarHostView::CanSaveAndSplitTemporaryTab(
    int runtime_tab_handle,
    const base::Uuid& target_node_id) const {
  tabs::TabInterface* source = FindTemporaryTab(runtime_tab_handle);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  if (!source || !target || source == target || source->IsSplit()) {
    return false;
  }
  TabStripModel* source_model =
      session_bridge_->FindTabStripModelForTab(source);
  TabStripModel* target_model =
      session_bridge_->FindTabStripModelForTab(target);
  if (!source_model || source_model != target_model ||
      source_model != tab_strip_model_) {
    return false;
  }
  if (!target->IsSplit()) {
    return true;
  }
  const split_tabs::SplitTabData* split_data =
      target_model->GetSplitData(*target->GetSplit());
  return split_data &&
         split_data->ListTabs().size() < tabs::SplitTabCollection::kMaxTabs;
}

bool BrowserSidebarHostView::SaveAndSplitTemporaryTab(
    int runtime_tab_handle,
    const base::Uuid& target_node_id) {
  if (!CanSaveAndSplitTemporaryTab(runtime_tab_handle, target_node_id)) {
    return false;
  }
  const tab_tree::TreeNode* target_node =
      controller_->view_model().GetNode(target_node_id);
  if (!target_node) {
    return false;
  }
  base::Uuid created_node_id;
  if (!SaveTemporaryTabAtDrop(
          runtime_tab_handle,
          {.workspace_id = target_node->workspace_id,
           .target_node_id = target_node_id,
           .position = SidebarTreeController::DropPosition::kAfter},
          &created_node_id)) {
    return false;
  }
  if (SplitSavedPages(created_node_id, target_node_id)) {
    return true;
  }
  if (tabs::TabInterface* source =
          session_bridge_->FindTabByTreeNodeId(created_node_id)) {
    session_bridge_->MakeTabTemporary(source);
  }
  const tab_tree::TabTreeStore::Result rollback =
      controller_->DeleteNode(created_node_id, base::Time::Now());
  if (rollback != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(rollback);
  }
  return false;
}

bool BrowserSidebarHostView::CanReorderTemporarySplitPane(
    int runtime_tab_handle,
    const base::Uuid& target_node_id) const {
  tabs::TabInterface* source = FindTemporaryTab(runtime_tab_handle);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  return source && target && source != target &&
         source->GetSplit().has_value() &&
         source->GetSplit() == target->GetSplit() &&
         session_bridge_->FindTabStripModelForTab(source) == tab_strip_model_ &&
         session_bridge_->FindTabStripModelForTab(target) == tab_strip_model_;
}

bool BrowserSidebarHostView::ReorderTemporarySplitPane(
    int runtime_tab_handle,
    const base::Uuid& target_node_id) {
  if (!CanReorderTemporarySplitPane(runtime_tab_handle, target_node_id)) {
    return false;
  }
  tabs::TabInterface* source = FindTemporaryTab(runtime_tab_handle);
  tabs::TabInterface* target =
      session_bridge_->FindTabByTreeNodeId(target_node_id);
  const bool reordered = tab_strip_model_->ReorderTabInSplit(source, target);
  if (reordered) {
    OnTemporaryTabDragStateChanged(std::nullopt);
    ScheduleRuntimePresentationRefresh();
  }
  return reordered;
}

bool BrowserSidebarHostView::IsSavedPageRunning(
    const base::Uuid& node_id) const {
  tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node_id);
  return tab && session_bridge_->FindTabStripModelForTab(tab);
}

bool BrowserSidebarHostView::IsSavedPageSleeping(
    const base::Uuid& node_id) const {
  tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node_id);
  return tab && ahoi::memory::IsTabSleeping(tab);
}

std::vector<gfx::ImageSkia> BrowserSidebarHostView::GetSavedPageDragThumbnails(
    const base::Uuid& node_id) const {
  std::vector<gfx::ImageSkia> thumbnails;
  for (const base::Uuid& grouped_node_id : GetMoveGroupNodeIds(node_id)) {
    tabs::TabInterface* const tab =
        session_bridge_->FindTabByTreeNodeId(grouped_node_id);
    std::vector<gfx::ImageSkia> live = GetCachedDragThumbnails({tab});
    if (!live.empty() && !live.front().isNull() &&
        !live.front().size().IsEmpty()) {
      thumbnails.push_back(std::move(live.front()));
      continue;
    }
    const auto snapshot = saved_thumbnail_snapshots_.find(grouped_node_id);
    const tab_tree::TreeNode* const grouped_node =
        controller_->view_model().GetNode(grouped_node_id);
    thumbnails.push_back(snapshot != saved_thumbnail_snapshots_.end() &&
                                 grouped_node &&
                                 snapshot->second.url == grouped_node->url &&
                                 !snapshot->second.image.isNull() &&
                                 !snapshot->second.image.size().IsEmpty()
                             ? snapshot->second.image
                             : gfx::ImageSkia());
  }
  if (thumbnails.empty()) {
    thumbnails.emplace_back();
  }
  return thumbnails;
}

ui::ImageModel BrowserSidebarHostView::GetSavedPageIcon(
    const tab_tree::TreeNode& node) {
  if (tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node.id)) {
    ui::ImageModel live_icon = GetLiveTabFavicon(tab);
    if (!live_icon.IsEmpty()) {
      return live_icon;
    }
  }
  auto cached = favicon_cache_.find(node.url);
  if (cached != favicon_cache_.end()) {
    return cached->second;
  }
  if (favicon_service_ && node.url.is_valid() && !node.url.is_empty() &&
      requested_favicon_urls_.insert(node.url).second) {
    favicon_service_->GetFaviconImageForPageURL(
        node.url,
        base::BindOnce(&BrowserSidebarHostView::OnFaviconAvailable,
                       weak_ptr_factory_.GetWeakPtr(), node.url),
        &favicon_task_tracker_);
  }
  return ui::ImageModel();
}

ui::ImageModel BrowserSidebarHostView::GetSavedPageMediaIndicator(
    const tab_tree::TreeNode& node) const {
  return GetMediaIndicatorForTab(session_bridge_->FindTabByTreeNodeId(node.id));
}

void BrowserSidebarHostView::PerformSavedPageTrailingAction(
    const base::Uuid& node_id) {
  if (tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node_id)) {
    tab->Close();
    return;
  }
  const tab_tree::TabTreeStore::Result result =
      controller_->DeleteNode(node_id, base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
  }
}

}  // namespace ahoi::sidebar
