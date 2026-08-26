// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_internal.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

bool SessionBridge::ShouldTrackBrowser(
    const BrowserWindowInterface* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !shutting_down_ && profile_ && browser &&
         browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         !browser->IsDeleteScheduled() && browser->GetTabStripModel();
}

void SessionBridge::TrackBrowser(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ShouldTrackBrowser(browser) || windows_.contains(browser)) {
    return;
  }

  TabStripModel* model = browser->GetTabStripModel();
  if (model_windows_.contains(model)) {
    return;
  }

  base::Uuid window_id;
  do {
    window_id = base::Uuid::GenerateRandomV4();
  } while (!window_id.is_valid() || id_windows_.contains(window_id));

  windows_.emplace(
      browser, WindowState{.window_id = window_id, .tab_strip_model = model});
  id_windows_.emplace(window_id, browser);
  model_windows_.emplace(model, browser);
  model->AddObserver(this);

  if (workspace_service_ && !workspace_service_->ordered_workspaces().empty()) {
    CHECK(workspace_service_->SetActiveWorkspace(
        window_id, workspace_service_->ordered_workspaces().front().id,
        WorkspaceActivationSource::kDataReconciliation));
  }

  for (tabs::TabInterface* tab : *model) {
    TrackRuntimeTab(model, tab, tab->GetContents());
  }
  // BrowserCollection observers do not have a contractual ordering. Defer
  // the initial write so SessionService has first registered the new window;
  // later user-driven workspace changes are still written immediately.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<SessionBridge> bridge,
             base::WeakPtr<BrowserWindowInterface> weak_browser) {
            if (bridge && weak_browser) {
              bridge->PersistWindowSessionMetadata(weak_browser.get());
            }
          },
          weak_ptr_factory_.GetWeakPtr(), browser->GetWeakPtr()));
  PublishCommandItems();
}

void SessionBridge::UntrackBrowser(BrowserWindowInterface* browser,
                                   bool tab_strip_model_destroyed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto window_it = windows_.find(browser);
  if (window_it == windows_.end()) {
    return;
  }

  const base::Uuid window_id = window_it->second.window_id;
  TabStripModel* model = window_it->second.tab_strip_model;
  if (model && !tab_strip_model_destroyed) {
    model->RemoveObserver(this);
  }

  std::vector<tabs::TabInterface*> tabs_to_remove;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.tab_strip_model == model) {
      tabs_to_remove.push_back(tab);
    }
  }
  for (tabs::TabInterface* tab : tabs_to_remove) {
    RemoveRuntimeTab(tab);
  }

  if (workspace_service_) {
    workspace_service_->ForgetWindow(window_id);
  }
  model_windows_.erase(model);
  id_windows_.erase(window_id);
  windows_.erase(window_it);
  PublishCommandItems();
}

void SessionBridge::UpdateRuntimeTabContents(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end()) {
    return;
  }

  if (old_contents) {
    auto old_it = contents_tabs_.find(old_contents);
    if (old_it != contents_tabs_.end() &&
        (!old_it->second || old_it->second.get() == tab)) {
      contents_tabs_.erase(old_it);
    }
  }

  if (new_contents) {
    auto new_it = contents_tabs_.find(new_contents);
    if (new_it != contents_tabs_.end()) {
      tabs::TabInterface* existing_tab = new_it->second.get();
      if (existing_tab && existing_tab != tab) {
        UnbindTreeNodeFromTab(tab);
        runtime_it->second.web_contents.reset();
        return;
      }
      contents_tabs_.erase(new_it);
    }
    contents_tabs_.insert_or_assign(new_contents, runtime_it->second.tab);
    runtime_it->second.web_contents = new_contents->GetWeakPtr();
  } else {
    runtime_it->second.web_contents.reset();
  }
}

void SessionBridge::OnTabWillDiscardContents(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!shutting_down_) {
    UpdateRuntimeTabContents(tab, old_contents, new_contents);
  }
}

void SessionBridge::OnTabWillDetach(tabs::TabInterface* tab,
                                    tabs::TabInterface::DetachReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  if (reason == tabs::TabInterface::DetachReason::kDelete) {
    RemoveRuntimeTab(tab);
    return;
  }
  auto it = runtime_tabs_.find(tab);
  if (it != runtime_tabs_.end()) {
    it->second.tab_strip_model = nullptr;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SessionBridge::RemoveDetachedTabIfStillUnattached,
                       weak_ptr_factory_.GetWeakPtr(), tab, tab->GetWeakPtr()));
  }
}

void SessionBridge::OnTabDidInsert(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return;
  }
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  if (!ShouldTrackBrowser(browser)) {
    RemoveRuntimeTab(tab);
    return;
  }
  TrackBrowser(browser);
  TrackRuntimeTab(browser->GetTabStripModel(), tab, tab->GetContents());
  PublishCommandItems();
}

