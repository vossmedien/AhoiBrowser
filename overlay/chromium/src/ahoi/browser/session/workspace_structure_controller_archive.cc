// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_structure_controller.h"

#include <algorithm>
#include <set>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::session {
namespace {
using Store = tab_tree::TabTreeStore;

base::TimeDelta ArchiveAge(sync::SharedArchivePolicy policy) {
  switch (policy) {
    case sync::SharedArchivePolicy::kNever:
      return base::TimeDelta::Max();
    case sync::SharedArchivePolicy::kTwelveHours:
      return base::Hours(12);
    case sync::SharedArchivePolicy::kTwentyFourHours:
      return base::Hours(24);
    case sync::SharedArchivePolicy::kSevenDays:
      return base::Days(7);
    case sync::SharedArchivePolicy::kThirtyDays:
      return base::Days(30);
  }
  return base::TimeDelta::Max();
}

std::vector<base::Uuid> PageIds(const sync::TabArchiveEntryRecord& entry) {
  std::vector<base::Uuid> ids;
  for (const auto& page : entry.snapshot.pages)
    ids.push_back(page.tree_node_id);
  return ids;
}
}  // namespace

bool WorkspaceStructureController::CanArchive(
    const std::vector<base::Uuid>& ids) const {
  if (!observing_sync_ || !bridge_lifetime_ || !bridge_->is_ready() ||
      !resources_ || browser_shutdown::IsTryingToQuit() || ids.empty() ||
      ids.size() > 4)
    return false;
  std::set<base::Uuid> unique(ids.begin(), ids.end());
  if (unique.size() != ids.size())
    return false;
  base::Uuid workspace;
  for (const auto& id : ids) {
    tab_tree::TreeNode node;
    if (bridge_->tab_tree_store()->GetNode(id, &node) != Store::Result::kOk ||
        node.tombstone || !node.is_temporary ||
        !tab_tree::GetSharedPageTarget(node))
      return false;
    if (!workspace.is_valid())
      workspace = node.workspace_id;
    if (workspace != node.workspace_id)
      return false;
    if (auto* tab = bridge_->FindTabByTreeNodeId(id)) {
      if (!resources_->CanArchiveTab(tab))
        return false;
    }
  }
  // A split can only leave the live tree as a complete unit, including when
  // some/all of its members currently have no WebContents.
  bool complete_group = ids.size() == 1;
  for (const auto& [id, entry] : state_.entries) {
    const auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
    if (!split || split->tombstone)
      continue;
    const std::set<base::Uuid> members(split->topology.member_ids.begin(),
                                       split->topology.member_ids.end());
    if (std::ranges::none_of(members, [&](const auto& member) {
          return unique.contains(member);
        }))
      continue;
    if (members != unique)
      return false;
    complete_group = true;
  }
  // An uncaptured native split is a missing dependency, not an ordinary page.
  for (const auto& id : ids) {
    auto* tab = bridge_->FindTabByTreeNodeId(id);
    if (tab && tab->GetSplit()) {
      const auto token = tab->GetSplit()->ToString();
      if (std::ranges::none_of(state_.entries, [&](const auto& item) {
            return item.second.native_split_token == token &&
                   std::holds_alternative<sync::SplitGroupRecord>(
                       item.second.record);
          }))
        return false;
    }
  }
  return complete_group;
}

std::vector<sync::TabArchiveEntryRecord>
WorkspaceStructureController::Archives() const {
  std::vector<sync::TabArchiveEntryRecord> result;
  for (const auto& [id, entry] : state_.entries) {
    const auto* archive =
        std::get_if<sync::TabArchiveEntryRecord>(&entry.record);
    if (archive && !archive->tombstone && !archive->restored)
      result.push_back(*archive);
  }
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    return a.archived_at != b.archived_at ? a.archived_at > b.archived_at
                                          : a.id < b.id;
  });
  return result;
}

