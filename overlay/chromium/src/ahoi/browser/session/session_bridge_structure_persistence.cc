// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/session/portable_workspace_import_plan.h"
#include "ahoi/browser/session/workspace_structure_controller.h"
#include "base/functional/bind.h"
#include "base/location.h"

namespace ahoi {
std::vector<base::Uuid> SessionBridge::GetArchivePageGroup(
    base::Uuid node_id) const {
  if (!is_ready() || !workspace_structure_controller_)
    return {};
  for (const auto& [id, entry] :
       workspace_structure_controller_->state_.entries) {
    const auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
    if (split && !split->tombstone &&
        std::ranges::find(split->topology.member_ids, node_id) !=
            split->topology.member_ids.end())
      return split->topology.member_ids;
  }
  return {node_id};
}

bool SessionBridge::CanArchiveTemporaryPages(
    const std::vector<base::Uuid>& nodes) const {
  return workspace_structure_controller_ &&
         workspace_structure_controller_->CanArchive(nodes);
}
tab_tree::TabTreeStore::Result SessionBridge::SetWorkspaceArchivePolicy(
    base::Uuid workspace_id,
    sync::SharedArchivePolicy policy) {
  if (!is_ready())
    return tab_tree::TabTreeStore::Result::kNotInitialized;
  const auto result = tab_tree_store_->SetWorkspaceArchivePolicy(
      workspace_id, policy, base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk)
    return result;
  return RefreshWorkspaceSnapshot()
             ? result
             : tab_tree::TabTreeStore::Result::kDatabaseError;
}

void SessionBridge::ArchiveTemporaryPages(
    std::vector<base::Uuid> nodes,
    base::OnceCallback<void(bool)> completion) {
  if (!is_ready() || !workspace_structure_controller_) {
    std::move(completion).Run(false);
    return;
  }
  workspace_structure_controller_->Archive(
      nodes, sync::SharedArchiveReason::kManual, std::move(completion));
}

void SessionBridge::RestoreArchivedPages(
    base::Uuid entry_id,
    base::OnceCallback<void(bool)> completion) {
  if (!is_ready() || !workspace_structure_controller_) {
    std::move(completion).Run(false);
    return;
  }
  workspace_structure_controller_->Restore(entry_id, std::move(completion));
}

std::vector<sync::TabArchiveEntryRecord> SessionBridge::GetArchivedPages()
    const {
  return workspace_structure_controller_
             ? workspace_structure_controller_->Archives()
             : std::vector<sync::TabArchiveEntryRecord>();
}

void SessionBridge::RestoreArchivedPagesAt(
    base::Uuid entry_id,
    tab_tree::ArchiveRestorePlacement placement,
    base::OnceCallback<void(bool)> completion) {
  if (!is_ready() || !workspace_structure_controller_) {
    std::move(completion).Run(false);
    return;
  }
  workspace_structure_controller_->Restore(entry_id, std::move(completion),
                                           placement);
}

void SessionBridge::DeleteArchivedPages(
    sync::TabArchiveEntryRecord expected,
    base::OnceCallback<void(bool)> completion) {
  if (!is_ready() || !workspace_structure_controller_) {
    std::move(completion).Run(false);
    return;
  }
  workspace_structure_controller_->DeleteArchive(std::move(expected),
                                                 std::move(completion));
}

void SessionBridge::CommitWorkspaceStructureState(
    std::string state,
    base::RepeatingCallback<bool()> authorization,
    base::OnceCallback<void(bool)> completion,
    std::optional<tab_tree::TabTreeSnapshot> tree) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  using Store = tab_tree::TabTreeStore;
  if (!is_ready() || !persistence_enabled_ || !persistence_task_runner_ ||
      pending_tree_apply_cancelled_ || !authorization || !authorization.Run()) {
    std::move(completion).Run(false);
    return;
  }
  Store::PersistenceSnapshot before;
  if (tab_tree_store_->ExportPersistenceSnapshot(&before) !=
          Store::Result::kOk ||
      !durable_tree_snapshot_ || before != *durable_tree_snapshot_) {
    std::move(completion).Run(false);
    return;
  }
  Store::PersistenceSnapshot projected = before;
  if (tree) {
    if (tree->undo_operations != before.tree.undo_operations) {
      std::move(completion).Run(false);
      return;
    }
    projected.tree = std::move(*tree);
  }
  projected.workspace_structure_state = std::move(state);
  pending_tree_apply_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  pending_tree_apply_completion_ = base::BindOnce(
      [](base::WeakPtr<SessionBridge> bridge, bool changed_tree,
         base::OnceCallback<void(bool)> done, Store::Result result) {
        if (bridge && changed_tree && result == Store::Result::kOk)
          bridge->RequestLocalTabCapture();
        std::move(done).Run(result == Store::Result::kOk);
      },
      weak_ptr_factory_.GetWeakPtr(), tree.has_value(), std::move(completion));
  auto guarded = base::BindRepeating(
      [](base::RepeatingCallback<bool()> original,
         std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire) && original.Run();
      },
      std::move(authorization), pending_tree_apply_cancelled_);
  auto persist = base::BindOnce(
      [](base::FilePath path, Store::PersistenceSnapshot snapshot,
         base::RepeatingCallback<bool()> scope) {
        if (!scope.Run()) {
          return Store::Result::kCancelled;
        }
        Store store;
        return store.Initialize(path)
                   ? store.ReplacePersistenceSnapshot(snapshot, scope)
                   : Store::Result::kDatabaseError;
      },
      tab_tree_database_path_, projected, guarded);
  auto reply = base::BindOnce(&SessionBridge::OnSyncedTabTreePersisted,
                              weak_ptr_factory_.GetWeakPtr(), std::move(before),
                              std::move(projected),
                              pending_tree_apply_cancelled_, guarded);
  if (!persistence_task_runner_->PostTaskAndReplyWithResult(
          FROM_HERE, std::move(persist), std::move(reply))) {
    CancelPendingSyncedTabTreeApply();
    pending_tree_apply_cancelled_.reset();
    std::move(pending_tree_apply_completion_)
        .Run(Store::Result::kNotInitialized);
  }
}