void SessionBridge::OnTabUIChanged(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return;
  }
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end() ||
      runtime_it->second.tab.get() != tab) {
    return;
  }

  RuntimeTabState& runtime = runtime_it->second;
  content::WebContents* contents = runtime.web_contents.get();
  const GURL current_url = session_internal::GetRuntimeTabUrl(contents);
  const std::u16string current_title =
      session_internal::GetRuntimeTabTitle(tab, current_url);
  const std::u16string previous_automatic_title = runtime.last_observed_title;
  runtime.last_observed_url = current_url;
  runtime.last_observed_title = current_title;

  // Keep the visible tree title live while respecting an explicit user rename:
  // only replace the stored value if it still equals Chromium's previous title
  // for this runtime tab.
  if (runtime.node_id.has_value() && tab_tree_store_) {
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(*runtime.node_id, &node) ==
        tab_tree::TabTreeStore::Result::kOk) {
      std::u16string persisted_title = node.title;
      if (!previous_automatic_title.empty() &&
          previous_automatic_title != current_title &&
          node.title == previous_automatic_title) {
        persisted_title = current_title;
      }
      if (node.url != current_url || node.title != persisted_title) {
        std::ignore = tab_tree_store_->UpdateSavedPageMetadata(
            node.id, std::move(persisted_title), current_url,
            base::Time::Now());
      }
    }
  }
  PublishCommandItems();
}

void SessionBridge::RemoveDetachedTabIfStillUnattached(
    tabs::TabInterface* tab,
    base::WeakPtr<tabs::TabInterface> tab_weak_ptr) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  auto it = runtime_tabs_.find(tab);
  if (it == runtime_tabs_.end() || it->second.tab.get() != tab_weak_ptr.get() ||
      it->second.tab_strip_model) {
    return;
  }

  // Native window moves detach and reinsert synchronously. If an owner drops
  // a detached TabModel instead, no kDelete notification follows the earlier
  // kInsertIntoOtherWindow notification. Retire that orphan after the current
  // operation, before a later task can observe stale raw-pointer indexes. The
  // callback subscriptions self-invalidate when their callback lists die.
  if (!tab_weak_ptr) {
    RemoveRuntimeTab(tab);
    return;
  }

  BrowserWindowInterface* browser = tab_weak_ptr->GetBrowserWindowInterface();
  if (!ShouldTrackBrowser(browser)) {
    RemoveRuntimeTab(tab);
    return;
  }
  TrackBrowser(browser);
  TrackRuntimeTab(browser->GetTabStripModel(), tab_weak_ptr.get(),
                  tab_weak_ptr->GetContents());
  if (!runtime_tabs_.at(tab).tab_strip_model) {
    RemoveRuntimeTab(tab);
  }
  PublishCommandItems();
}

bool SessionBridge::WorkspaceExists(const base::Uuid& workspace_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!workspace_service_ || !workspace_id.is_valid()) {
    return false;
  }
  for (const auto& workspace : workspace_service_->ordered_workspaces()) {
    if (workspace.id == workspace_id) {
      return true;
    }
  }
  return false;
}

bool SessionBridge::RefreshWorkspaceSnapshot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_store_ || !workspace_service_) {
    return false;
  }
  std::vector<tab_tree::Workspace> workspaces;
  if (tab_tree_store_->GetWorkspaces(&workspaces) !=
          tab_tree::TabTreeStore::Result::kOk ||
      workspaces.empty() ||
      !workspace_service_->ReplaceWorkspaces(std::move(workspaces))) {
    return false;
  }
  ScheduleTabTreePersistence();
  PublishCommandItems();
  runtime_presentation_changed_callbacks_.Notify();
  return true;
}

void SessionBridge::ScheduleWorkspaceReconciliation() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || workspace_reconciliation_scheduled_) {
    return;
  }
  workspace_reconciliation_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SessionBridge::ReconcileWorkspaces,
                                weak_ptr_factory_.GetWeakPtr()));
}