void WorkspaceStructureController::Archive(
    const std::vector<base::Uuid>& nodes,
    sync::SharedArchiveReason reason,
    base::OnceCallback<void(bool)> done) {
  if (persisting_ || publish_pending_ || !CanArchive(nodes)) {
    std::move(done).Run(false);
    return;
  }
  sync::SharedArchiveSnapshot snapshot;
  std::vector<base::Uuid> ordered = nodes;
  for (const auto& [id, entry] : state_.entries) {
    const auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
    if (split && !split->tombstone &&
        std::ranges::find(split->topology.member_ids, nodes.front()) !=
            split->topology.member_ids.end()) {
      snapshot.split = sync::SharedSplitMetadata{
          split->id, split->workspace_id, split->topology, split->ratios};
      ordered = split->topology.member_ids;
      break;
    }
  }
  WorkspaceStructureEntry candidate;
  for (const auto& id : ordered) {
    tab_tree::TreeNode node;
    if (bridge_->tab_tree_store()->GetNode(id, &node) != Store::Result::kOk ||
        bridge_->tab_tree_store()->IsNodeArchived(id)) {
      std::move(done).Run(false);
      return;
    }
    snapshot.workspace_id = node.workspace_id;
    snapshot.pages.push_back(
        {node.id, node.parent_id, node.sort_key,
         base::UTF16ToUTF8(tab_tree::GetSharedPageTitle(node)),
         *tab_tree::GetSharedPageTarget(node),
         tab_tree::GetSharedHomeTarget(node)});
    candidate.private_nodes.push_back(node);
  }
  if (!sync::ValidateArchiveSnapshot(snapshot)) {
    std::move(done).Run(false);
    return;
  }
  const auto id = sync::ArchiveIdForSnapshot(snapshot);
  const auto existing = state_.entries.find(id);
  std::optional<WorkspaceStructureEntry> before;
  if (existing != state_.entries.end()) {
    const auto* previous =
        std::get_if<sync::TabArchiveEntryRecord>(&existing->second.record);
    if (!previous || previous->tombstone) {
      std::move(done).Run(false);
      return;
    }
    before = existing->second;
    candidate.baseline = before->baseline;
  }
  candidate.record =
      sync::TabArchiveEntryRecord{.id = id,
                                  .snapshot = std::move(snapshot),
                                  .reason = reason,
                                  .archived_at = base::Time::Now()};
  if (!Stamp(&candidate.record, before ? &before->record : nullptr)) {
    std::move(done).Run(false);
    return;
  }
  candidate.archived_locally = true;
  const auto version = candidate.record;
  state_.entries.insert_or_assign(id, std::move(candidate));
  local_changes_.insert(id);
  remote_authorities_.erase(id);
  blocked_publications_.erase(id);
  const auto authority = LocalAuthority();
  Persist(authority,
          base::BindOnce(
              [](base::WeakPtr<WorkspaceStructureController> owner,
                 base::Uuid id, std::optional<WorkspaceStructureEntry> before,
                 sync::SyncRecord version, sync::SyncAuthorization authority,
                 base::OnceCallback<void(bool)> done, bool ok) {
                if (!owner) {
                  std::move(done).Run(false);
                  return;
                }
                if (!ok) {
                  auto it = owner->state_.entries.find(id);
                  if (it != owner->state_.entries.end() &&
                      it->second.record == version) {
                    if (before)
                      it->second = std::move(*before);
                    else
                      owner->state_.entries.erase(it);
                  }
                  std::move(done).Run(false);
                  return;
                }
                owner->CloseArchived(id, authority);
                if (!owner) {
                  std::move(done).Run(false);
                  return;
                }
                bool closed = authority.Run();
                const auto& entry = std::get<sync::TabArchiveEntryRecord>(
                    owner->state_.entries.at(id).record);
                for (const auto& page : entry.snapshot.pages)
                  closed &=
                      !owner->bridge_->FindTabByTreeNodeId(page.tree_node_id);
                owner->Schedule();
                std::move(done).Run(closed);
              },
              weak_factory_.GetWeakPtr(), id, std::move(before), version,
              authority, std::move(done)));
}

void WorkspaceStructureController::CloseArchived(
    base::Uuid id,
    sync::SyncAuthorization authority) {
  const auto found = state_.entries.find(id);
  if (found == state_.entries.end() || !authority.Run() || !bridge_lifetime_)
    return;
  const auto* archive =
      std::get_if<sync::TabArchiveEntryRecord>(&found->second.record);
  if (!archive || archive->restored || archive->tombstone ||
      !found->second.archived_locally)
    return;
  const auto ids = PageIds(*archive);
  for (const auto& node : ids) {
    // A failed/cancelled disk operation must never become a close on a later
    // unrelated notification, even if its prepared RAM entry still exists.
    if (!bridge_->tab_tree_store()->IsNodeArchived(node))
      return;
  }
  if (!CanArchive(ids))
    return;
  const auto scope = scope_;
  base::AutoReset<int> applying(&scope->applying, scope->applying + 1);
  const auto lifetime = weak_factory_.GetWeakPtr();
  for (const auto& node : ids) {
    if (!lifetime || !bridge_lifetime_ || !authority.Run())
      return;
    auto* tab = bridge_->FindTabByTreeNodeId(node);
    if (!tab)
      continue;
    if (!resources_->CanArchiveTab(tab))
      return;
    auto* model = bridge_->FindTabStripModelForTab(tab);
    if (!model || model->closing_all())
      return;
    const int index = model->GetIndexOfTab(tab);
    if (index < 0)
      return;
    model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  }
}

