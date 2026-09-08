// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "ahoi/browser/session/session_bridge.h"
#include "base/auto_reset.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"

namespace ahoi {
namespace {

using Store = tab_tree::TabTreeStore;

Store::Result PersistAuthorizedTree(
    const base::FilePath& path,
    const Store::PersistenceSnapshot& snapshot,
    const base::RepeatingCallback<bool()>& authorization) {
  // This original, thread-safe scope is never renewed on the worker sequence.
  if (!authorization || !authorization.Run()) {
    return Store::Result::kCancelled;
  }
  Store store;
  if (!store.Initialize(path)) {
    return Store::Result::kDatabaseError;
  }
  return store.ReplacePersistenceSnapshot(snapshot, authorization);
}

}  // namespace

sync::SharedTabNativeSupport SessionBridge::GetSharedTabNativeSupport() const {
  // Implementation support is not current readiness or write authority.
  // Loading/undurable snapshots defer through Export and capture instead.
  return {.projection = true, .capture = true};
}

base::CallbackListSubscription SessionBridge::AddSharedTabCaptureCallback(
    base::RepeatingCallback<void(uint64_t)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return shared_tab_capture_callbacks_.Add(std::move(callback));
}

void SessionBridge::RequestSharedTabCapture(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!shutting_down_ && generation) {
    shared_tab_capture_callbacks_.Notify(generation);
  }
}

bool SessionBridge::ExportTabTreeSyncSnapshot(
    tab_tree::TabTreeSnapshot* snapshot,
    std::string* baseline_receipt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!snapshot || !baseline_receipt || !is_ready() || !persistence_enabled_ ||
      pending_tree_apply_cancelled_ || !pending_temporary_closes_.empty()) {
    return false;
  }
  Store::PersistenceSnapshot exported;
  if (tab_tree_store_->ExportPersistenceSnapshot(&exported) !=
          Store::Result::kOk ||
      !durable_tree_snapshot_ || exported != *durable_tree_snapshot_) {
    return false;
  }
  *snapshot = std::move(exported.tree);
  *baseline_receipt = std::move(exported.sync_baseline_receipt);
  return true;
}

void SessionBridge::CancelPendingSyncedTabTreeApply() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (pending_tree_apply_cancelled_) {
    pending_tree_apply_cancelled_->store(true, std::memory_order_release);
  }
}

void SessionBridge::ApplySyncedTabTreeSnapshotWithReceipt(
    tab_tree::TabTreeSnapshot snapshot,
    std::string baseline_receipt,
    base::RepeatingCallback<bool()> authorization,
    base::OnceCallback<void(Store::Result)> completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(completion);
  if (!is_ready() || !persistence_enabled_ || !persistence_task_runner_) {
    std::move(completion).Run(Store::Result::kNotInitialized);
    return;
  }
  if (pending_tree_apply_cancelled_ || !authorization || !authorization.Run()) {
    std::move(completion).Run(Store::Result::kCancelled);
    return;
  }
  if (baseline_receipt.empty() ||
      std::ranges::none_of(snapshot.workspaces, [](const auto& workspace) {
        return !workspace.tombstone;
      })) {
    std::move(completion).Run(Store::Result::kInvalidArgument);
    return;
  }
  Store::PersistenceSnapshot before;
  if (tab_tree_store_->ExportPersistenceSnapshot(&before) !=
      Store::Result::kOk) {
    std::move(completion).Run(Store::Result::kDatabaseError);
    return;
  }
  if (!durable_tree_snapshot_ || before != *durable_tree_snapshot_) {
    std::move(completion).Run(Store::Result::kCancelled);
    return;
  }
  // The prepared projection must retain the actual local undo history. Do not
  // silently change Common's expected tree behind its exact receipt readback.
  if (snapshot.undo_operations != before.tree.undo_operations) {
    std::move(completion).Run(Store::Result::kInvalidArgument);
    return;
  }

  const bool local_flush_pending = persistence_timer_.IsRunning();
  persistence_timer_.Stop();
  if (local_flush_pending) {
    // Cancelling the remote attempt must not also discard a previously due
    // ordinary local write. It runs ahead of the projection on this sequence.
    PersistTabTreeNow();
  }
  pending_tree_apply_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  pending_tree_apply_completion_ = std::move(completion);
  auto guarded = base::BindRepeating(
      [](base::RepeatingCallback<bool()> original,
         std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire) && original.Run();
      },
      std::move(authorization), pending_tree_apply_cancelled_);
  Store::PersistenceSnapshot projected{std::move(snapshot),
                                       std::move(baseline_receipt)};
  // Existing local writes are ahead of this task on the SAME sequence. RAM
  // stays untouched until the remote tree+receipt has committed, so a failed
  // remote write cannot leak through a later ordinary persistence flush.
  auto persist = base::BindOnce(&PersistAuthorizedTree, tab_tree_database_path_,
                                projected, guarded);
  auto reply = base::BindOnce(
      &SessionBridge::OnSyncedTabTreePersisted, weak_ptr_factory_.GetWeakPtr(),
      std::move(before), std::move(projected), pending_tree_apply_cancelled_,
      std::move(guarded));
  if (!persistence_task_runner_->PostTaskAndReplyWithResult(
          FROM_HERE, std::move(persist), std::move(reply))) {
    CancelPendingSyncedTabTreeApply();
    pending_tree_apply_cancelled_.reset();
    std::move(pending_tree_apply_completion_)
        .Run(Store::Result::kNotInitialized);
  }
}

