// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_service_internal.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/importer/arc/arc_split_receipt.h"
#include "ahoi/browser/importer/arc/arc_split_runtime.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"

namespace ahoi::importer::arc {

void ArcImportService::OnPreparedTreeFlushed(
    std::unique_ptr<CommitContext> context,
    bool persistence_flushed) {
  tab_tree::TabTreeSnapshot durable;
  if (!persistence_flushed || !profile_ || !session_bridge_ ||
      !session_bridge_->ExportTabTreeSnapshot(&durable) ||
      (context->tree_changed &&
       (!context->merged_tree || durable != *context->merged_tree)) ||
      (!context->tree_changed && durable != context->previous_tree)) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kTransactionFailed);
    return;
  }
  if (context->runtime_plan.splits.empty()) {
    OnCommittedPersistenceFlushed(std::move(context), true);
    return;
  }
  context->prepared.phase = ArcImportPreparedPhase::kRuntimeMayHaveStarted;
  context->prepared.native_receipt_sha256.clear();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                     context->prepared),
      base::BindOnce(&ArcImportService::OnRuntimePhaseWritten,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnRuntimePhaseWritten(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  tab_tree::TabTreeSnapshot live;
  if (!journal_written || !session_bridge_ || !session_bridge_->is_ready() ||
      !context->browser || context->browser->GetProfile() != profile_ ||
      context->browser->IsDeleteScheduled() ||
      (context->tree_changed && !context->merged_tree)) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kJournalError);
    return;
  }
  const tab_tree::TabTreeSnapshot& expected_tree =
      context->tree_changed ? *context->merged_tree : context->previous_tree;
  if (!session_bridge_->ExportTabTreeSnapshot(&live) || live != expected_tree) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kJournalError);
    return;
  }

  // From this point onward the durable journal explicitly forbids automatic
  // tree-only rollback. Native tabs and split membership are a second store.
  context->runtime_started = true;
  ArcSplitRuntimeResult runtime = ReconstructArcSplits(
      context->browser.get(), session_bridge_, context->runtime_plan);
  context->opened_tabs = std::move(runtime.opened_tabs);
  if (runtime.status != ArcImportStatus::kOk ||
      VerifyArcSplitRuntime(
          context->browser.get(), session_bridge_, context->runtime_plan,
          /*require_focus=*/true) != ArcSplitVerification::kExact) {
    MarkManualRecoveryAndFinish(std::move(context));
    return;
  }
  context->result.reconstructed_split_count = runtime.reconstructed_split_count;
  context->result.approximated_four_pane_ratio_count =
      runtime.approximated_four_pane_ratio_count;
  BeginNativeSessionReceipt(std::move(context));
}