bool WorkspaceStructureController::CanRestore(
    const sync::TabArchiveEntryRecord& archive) const {
  if (!bridge_lifetime_ || !bridge_->is_ready())
    return false;
  tab_tree::Workspace workspace;
  if (bridge_->tab_tree_store()->GetWorkspace(
          archive.snapshot.workspace_id, &workspace) != Store::Result::kOk ||
      workspace.tombstone)
    return false;
  for (const auto& page : archive.snapshot.pages) {
    tab_tree::TreeNode node, parent;
    if (bridge_->tab_tree_store()->GetNode(page.tree_node_id, &node) !=
            Store::Result::kOk ||
        node.tombstone || !node.is_temporary ||
        node.workspace_id != archive.snapshot.workspace_id ||
        node.parent_id != page.parent_id || node.sort_key != page.sort_key ||
        (page.parent_id &&
         (bridge_->tab_tree_store()->GetNode(*page.parent_id, &parent) !=
              Store::Result::kOk ||
          parent.tombstone || parent.type != tab_tree::TreeNodeType::kFolder ||
          parent.workspace_id != node.workspace_id)))
      return false;
  }
  return true;
}

void WorkspaceStructureController::Restore(
    base::Uuid id,
    base::OnceCallback<void(bool)> done) {
  auto found = state_.entries.find(id);
  if (!observing_sync_ || persisting_ || publish_pending_ ||
      found == state_.entries.end()) {
    std::move(done).Run(false);
    return;
  }
  auto* archive =
      std::get_if<sync::TabArchiveEntryRecord>(&found->second.record);
  if (!archive || archive->tombstone || archive->restored) {
    std::move(done).Run(false);
    return;
  }
  // Retained rows are the local identity authority. Missing/changed
  // dependencies require an explicit placement decision; never invent a root or
  // empty split.
  if (!CanRestore(*archive)) {
    std::move(done).Run(false);
    return;
  }
  const auto before = found->second;
  archive->restored = true;
  if (!Stamp(&found->second.record, &before.record)) {
    found->second = before;
    std::move(done).Run(false);
    return;
  }
  found->second.archived_locally = false;
  found->second.restore_pending = false;
  found->second.pending.clear();
  found->second.pending_expected.clear();
  local_changes_.insert(id);
  remote_authorities_.erase(id);
  blocked_publications_.erase(id);
  Persist(LocalAuthority(),
          base::BindOnce(
              [](base::WeakPtr<WorkspaceStructureController> owner,
                 base::Uuid id, WorkspaceStructureEntry before,
                 base::OnceCallback<void(bool)> done, bool ok) {
                if (!owner) {
                  std::move(done).Run(false);
                  return;
                }
                if (!ok)
                  owner->state_.entries.at(id) = std::move(before);
                if (ok)
                  owner->Schedule();
                std::move(done).Run(ok);
              },
              weak_factory_.GetWeakPtr(), id, before, std::move(done)));
}