void SessionBridge::OnSyncedTabTreePersisted(
    Store::PersistenceSnapshot before,
    Store::PersistenceSnapshot projected,
    std::shared_ptr<std::atomic<bool>> cancelled,
    base::RepeatingCallback<bool()> authorization,
    Store::Result result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(pending_tree_apply_cancelled_, cancelled);
  auto completion = std::move(pending_tree_apply_completion_);
  pending_tree_apply_cancelled_.reset();
  const bool committed = result == Store::Result::kOk;
  if (result == Store::Result::kDatabaseError) {
    durable_tree_snapshot_.reset();
  }
  if (committed) {
    durable_tree_snapshot_ = projected;
    Store::PersistenceSnapshot current;
    if (!is_ready() || !authorization.Run()) {
      result = Store::Result::kCancelled;
    } else if (tab_tree_store_->ExportPersistenceSnapshot(&current) !=
               Store::Result::kOk) {
      result = Store::Result::kDatabaseError;
    } else if (current != before) {
      result = Store::Result::kCancelled;
    } else {
      // Only this synchronous RAM publication suppresses the Sync observer.
      // Workspace/Sidebar/runtime notifications remain on their normal path;
      // user changes during the disk wait were never suppressed or overwritten.
      base::AutoReset<bool> applying(&applying_synced_tree_snapshot_, true);
      result =
          tab_tree_store_->ReplacePersistenceSnapshot(projected, authorization);
      if (result == Store::Result::kOk && !PublishSyncedTabTreeSnapshot()) {
        result = Store::Result::kDatabaseError;
        if (tab_tree_store_->ReplacePersistenceSnapshot(before) !=
            Store::Result::kOk) {
          persistence_enabled_ = false;
        }
      }
    }
  }
  if (committed && result != Store::Result::kOk) {
    // A local edit/revocation can arrive AFTER the disk commit but BEFORE its
    // UI reply. Preserve the actual local state, including its older baseline,
    // on the same sequence; never call success for an unapplied projection.
    PersistTabTreeNow();
  }
  std::move(completion).Run(result);
}

void SessionBridge::OnLocalTabTreePersisted(Store::PersistenceSnapshot snapshot,
                                            bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!success) {
    durable_tree_snapshot_.reset();
    return;
  }
  const bool newly_durable =
      !durable_tree_snapshot_ || *durable_tree_snapshot_ != snapshot;
  durable_tree_snapshot_ = std::move(snapshot);
  if (!newly_durable || !is_ready() || applying_synced_tree_snapshot_) {
    return;
  }
  Store::PersistenceSnapshot current;
  if (tab_tree_store_->ExportPersistenceSnapshot(&current) ==
          Store::Result::kOk &&
      current == *durable_tree_snapshot_) {
    // The earlier mutation invalidated stale work. Common may observe this
    // SAME revision only now, after its disk commit has actually succeeded.
    NotifyTabTreeSnapshotChanged();
  }
}

}  // namespace ahoi
