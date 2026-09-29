// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Zusammenführen mit …" (ADR 0012, crest-hardening handoff 080). The tree
// moves in one store transaction (TabTreeStore::MergeWorkspace, handoff
// 078). Open pages follow their nodes into the target when both Workspaces
// share one web context. Otherwise the source's pages are asked as one
// before-unload group, as on deletion: a veto changes nothing, and after the
// commit they close and the source's own website sessions are retired. Two
// partitions or profiles are never merged.

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/website_session_context.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {
namespace {

bool UsesDefaultContext(
    const std::optional<session::WebsiteSessionBinding>& binding) {
  return !binding.has_value() || binding->is_default();
}

constexpr char kReceiptTargetKey[] = "target";
constexpr char kReceiptRulesKey[] = "rules";
constexpr char kReceiptDefaultKey[] = "default";

// Link-routing rules and an explicit default route that named the merged
// Workspace point at the target instead of an unavailable Workspace. Returns
// the retargeted rule IDs; `default_changed` reports the default route.
std::vector<base::Uuid> RetargetRouting(navigation::RoutingSettings& settings,
                                        const base::Uuid& from,
                                        const base::Uuid& to,
                                        bool* default_changed) {
  std::vector<base::Uuid> retargeted;
  for (navigation::RoutingRule& rule : settings.rules) {
    if (rule.target_workspace_id == from) {
      rule.target_workspace_id = to;
      retargeted.push_back(rule.id);
    }
  }
  *default_changed = settings.default_route.target_workspace_id == from;
  if (*default_changed) {
    settings.default_route.target_workspace_id = to;
  }
  return retargeted;
}

// Undo of a merge revives the source; only the entries this merge moved and
// that still name its target go back, so later user edits win.
void RestoreRoutingForRevived(PrefService* prefs,
                              const std::set<base::Uuid>& live_workspaces) {
  const base::DictValue& receipts =
      prefs->GetDict(session::kWorkspaceMergeRoutingReceiptsPref);
  std::vector<std::string> consumed;
  navigation::RoutingSettings routing = navigation::ReadRoutingSettings(*prefs);
  bool changed = false;
  for (const auto [key, value] : receipts) {
    const base::Uuid source = base::Uuid::ParseLowercase(key);
    if (!source.is_valid() || !live_workspaces.contains(source)) {
      continue;
    }
    consumed.push_back(key);
    const base::DictValue* receipt = value.GetIfDict();
    const std::string* target_text =
        receipt ? receipt->FindString(kReceiptTargetKey) : nullptr;
    const base::Uuid target = target_text
                                  ? base::Uuid::ParseLowercase(*target_text)
                                  : base::Uuid();
    if (!target.is_valid()) {
      continue;
    }
    std::set<base::Uuid> rule_ids;
    if (const base::ListValue* rules = receipt->FindList(kReceiptRulesKey)) {
      for (const base::Value& id : *rules) {
        if (id.is_string()) {
          rule_ids.insert(base::Uuid::ParseLowercase(id.GetString()));
        }
      }
    }
    for (navigation::RoutingRule& rule : routing.rules) {
      if (rule_ids.contains(rule.id) && rule.target_workspace_id == target) {
        rule.target_workspace_id = source;
        changed = true;
      }
    }
    if (receipt->FindBool(kReceiptDefaultKey).value_or(false) &&
        routing.default_route.target_workspace_id == target) {
      routing.default_route.target_workspace_id = source;
      changed = true;
    }
  }
  if (changed && !navigation::WriteRoutingSettings(prefs, routing)) {
    LOG(ERROR) << "Ahoi could not restore link routing after a merge undo";
    return;
  }
  ScopedDictPrefUpdate update(prefs,
                              session::kWorkspaceMergeRoutingReceiptsPref);
  for (const std::string& key : consumed) {
    update->Remove(key);
  }
}

}  // namespace

bool SessionBridge::SharesWebContext(const base::Uuid& source_id,
                                     const base::Uuid& target_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const PrefService* prefs = profile_->GetPrefs();
  return UsesDefaultContext(
             session::FindWebsiteSessionBinding(prefs, source_id)) &&
         UsesDefaultContext(
             session::FindWebsiteSessionBinding(prefs, target_id));
}

void SessionBridge::MergeWorkspace(const base::Uuid& source_id,
                                   const base::Uuid& target_id,
                                   bool into_folder,
                                   WorkspaceDeletionCallback done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_) {
    std::move(done).Run(tab_tree::TabTreeStore::Result::kNotInitialized);
    return;
  }
  if (source_id == target_id || !WorkspaceExists(source_id) ||
      !WorkspaceExists(target_id)) {
    std::move(done).Run(tab_tree::TabTreeStore::Result::kInvalidArgument);
    return;
  }
  if (workspace_deletion_close_) {
    // One deletion or merge question at a time; the dialog is modal.
    std::move(done).Run(tab_tree::TabTreeStore::Result::kCancelled);
    return;
  }
  std::vector<content::WebContents*> contents;
  std::vector<base::WeakPtr<content::WebContents>> asked;
  if (!SharesWebContext(source_id, target_id)) {
    for (const auto& [tab, runtime] : runtime_tabs_) {
      if (runtime.workspace_id == source_id && runtime.web_contents) {
        contents.push_back(runtime.web_contents.get());
        asked.push_back(runtime.web_contents);
      }
    }
  }
  if (contents.empty()) {
    std::move(done).Run(CommitWorkspaceMerge(source_id, target_id,
                                             into_folder, /*closing=*/{}));
    return;
  }
  workspace_deletion_close_ = session::GroupPageClose::Ask(
      std::move(contents),
      base::BindOnce(&SessionBridge::OnWorkspaceMergePagesAnswered,
                     weak_ptr_factory_.GetWeakPtr(), source_id, target_id,
                     into_folder, std::move(asked), std::move(done)));
}

