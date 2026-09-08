// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <utility>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi::sidebar {

bool BrowserSidebarHostView::SaveTemporaryTabAtDrop(
    int runtime_tab_handle,
    const SidebarTreeController::DropTarget& target,
    base::Uuid* created_node_id) {
  tabs::TabInterface* tab = FindTemporaryTab(runtime_tab_handle);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!tab || !contents) {
    return false;
  }
  const base::WeakPtr<tabs::TabInterface> weak_tab = tab->GetWeakPtr();
  GURL url = contents->GetVisibleURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetLastCommittedURL();
  }
  if (!url.is_valid() || url.is_empty()) {
    url = GURL("about:blank");
  }
  const std::u16string title = tab->GetTitle().empty()
                                   ? l10n_util::GetStringUTF16(IDS_NEW_TAB)
                                   : tab->GetTitle();

  // Capture everything needed from WebContents before changing split
  // membership. TabStripModel observers run synchronously and may destroy or
  // replace the source during extraction.
  const bool extract_split_pane = tab->IsSplit();
  std::optional<SplitTabExtractionSnapshot> extraction_snapshot;
  if (extract_split_pane) {
    extraction_snapshot =
        CaptureSplitTabExtractionSnapshot(tab_strip_model_, tab);
    if (!extraction_snapshot.has_value() ||
        !ExtractTabFromSplitPreservingRemainder(tab_strip_model_, tab)) {
      return false;
    }
  }
  base::ScopedClosureRunner rollback_extraction;
  if (extraction_snapshot.has_value()) {
    rollback_extraction.ReplaceClosure(base::BindOnce(
        [](base::WeakPtr<BrowserSidebarHostView> host,
           SplitTabExtractionSnapshot snapshot) {
          if (host && host->tab_strip_model_ &&
              !RestoreSplitTabExtraction(host->tab_strip_model_, snapshot)) {
            LOG(ERROR) << "Temporary-tab save failed and its split rollback "
                          "was incomplete";
          }
        },
        weak_ptr_factory_.GetWeakPtr(), std::move(*extraction_snapshot)));
  }
  if (!weak_tab || session_bridge_->FindTabStripModelForTab(weak_tab.get()) !=
                       tab_strip_model_) {
    return false;
  }
  tab_tree::TreeNode created;
  const auto existing_id =
      session_bridge_->FindSharedTreeNodeIdForTab(weak_tab.get());
  auto* const store = session_bridge_->tab_tree_store();
  tab_tree::TabTreeStore::Result result;
  if (existing_id) {
    tab_tree::TreeNode existing;
    result = store->GetNode(*existing_id, &existing);
    if (result != tab_tree::TabTreeStore::Result::kOk ||
        !existing.is_temporary || existing.tombstone) {
      return false;
    }
    // The user is explicitly saving the live page. A previously dormant
    // logical NTP may not yet have local target bytes in its native row.
    if (existing.url.is_empty()) {
      result = store->UpdateSavedPageMetadata(existing.id, existing.title, url,
                                              base::Time::Now());
      if (result != tab_tree::TabTreeStore::Result::kOk) {
        OnMutationFailed(result);
        return false;
      }
    }
    SidebarTreeController::DropPlan plan;
    const auto validation = controller_->ValidateDrop(
        existing.id, target, SidebarTreeController::DropOperation::kMove,
        &plan);
    if (validation == SidebarTreeController::DropValidationResult::kNoOp) {
      result = store->SetPageTemporary(existing.id, false, base::Time::Now());
    } else if (validation ==
               SidebarTreeController::DropValidationResult::kAllowed) {
      result =
          store->MoveSavedPagesAtomically({{.node_id = existing.id,
                                            .workspace_id = plan.workspace_id,
                                            .parent_id = plan.parent_id,
                                            .sort_key = plan.sort_key,
                                            .is_temporary = false}},
                                          base::Time::Now());
    } else {
      return false;
    }
    if (result == tab_tree::TabTreeStore::Result::kOk) {
      result = store->GetNode(existing.id, &created);
    }
  } else {
    result = controller_->CreateSavedPageAtDrop(target, title, url,
                                                base::Time::Now(), &created);
  }
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
    return false;
  }
  if (!weak_tab ||
      !session_bridge_->BindTreeNodeToTab(created, weak_tab.get())) {
    const tab_tree::TabTreeStore::Result rollback =
        controller_->UndoLastMutation();
    if (rollback != tab_tree::TabTreeStore::Result::kOk) {
      OnMutationFailed(rollback);
    }
    return false;
  }
  if (created.parent_id.has_value()) {
    std::ignore = controller_->ExpandNode(*created.parent_id);
  }
  std::ignore = controller_->SelectNode(created.id);
  if (created_node_id) {
    *created_node_id = created.id;
  }
  rollback_extraction.ReplaceClosure(base::OnceClosure());
  OnTemporaryTabDragStateChanged(std::nullopt);
  ScheduleRuntimePresentationRefresh();
  return true;
}

BrowserSidebarSplitDropSource BrowserSidebarHostView::MaterializeSavedPage(
    const tab_tree::TreeNode& requested_node,
    bool require_local_model) {
  tab_tree::TreeNode node;
  if (!session_bridge_ || !session_bridge_->is_ready() ||
      session_bridge_->tab_tree_store()->GetNode(requested_node.id, &node) !=
          tab_tree::TabTreeStore::Result::kOk ||
      node.tombstone || node.type != tab_tree::TreeNodeType::kSavedPage) {
    return {};
  }
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

  const auto target = tab_tree::GetSharedPageTarget(node);
  if (!target) {
    return {};
  }
  GURL navigation_url;
  switch (target->kind) {
    case sync::SharedTabTargetKind::kWeb:
      navigation_url = GURL(target->url);
      break;
    case sync::SharedTabTargetKind::kNewTab:
      navigation_url = GURL(chrome::kChromeUINewTabURL);
      break;
    case sync::SharedTabTargetKind::kLocalOnly:
      // Only existing LOCAL native target bytes can open here, never a wire
      // placeholder, invented URL, JS execution or an OS-scheme fallback.
      if (node.url.is_empty() || !node.url.is_valid() ||
          !(node.url.SchemeIs("chrome") ||
            node.url.SchemeIs("chrome-extension") ||
            node.url.SchemeIs("about") || node.url.SchemeIs("file"))) {
        return {};
      }
      navigation_url = node.url;
      break;
  }
  Browser* const navigation_browser = browser_;
  NavigateParams params(navigation_browser, navigation_url,
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

}  // namespace ahoi::sidebar