void ArcImportService::BeginNativeSessionReceipt(
    std::unique_ptr<CommitContext> context) {
  SessionService* service =
      profile_ ? SessionServiceFactory::GetForProfileIfExisting(profile_)
               : nullptr;
  if (!service || !context->browser ||
      context->browser->GetProfile() != profile_ ||
      context->browser->IsDeleteScheduled()) {
    if (context->prepared.transaction_id.empty()) {
      operation_in_progress_ = false;
      context->result.status = ArcImportStatus::kRuntimeFailed;
      std::move(context->callback).Run(std::move(context->result));
    } else {
      MarkManualRecoveryAndFinish(std::move(context));
    }
    return;
  }
  service->ResetFlushAndReadCurrentSessionForVerification(
      base::BindOnce(&ArcImportService::OnNativeSessionReadback,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnNativeSessionReadback(
    std::unique_ptr<CommitContext> context,
    std::vector<std::unique_ptr<sessions::SessionWindow>> windows,
    SessionID active_window_id,
    bool read_error) {
  tab_tree::TabTreeSnapshot live_tree;
  const tab_tree::TabTreeSnapshot* expected_tree =
      context->tree_changed && context->merged_tree ? &*context->merged_tree
                                                    : &context->previous_tree;
  const bool live_exact =
      !read_error && profile_ && session_bridge_ &&
      session_bridge_->is_ready() && context->browser &&
      context->browser->GetProfile() == profile_ &&
      !context->browser->IsDeleteScheduled() &&
      session_bridge_->ExportTabTreeSnapshot(&live_tree) &&
      live_tree == *expected_tree &&
      VerifyArcSplitRuntime(context->browser.get(), session_bridge_,
                            context->runtime_plan,
                            /*require_focus=*/context->runtime_started) ==
          ArcSplitVerification::kExact;
  ArcSplitReceipt receipt;
  if (live_exact) {
    receipt = VerifyArcSplitSessionWindows(
        context->runtime_plan, context->browser->GetSessionID(), windows,
        active_window_id, /*require_focus=*/context->runtime_started);
  }
  const bool receipt_exact =
      live_exact && receipt.verification == ArcSplitVerification::kExact &&
      receipt.verified_split_count == context->runtime_plan.splits.size() &&
      receipt.structure_sha256 ==
          context->prepared.expected_native_structure_sha256 &&
      IsArcImportTreeFingerprint(receipt.receipt_sha256);
  if (!receipt_exact) {
    if (context->prepared.transaction_id.empty()) {
      operation_in_progress_ = false;
      context->result.status = ArcImportStatus::kRuntimeFailed;
      std::move(context->callback).Run(std::move(context->result));
    } else {
      MarkManualRecoveryAndFinish(std::move(context));
    }
    return;
  }

  if (context->prepared.transaction_id.empty()) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kNoChanges;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  context->prepared.native_receipt_sha256 = receipt.receipt_sha256;
  context->prepared.phase = ArcImportPreparedPhase::kRuntimePersisted;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                     context->prepared),
      base::BindOnce(&ArcImportService::OnRuntimePersistedJournalWritten,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnRuntimePersistedJournalWritten(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  tab_tree::TabTreeSnapshot live_tree;
  const tab_tree::TabTreeSnapshot* expected_tree =
      context->tree_changed && context->merged_tree ? &*context->merged_tree
                                                    : &context->previous_tree;
  if (!journal_written || !profile_ || !session_bridge_ ||
      !session_bridge_->is_ready() || !context->browser ||
      context->browser->GetProfile() != profile_ ||
      context->browser->IsDeleteScheduled() ||
      !session_bridge_->ExportTabTreeSnapshot(&live_tree) ||
      live_tree != *expected_tree ||
      VerifyArcSplitRuntime(context->browser.get(), session_bridge_,
                            context->runtime_plan,
                            /*require_focus=*/context->runtime_started) !=
          ArcSplitVerification::kExact) {
    // The durable runtime receipt remains authoritative, but a mutation that
    // crossed the journal worker boundary must not be labelled committed.
    // Recovery keeps both stores untouched instead of compensating one side.
    FinishRecoveryRequired(std::move(context), false);
    return;
  }
  OnCommittedPersistenceFlushed(std::move(context), true);
}

void ArcImportService::OnCommittedPersistenceFlushed(
    std::unique_ptr<CommitContext> context,
    bool persistence_flushed) {
  tab_tree::TabTreeSnapshot durable;
  const bool tree_is_exact =
      persistence_flushed && profile_ && session_bridge_ &&
      session_bridge_->ExportTabTreeSnapshot(&durable) &&
      ((!context->tree_changed && durable == context->previous_tree) ||
       (context->tree_changed && context->merged_tree &&
        durable == *context->merged_tree));
  const bool runtime_receipt_is_exact =
      !context->runtime_started ||
      (context->prepared.phase == ArcImportPreparedPhase::kRuntimePersisted &&
       IsArcImportTreeFingerprint(
           context->prepared.expected_native_structure_sha256) &&
       IsArcImportTreeFingerprint(context->prepared.native_receipt_sha256));
  if (!tree_is_exact || !runtime_receipt_is_exact) {
    if (context->runtime_started) {
      FinishRecoveryRequired(std::move(context), false);
    } else {
      RollbackAndFinish(std::move(context),
                        ArcImportStatus::kTransactionFailed);
    }
    return;
  }
  context->next_committed = MakeArcImportCommittedState(
      context->snapshot_hash, context->selection_fingerprint,
      context->idempotency_key, context->result);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath profile_path, ArcImportCommittedState committed) {
            return WriteArcImportCommittedJournal(profile_path, committed);
          },
          profile_->GetPath(), *context->next_committed),
      base::BindOnce(&ArcImportService::FinishJournalWrite,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::FinishJournalWrite(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  if (!journal_written) {
    if (context->runtime_started) {
      // The kRuntimePersisted marker remains the authoritative crash boundary;
      // never compensate one store after the other has a durable receipt.
      FinishRecoveryRequired(std::move(context), false);
      return;
    }
    if (context->prepared.transaction_id.empty()) {
      operation_in_progress_ = false;
      context->result.status = ArcImportStatus::kJournalError;
      std::move(context->callback).Run(std::move(context->result));
      return;
    }
    if (!profile_) {
      FinishRecoveryRequired(std::move(context), false);
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                       context->prepared),
        base::BindOnce(&ArcImportService::OnPreparedAfterCommitFailure,
                       weak_factory_.GetWeakPtr(), std::move(context)));
    return;
  }
  operation_in_progress_ = false;
  context->result.status = context->tree_changed || context->runtime_started
                               ? ArcImportStatus::kOk
                               : ArcImportStatus::kNoChanges;
  committed_journal_state_ = std::move(context->next_committed);
  std::move(context->callback).Run(std::move(context->result));
}

void ArcImportService::OnPreparedAfterCommitFailure(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  if (!journal_written) {
    FinishRecoveryRequired(std::move(context), false);
    return;
  }
  RollbackAndFinish(std::move(context), ArcImportStatus::kJournalError);
}

}  // namespace ahoi::importer::arc