void WorkspaceStructureController::ReconcileArchives(
    sync::SyncAuthorization authority) {
  if (persisting_ || !authority.Run())
    return;
  const auto lifetime = weak_factory_.GetWeakPtr();
  for (auto& [id, entry] : state_.entries) {
    const auto* archive =
        std::get_if<sync::TabArchiveEntryRecord>(&entry.record);
    if (!archive || archive->tombstone)
      continue;
    const auto remote = remote_authorities_.find(id);
    const bool local = local_changes_.contains(id) || entry.baseline.empty();
    if (!local &&
        (remote == remote_authorities_.end() || !remote->second.Run()))
      continue;
    if (archive->restored) {
      // This only exposes retained tree rows; no loading, focus or navigation.
      if (entry.archived_locally && CanRestore(*archive)) {
        entry.archived_locally = false;
        dirty_ = true;
        Persist(local ? authority : remote->second,
                base::BindOnce(
                    [](base::WeakPtr<WorkspaceStructureController> owner,
                       base::Uuid id, bool ok) {
                      if (!owner)
                        return;
                      if (!ok)
                        owner->state_.entries.at(id).archived_locally = true;
                      else
                        owner->Schedule();
                    },
                    weak_factory_.GetWeakPtr(), id));
        return;
      }
      continue;
    }
    if (entry.archived_locally) {
      CloseArchived(id, local ? authority : remote->second);
      if (!lifetime || !bridge_lifetime_)
        return;
      continue;
    }
    const auto ids = PageIds(*archive);
    if (!CanArchive(ids))
      continue;
    std::vector<tab_tree::TreeNode> retained;
    for (const auto& page : archive->snapshot.pages) {
      tab_tree::TreeNode node;
      if (bridge_->tab_tree_store()->GetNode(page.tree_node_id, &node) !=
              Store::Result::kOk ||
          node.workspace_id != archive->snapshot.workspace_id ||
          node.parent_id != page.parent_id || node.sort_key != page.sort_key ||
          tab_tree::GetSharedPageTarget(node) != page.target ||
          tab_tree::GetSharedHomeTarget(node) != page.home_target)
        break;
      retained.push_back(node);
    }
    if (retained.size() != ids.size())
      continue;
    entry.private_nodes = std::move(retained);
    entry.archived_locally = true;
    dirty_ = true;
    auto original = local ? authority : remote->second;
    Persist(original,
            base::BindOnce(
                [](base::WeakPtr<WorkspaceStructureController> owner,
                   base::Uuid id, sync::SyncAuthorization original, bool ok) {
                  if (!owner)
                    return;
                  if (ok && original.Run())
                    owner->CloseArchived(id, original);
                  else if (!ok)
                    owner->state_.entries.at(id).archived_locally = false;
                },
                weak_factory_.GetWeakPtr(), id, original));
    return;
  }
}

void WorkspaceStructureController::ScanArchiveDeadline() {
  if (persisting_ || !bridge_lifetime_)
    return;
  archive_timer_.Stop();
  const auto now = base::Time::Now();
  base::Time next;
  for (const auto& browser : Windows()) {
    if (!browser)
      continue;
    for (auto* tab : *browser->GetTabStripModel()) {
      const auto id = bridge_->FindSharedTreeNodeIdForTab(tab);
      const auto workspace_id = bridge_->GetWorkspaceForTab(tab);
      tab_tree::Workspace workspace;
      if (!id || !workspace_id ||
          bridge_->tab_tree_store()->IsNodeArchived(*id) ||
          bridge_->tab_tree_store()->GetWorkspace(*workspace_id, &workspace) !=
              Store::Result::kOk ||
          workspace.archive_policy == sync::SharedArchivePolicy::kNever)
        continue;
      std::vector<base::Uuid> ids{*id};
      for (const auto& [entry_id, entry] : state_.entries) {
        const auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
        if (split && !split->tombstone &&
            std::ranges::find(split->topology.member_ids, *id) !=
                split->topology.member_ids.end())
          ids = split->topology.member_ids;
      }
      base::Time last;
      bool complete = true;
      for (const auto& member : ids) {
        auto* live = bridge_->FindTabByTreeNodeId(member);
        if (!live || live->GetLastActiveTime().is_null()) {
          complete = false;
          break;
        }
        last = std::max(last, live->GetLastActiveTime());
      }
      if (!complete || !CanArchive(ids))
        continue;
      const auto deadline = last + ArchiveAge(workspace.archive_policy);
      if (deadline <= now) {
        Archive(
            ids, sync::SharedArchiveReason::kAutomatic,
            base::BindOnce(
                [](base::WeakPtr<WorkspaceStructureController> owner, bool ok) {
                  if (owner && ok)
                    owner->Schedule();
                },
                weak_factory_.GetWeakPtr()));
        return;
      }
      if (next.is_null() || deadline < next)
        next = deadline;
    }
  }
  if (!next.is_null())
    archive_timer_.Start(FROM_HERE, next - now,
                         base::BindOnce(&WorkspaceStructureController::Schedule,
                                        weak_factory_.GetWeakPtr()));
}
}  // namespace ahoi::session
