// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Deleting a Workspace that owns a website-session partition (ADR 0011 order
// step 1, crest-hardening handoff 003). The partition's pages are asked as one
// before-unload group; only an all-agree answer commits the tree deletion,
// retires the local binding, closes the pages and removes the partition's
// data. Startup resumes an interrupted removal.

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/website_session_context.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace ahoi {
namespace {

// Only directories inside this profile's own Ahoi partition root may be
// deleted, never an arbitrary recorded path.
bool IsOwnWebsiteSessionPartitionPath(const base::FilePath& profile_path,
                                      const base::FilePath& path) {
  const base::FilePath root = profile_path.Append(FILE_PATH_LITERAL("Storage"))
                                  .Append(FILE_PATH_LITERAL("ext"))
                                  .Append(FILE_PATH_LITERAL(
                                      session::kWebsiteSessionPartitionDomain));
  return !path.empty() && !path.ReferencesParent() && root.IsParent(path);
}

}  // namespace

std::vector<tabs::TabInterface*> SessionBridge::IsolatedPagesOfWorkspace(
    const base::Uuid& workspace_id,
    const std::optional<session::WebsiteSessionBinding>& binding) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<tabs::TabInterface*> pages;
  if (!binding.has_value() || binding->is_default()) {
    return pages;
  }
  for (const auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.workspace_id != workspace_id || !runtime.web_contents) {
      continue;
    }
    const std::optional<session::WebsiteSessionBinding> page_binding =
        session::WebsiteSessionBindingForWebContents(
            profile_, runtime.web_contents.get());
    if (page_binding == binding) {
      pages.push_back(tab);
    }
  }
  return pages;
}

bool SessionBridge::HasOwnWebsiteSessions(
    const base::Uuid& workspace_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::optional<session::WebsiteSessionBinding> binding =
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), workspace_id);
  return binding.has_value() && !binding->is_default();
}

void SessionBridge::DeleteWorkspaceClosingIsolatedPages(
    const base::Uuid& workspace_id,
    WorkspaceDeletionCallback done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_) {
    std::move(done).Run(tab_tree::TabTreeStore::Result::kNotInitialized);
    return;
  }
  if (workspace_deletion_close_) {
    // One deletion question at a time; the dialog is modal to the sidebar.
    std::move(done).Run(tab_tree::TabTreeStore::Result::kCancelled);
    return;
  }
  const std::optional<session::WebsiteSessionBinding> binding =
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), workspace_id);
  const std::vector<tabs::TabInterface*> pages =
      IsolatedPagesOfWorkspace(workspace_id, binding);
  if (pages.empty()) {
    std::move(done).Run(CommitWorkspaceDeletion(workspace_id, binding));
    return;
  }
  std::vector<content::WebContents*> contents;
  for (tabs::TabInterface* tab : pages) {
    contents.push_back(tab->GetContents());
  }
  workspace_deletion_close_ = session::GroupPageClose::Ask(
      std::move(contents),
      base::BindOnce(&SessionBridge::OnWorkspaceDeletionPagesAnswered,
                     weak_ptr_factory_.GetWeakPtr(), workspace_id,
                     std::move(done)));
}

void SessionBridge::OnWorkspaceDeletionPagesAnswered(
    base::Uuid workspace_id,
    WorkspaceDeletionCallback done,
    bool all_agreed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unique_ptr<session::GroupPageClose> group =
      std::move(workspace_deletion_close_);
  if (!all_agreed || shutting_down_) {
    // A veto leaves the Workspace, its tabs, binding and data unchanged.
    std::move(done).Run(tab_tree::TabTreeStore::Result::kCancelled);
    return;
  }
  const std::optional<session::WebsiteSessionBinding> binding =
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), workspace_id);
  const tab_tree::TabTreeStore::Result result =
      CommitWorkspaceDeletion(workspace_id, binding);
  if (result == tab_tree::TabTreeStore::Result::kOk && group) {
    group->ClosePages();
  }
  if (result == tab_tree::TabTreeStore::Result::kOk && binding.has_value() &&
      !binding->is_default()) {
    // Give the closing pages a moment to leave the partition before clearing
    // it; the directory itself is removed at the next launch.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SessionBridge::ClearRetiredWebsiteSessionData,
                       weak_ptr_factory_.GetWeakPtr(), binding->context_id),
        base::Seconds(3));
  }
  std::move(done).Run(result);
}