void SessionBridge::ReconcileWorkspaces() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  workspace_reconciliation_scheduled_ = false;
  if (shutting_down_ || !workspace_service_) {
    return;
  }

  std::set<base::Uuid> visible_workspaces;
  for (const auto& workspace : workspace_service_->ordered_workspaces()) {
    visible_workspaces.insert(workspace.id);
  }

  const std::optional<base::Uuid> fallback_workspace =
      workspace_service_->ordered_workspaces().empty()
          ? std::nullopt
          : std::make_optional(
                workspace_service_->ordered_workspaces().front().id);
  std::vector<tabs::TabInterface*> tabs_to_reassign;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.workspace_id.has_value() &&
        !visible_workspaces.contains(*runtime.workspace_id)) {
      tabs_to_reassign.push_back(tab);
    }
  }
  bool presentation_changed = false;
  for (tabs::TabInterface* tab : tabs_to_reassign) {
    UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
    auto runtime = runtime_tabs_.find(tab);
    if (runtime != runtime_tabs_.end()) {
      RemoveTabFromLastActiveState(tab);
      runtime->second.workspace_id = fallback_workspace;
      if (tab->IsActivated() && runtime->second.tab_strip_model) {
        UpdateLastActiveTab(runtime->second.tab_strip_model, tab);
      }
      PersistTabSessionMetadata(tab);
      presentation_changed = true;
    }
  }

  if (!fallback_workspace.has_value()) {
    return;
  }
  for (const auto& [browser, window] : windows_) {
    if (!workspace_service_->GetActiveWorkspace(window.window_id).has_value()) {
      CHECK(workspace_service_->SetActiveWorkspace(
          window.window_id, *fallback_workspace,
          WorkspaceActivationSource::kDataReconciliation));
    }
  }
  if (presentation_changed) {
    runtime_presentation_changed_callbacks_.Notify();
  }
}

void SessionBridge::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TrackBrowser(browser);
}

void SessionBridge::OnBrowserClosed(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!shutting_down_) {
    UntrackBrowser(browser, /*tab_strip_model_destroyed=*/false);
  }
}

void SessionBridge::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !model_windows_.contains(tab_strip_model)) {
    return;
  }

  switch (change.type()) {
    case TabStripModelChange::kSelectionOnly:
      break;
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        TrackRuntimeTab(tab_strip_model, inserted.tab, inserted.contents);
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& removed : change.GetRemove()->contents) {
        if (removed.tab_detach_reason ==
            tabs::TabInterface::DetachReason::kDelete) {
          RemoveRuntimeTab(removed.tab);
        } else {
          auto it = runtime_tabs_.find(removed.tab);
          if (it != runtime_tabs_.end()) {
            it->second.tab_strip_model = nullptr;
          }
        }
      }
      break;
    case TabStripModelChange::kMoved: {
      const TabStripModelChange::Move* moved = change.GetMove();
      TrackRuntimeTab(tab_strip_model, moved->tab, moved->contents);
      break;
    }
    case TabStripModelChange::kReplaced: {
      const TabStripModelChange::Replace* replaced = change.GetReplace();
      TrackRuntimeTab(tab_strip_model, replaced->tab, replaced->new_contents);
      break;
    }
  }
  if (selection.active_tab_changed() && selection.new_tab) {
    UpdateLastActiveTab(tab_strip_model, selection.new_tab);
  }
  PublishCommandItems();
}

void SessionBridge::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  auto model_it = model_windows_.find(tab_strip_model);
  if (model_it != model_windows_.end()) {
    UntrackBrowser(model_it->second,
                   /*tab_strip_model_destroyed=*/true);
  }
}

void SessionBridge::OnWorkspaceListChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ScheduleWorkspaceReconciliation();
  PublishCommandItems();
  NotifyTabTreeSnapshotChanged();
}

void SessionBridge::OnActiveWorkspaceChanged(const base::Uuid& window_id,
                                             const std::optional<base::Uuid>&,
                                             const std::optional<base::Uuid>&,
                                             WorkspaceActivationSource) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  BrowserWindowInterface* browser = FindWindowById(window_id);
  if (browser) {
    PersistWindowSessionMetadata(browser);
  }
  PublishCommandItems();
}

void SessionBridge::OnTabTreeChanged(const tab_tree::TabTreeChange& change) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const base::Uuid& node_id : change.node_ids) {
    auto bound = node_tabs_.find(node_id);
    if (bound == node_tabs_.end()) {
      continue;
    }
    tabs::TabInterface* tab = bound->second.get();
    if (!tab) {
      node_tabs_.erase(bound);
      continue;
    }
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(node_id, &node) !=
            tab_tree::TabTreeStore::Result::kOk ||
        node.tombstone) {
      // A user moving a saved page back below the separator expects the live
      // tab to survive as a temporary tab in the same workspace.
      UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
      continue;
    }
    auto runtime = runtime_tabs_.find(tab);
    if (runtime != runtime_tabs_.end()) {
      if (runtime->second.workspace_id != node.workspace_id) {
        RemoveTabFromLastActiveState(tab);
      }
      runtime->second.workspace_id = node.workspace_id;
      if (tab->IsActivated() && runtime->second.tab_strip_model) {
        UpdateLastActiveTab(runtime->second.tab_strip_model, tab);
      }
      PersistTabSessionMetadata(tab);
    }
  }
  ScheduleTabTreePersistence();
  PublishCommandItems();
  NotifyTabTreeSnapshotChanged();
}

}  // namespace ahoi