void SessionBridge::OnWorkspaceMergePagesAnswered(
    base::Uuid source_id,
    base::Uuid target_id,
    bool into_folder,
    std::vector<base::WeakPtr<content::WebContents>> asked_pages,
    WorkspaceDeletionCallback done,
    bool all_agreed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unique_ptr<session::GroupPageClose> group =
      std::move(workspace_deletion_close_);
  if (!all_agreed || shutting_down_ || !WorkspaceExists(source_id) ||
      !WorkspaceExists(target_id)) {
    // A veto leaves both Workspaces, their tabs, bindings and data unchanged.
    std::move(done).Run(tab_tree::TabTreeStore::Result::kCancelled);
    return;
  }
  // Every page of the source's context closes, including one opened while
  // the question was showing (handoff 010 R3).
  std::vector<tabs::TabInterface*> closing;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.workspace_id == source_id) {
      closing.push_back(tab);
    }
  }
  const std::optional<session::WebsiteSessionBinding> binding =
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), source_id);
  const tab_tree::TabTreeStore::Result result =
      CommitWorkspaceMerge(source_id, target_id, into_folder, closing);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    std::move(done).Run(result);
    return;
  }
  if (group) {
    group->ClosePages();
  }
  for (const auto& [tab, runtime] : runtime_tabs_) {
    content::WebContents* contents = runtime.web_contents.get();
    if (runtime.closing_with_deleted_workspace && contents &&
        std::ranges::none_of(asked_pages, [contents](const auto& asked) {
          return asked.get() == contents;
        })) {
      contents->ClosePage();
    }
  }
  if (!UsesDefaultContext(binding)) {
    ClearRetiredWebsiteSessionDataAfterCloses(
        binding->context_id, WebContentsClosingWithDeletedWorkspace(),
        base::TimeTicks::Now() + base::Seconds(30));
  }
  std::move(done).Run(result);
}