void SessionBridge::CommitPortableWorkspaceImport(
    const session::PortableWorkspaceStructure& imported,
    base::RepeatingCallback<bool()> authorization,
    base::OnceCallback<void(PortableImportResult)> completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(completion);
  auto* controller = workspace_structure_controller_.get();
  if (!is_ready() || !persistence_enabled_ || !controller ||
      !controller->sync_ || !controller->observing_sync_ ||
      controller->persisting_ || controller->dirty_ ||
      controller->read_pending_ || controller->publish_pending_ ||
      pending_tree_apply_cancelled_ || !authorization || !authorization.Run()) {
    std::move(completion).Run(PortableImportResult::kUnavailable);
    return;
  }
  tab_tree::TabTreeStore::PersistenceSnapshot before;
  if (tab_tree_store_->ExportPersistenceSnapshot(&before) !=
          tab_tree::TabTreeStore::Result::kOk ||
      !durable_tree_snapshot_ || before != *durable_tree_snapshot_) {
    std::move(completion).Run(PortableImportResult::kUnavailable);
    return;
  }
  const auto structure =
      session::DecodeWorkspaceStructureState(before.workspace_structure_state);
  if (!structure || controller->state_.entries != structure->entries ||
      controller->state_.clock != structure->clock) {
    std::move(completion).Run(PortableImportResult::kUnavailable);
    return;
  }
  auto plan = session::PreparePortableWorkspaceImport(
      imported, before.tree, *structure, controller->sync_->local_device_id(),
      base::Time::Now());
  if (!plan) {
    std::move(completion).Run(PortableImportResult::kConflict);
    return;
  }
  if (!plan->changed) {
    std::move(completion).Run(PortableImportResult::kNoChanges);
    return;
  }

  std::optional<tab_tree::TabTreeSnapshot> changed_tree;
  if (plan->tree != before.tree) {
    changed_tree = std::move(plan->tree);
  }
  controller->persisting_ = true;
  auto guarded = base::BindRepeating(
      [](base::RepeatingCallback<bool()> requested,
         base::RepeatingCallback<bool()> local) {
        return requested.Run() && local.Run();
      },
      std::move(authorization), controller->LocalAuthority());
  CommitWorkspaceStructureState(
      std::move(plan->encoded_structure), std::move(guarded),
      base::BindOnce(
          [](base::WeakPtr<SessionBridge> bridge,
             session::WorkspaceStructureState next,
             base::OnceCallback<void(PortableImportResult)> done, bool ok) {
            if (!bridge || !bridge->workspace_structure_controller_) {
              std::move(done).Run(PortableImportResult::kUnavailable);
              return;
            }
            auto* controller = bridge->workspace_structure_controller_.get();
            controller->persisting_ = false;
            if (!ok) {
              controller->Schedule();
              std::move(done).Run(PortableImportResult::kFailed);
              return;
            }
            controller->state_ = std::move(next);
            controller->clock_.Restore(controller->state_.clock);
            controller->dirty_ = false;
            controller->Schedule();
            std::move(done).Run(PortableImportResult::kImported);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(plan->structure),
          std::move(completion)),
      std::move(changed_tree));
}
}  // namespace ahoi