tab_tree::TabTreeStore::Result SessionBridge::CommitWorkspaceDeletion(
    const base::Uuid& workspace_id,
    const std::optional<session::WebsiteSessionBinding>& binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (workspace_service_->ordered_workspaces().size() <= 1) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  auto fallback = std::ranges::find_if(
      workspace_service_->ordered_workspaces(),
      [&workspace_id](const tab_tree::Workspace& candidate) {
        return candidate.id != workspace_id;
      });
  if (fallback == workspace_service_->ordered_workspaces().end()) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  const base::Uuid fallback_id = fallback->id;
  const bool isolated = binding.has_value() && !binding->is_default();
  const std::vector<tabs::TabInterface*> isolated_pages =
      IsolatedPagesOfWorkspace(workspace_id, binding);

  const tab_tree::TabTreeStore::Result result =
      tab_tree_store_->DeleteWorkspace(workspace_id, base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }

  if (binding.has_value()) {
    base::FilePath partition_path;
    if (isolated) {
      if (content::StoragePartition* partition = profile_->GetStoragePartition(
              session::StoragePartitionConfigForWebsiteSession(profile_,
                                                                *binding))) {
        partition_path = partition->GetPath();
      }
    }
    // Retire the binding and record the removal intent right after the tree
    // commit. Startup also retires bindings of Workspaces that no longer
    // exist, so a crash between both writes cannot leave a restorable
    // partition behind.
    if (!session::RetireWebsiteSessionBinding(profile_->GetPrefs(),
                                              workspace_id, partition_path)) {
      LOG(ERROR) << "Ahoi could not retire a deleted Workspace's binding";
    }
    profile_->GetPrefs()->CommitPendingWrite();
  }

  bool runtime_changed = false;
  for (auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.workspace_id != workspace_id) {
      continue;
    }
    UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
    RemoveTabFromLastActiveState(tab);
    if (std::ranges::find(isolated_pages, tab) != isolated_pages.end()) {
      // Closed by the caller after this commit; never re-homed with the
      // deleted Workspace's accounts into the fallback.
      runtime_changed = true;
      continue;
    }
    runtime.workspace_id = fallback_id;
    if (tab->IsActivated() && runtime.tab_strip_model) {
      UpdateLastActiveTab(runtime.tab_strip_model, tab);
    }
    PersistTabSessionMetadata(tab);
    runtime_changed = true;
  }
  if (!RefreshWorkspaceSnapshot()) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }
  if (runtime_changed) {
    runtime_presentation_changed_callbacks_.Notify();
  }
  return tab_tree::TabTreeStore::Result::kOk;
}

void SessionBridge::ClearRetiredWebsiteSessionData(base::Uuid context_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !context_id.is_valid()) {
    return;
  }
  const session::WebsiteSessionBinding binding{.context_id = context_id};
  content::StoragePartition* partition = profile_->GetStoragePartition(
      session::StoragePartitionConfigForWebsiteSession(profile_, binding),
      /*can_create=*/false);
  if (!partition) {
    return;  // Never loaded this run; the directory goes at next launch.
  }
  partition->ClearData(
      content::StoragePartition::REMOVE_DATA_MASK_ALL,
      /*filter_builder=*/nullptr,
      content::StoragePartition::StorageKeyPolicyMatcherFunction(),
      network::mojom::CookieDeletionFilter::New(),
      /*perform_storage_cleanup=*/true, base::Time(), base::Time::Max(),
      base::DoNothing());
}

void SessionBridge::ResumeWebsiteSessionRemovals() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PrefService* prefs = profile_->GetPrefs();
  // Bindings whose Workspace no longer exists belong to an interrupted
  // deletion: retire them now so restore treats their pages as unknown.
  const std::vector<base::Uuid> workspaces = OrderedWorkspaceIdsForSession();
  const std::set<base::Uuid> existing(workspaces.begin(), workspaces.end());
  std::vector<base::Uuid> orphaned;
  for (const base::Uuid& id :
       session::GetWebsiteSessionBoundWorkspaceIds(prefs)) {
    if (!existing.contains(id)) {
      orphaned.push_back(id);
    }
  }
  for (const base::Uuid& id : orphaned) {
    const std::optional<session::WebsiteSessionBinding> binding =
        session::FindWebsiteSessionBinding(prefs, id);
    base::FilePath path;
    if (binding.has_value() && !binding->is_default()) {
      if (content::StoragePartition* partition = profile_->GetStoragePartition(
              session::StoragePartitionConfigForWebsiteSession(profile_,
                                                                *binding))) {
        path = partition->GetPath();
      }
    }
    session::RetireWebsiteSessionBinding(prefs, id, path);
    if (binding.has_value() && !binding->is_default()) {
      // A page restored before this retirement must not stay open with the
      // deleted Workspace's accounts.
      for (const auto& [tab, runtime] : runtime_tabs_) {
        if (runtime.web_contents &&
            session::WebsiteSessionBindingForWebContents(
                profile_, runtime.web_contents.get()) == binding) {
          runtime.web_contents->ClosePage();
        }
      }
    }
  }
  if (!orphaned.empty()) {
    prefs->CommitPendingWrite();
  }

  const base::FilePath profile_path = profile_->GetPath();
  for (const session::PendingWebsiteSessionRemoval& pending :
       session::GetPendingWebsiteSessionRemovals(prefs)) {
    if (!IsOwnWebsiteSessionPartitionPath(profile_path,
                                          pending.partition_path)) {
      LOG(ERROR) << "Ahoi ignored a website-session removal outside its root";
      session::CompleteWebsiteSessionRemoval(prefs, pending.context_id);
      continue;
    }
    // Clear through the native path first when the partition is loaded (a
    // restored page may still hold it), then delete the directory.
    ClearRetiredWebsiteSessionData(pending.context_id);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(&base::DeletePathRecursively, pending.partition_path),
        base::BindOnce(&SessionBridge::OnWebsiteSessionDirectoryDeleted,
                       weak_ptr_factory_.GetWeakPtr(), pending.context_id));
  }
}

void SessionBridge::OnWebsiteSessionDirectoryDeleted(base::Uuid context_id,
                                                     bool deleted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (deleted && !shutting_down_ && profile_) {
    session::CompleteWebsiteSessionRemoval(profile_->GetPrefs(), context_id);
  }
}

}  // namespace ahoi