tab_tree::TabTreeStore::Result SessionBridge::CommitWorkspaceMerge(
    const base::Uuid& source_id,
    const base::Uuid& target_id,
    bool into_folder,
    const std::vector<tabs::TabInterface*>& closing) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::optional<session::WebsiteSessionBinding> binding =
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), source_id);
  std::vector<tab_tree::TreeNode> target_roots;
  if (tab_tree_store_->GetChildren(target_id, std::nullopt, &target_roots) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }
  tab_tree::TabTreeStore::WorkspaceMerge merge{
      .source_workspace_id = source_id,
      .target_workspace_id = target_id,
      .into_folder = into_folder,
      // Same convention as a new temporary page: after the last root.
      .sort_key = target_roots.empty() ? "@"
                                       : target_roots.back().sort_key + "@",
      // Retired website sessions cannot come back with an undo.
      .record_undo = UsesDefaultContext(binding),
      .modified_at = base::Time::Now(),
  };
  for (tabs::TabInterface* tab : closing) {
    const auto runtime = runtime_tabs_.find(tab);
    if (runtime != runtime_tabs_.end() && runtime->second.node_id &&
        runtime->second.is_temporary) {
      merge.closing_temporary_ids.insert(*runtime->second.node_id);
    }
  }
  std::optional<base::Uuid> folder_id;
  const tab_tree::TabTreeStore::Result result =
      tab_tree_store_->MergeWorkspace(merge, &folder_id);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }

  if (binding.has_value()) {
    // As on deletion: retired right after the tree commit, resumable after a
    // crash; a default binding is only removed.
    const base::FilePath partition_path =
        UsesDefaultContext(binding)
            ? base::FilePath()
            : session::WebsiteSessionPartitionPath(profile_->GetPath(),
                                                   *binding);
    if (!session::RetireWebsiteSessionBinding(profile_->GetPrefs(), source_id,
                                              partition_path)) {
      LOG(ERROR) << "Ahoi could not retire a merged Workspace's binding";
    }
    profile_->GetPrefs()->CommitPendingWrite();
  }

  // Bound pages already followed their nodes (OnTabTreeChanged). Closing
  // pages are never re-homed; an unbound page of the source moves along.
  MergeRuntimeReceipt runtime_receipt{.target_id = target_id};
  for (auto& [tab, runtime] : runtime_tabs_) {
    const bool is_closing = std::ranges::find(closing, tab) != closing.end();
    if (is_closing) {
      runtime.closing_with_deleted_workspace = true;
      UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/true);
      continue;
    }
    if (runtime.workspace_id != source_id) {
      continue;
    }
    RemoveTabFromLastActiveState(tab);
    runtime.workspace_id = target_id;
    runtime_receipt.unbound_tabs.push_back(tab->GetWeakPtr());
    if (tab->IsActivated() && runtime.tab_strip_model) {
      UpdateLastActiveTab(runtime.tab_strip_model, tab);
    }
    PersistTabSessionMetadata(tab);
  }
  // Windows that showed the source show the target, not the first Workspace.
  for (const auto& [browser, window] : windows_) {
    if (workspace_service_->GetActiveWorkspace(window.window_id) ==
            source_id &&
        workspace_service_->SetActiveWorkspace(
            window.window_id, target_id, WorkspaceActivationSource::kSidebar)) {
      runtime_receipt.window_ids.push_back(window.window_id);
    }
  }
  merge_runtime_receipts_.erase(source_id);
  if (merge.record_undo) {
    merge_runtime_receipts_.emplace(source_id, std::move(runtime_receipt));
  }
  PrefService* prefs = profile_->GetPrefs();
  navigation::RoutingSettings routing =
      navigation::ReadRoutingSettings(*prefs);
  bool default_changed = false;
  const std::vector<base::Uuid> retargeted =
      RetargetRouting(routing, source_id, target_id, &default_changed);
  {
    ScopedDictPrefUpdate receipts(prefs,
                                  session::kWorkspaceMergeRoutingReceiptsPref);
    receipts->Remove(source_id.AsLowercaseString());
    if (!retargeted.empty() || default_changed) {
      if (!navigation::WriteRoutingSettings(prefs, routing)) {
        LOG(ERROR) << "Ahoi could not retarget link routing after a merge";
      } else if (merge.record_undo) {
        base::ListValue rule_ids;
        for (const base::Uuid& id : retargeted) {
          rule_ids.Append(id.AsLowercaseString());
        }
        receipts->Set(source_id.AsLowercaseString(),
                      base::DictValue()
                          .Set(kReceiptTargetKey, target_id.AsLowercaseString())
                          .Set(kReceiptRulesKey, std::move(rule_ids))
                          .Set(kReceiptDefaultKey, default_changed));
      }
    }
  }
  if (!RefreshWorkspaceSnapshot()) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }
  runtime_presentation_changed_callbacks_.Notify();
  return tab_tree::TabTreeStore::Result::kOk;
}

void SessionBridge::RefreshWorkspacesAfterUndo() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Undoing a merge revives its source (TabTreeStore::UndoLastMutation).
  std::vector<tab_tree::Workspace> stored;
  if (!workspace_service_ ||
      tab_tree_store_->GetWorkspaces(&stored) !=
          tab_tree::TabTreeStore::Result::kOk ||
      stored.size() == workspace_service_->ordered_workspaces().size()) {
    return;
  }
  std::ignore = RefreshWorkspaceSnapshot();
  std::set<base::Uuid> live;
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    live.insert(workspace.id);
  }
  RestoreRoutingForRevived(profile_->GetPrefs(), live);
  RestoreMergeRuntimeForRevived(live);
}

void SessionBridge::RestoreMergeRuntimeForRevived(
    const std::set<base::Uuid>& live) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto it = merge_runtime_receipts_.begin();
       it != merge_runtime_receipts_.end();) {
    const base::Uuid source_id = it->first;
    if (!live.contains(source_id)) {
      ++it;
      continue;
    }
    const MergeRuntimeReceipt receipt = std::move(it->second);
    it = merge_runtime_receipts_.erase(it);
    // Only what the merge moved and what still shows its target goes back;
    // later user choices win.
    for (const base::WeakPtr<tabs::TabInterface>& weak_tab :
         receipt.unbound_tabs) {
      tabs::TabInterface* const tab = weak_tab.get();
      const auto runtime = tab ? runtime_tabs_.find(tab) : runtime_tabs_.end();
      if (runtime == runtime_tabs_.end() || runtime->second.node_id ||
          runtime->second.workspace_id != receipt.target_id) {
        continue;
      }
      RemoveTabFromLastActiveState(tab);
      runtime->second.workspace_id = source_id;
      PersistTabSessionMetadata(tab);
    }
    for (const base::Uuid& window_id : receipt.window_ids) {
      if (workspace_service_->GetActiveWorkspace(window_id) ==
          receipt.target_id) {
        std::ignore = workspace_service_->SetActiveWorkspace(
            window_id, source_id,
            WorkspaceActivationSource::kDataReconciliation);
      }
    }
  }
}

}  // namespace ahoi
