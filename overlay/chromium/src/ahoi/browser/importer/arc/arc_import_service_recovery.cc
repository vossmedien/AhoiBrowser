// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "ahoi/browser/importer/arc/arc_import_recovery_policy.h"
#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi::importer::arc {

void ArcImportService::BeginPreparedRecovery(ArcImportPreviewCallback callback,
                                             ArcImportPreparedState prepared) {
  if (prepared.phase == ArcImportPreparedPhase::kManualRecoveryRequired) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  if (!profile_ || !session_bridge_ || !session_bridge_->is_ready()) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  tab_tree::TabTreeSnapshot recovery_start_tree;
  if (!session_bridge_->ExportTabTreeSnapshot(&recovery_start_tree)) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&VerifyAndLoadArcImportBackup, profile_->GetPath(),
                     prepared.backup_identifier, prepared.manifest_sha256,
                     prepared.snapshot_hash),
      base::BindOnce(&ArcImportService::OnRecoveryBackupLoaded,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(prepared), std::move(recovery_start_tree)));
}

void ArcImportService::OnRecoveryBackupLoaded(
    ArcImportPreviewCallback callback,
    ArcImportPreparedState prepared,
    tab_tree::TabTreeSnapshot recovery_start_tree,
    ArcImportBackupRecoveryResult recovery) {
  if (recovery.status != ArcImportStatus::kOk || !recovery.previous_tree ||
      !session_bridge_ || !session_bridge_->is_ready()) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  tab_tree::TabTreeSnapshot live;
  if (!session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != recovery_start_tree) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  tab_tree::TabTreeSnapshot previous_tree = std::move(*recovery.previous_tree);
  auto fingerprint_task = base::BindOnce(
      [](tab_tree::TabTreeSnapshot previous,
         tab_tree::TabTreeSnapshot current) {
        return std::array<std::string, 2>{
            ComputeArcImportTreeFingerprint(previous),
            ComputeArcImportTreeFingerprint(current)};
      },
      previous_tree, recovery_start_tree);
  auto fingerprint_reply = base::BindOnce(
      &ArcImportService::OnRecoveryFingerprintsComputed,
      weak_factory_.GetWeakPtr(), std::move(callback), std::move(prepared),
      std::move(recovery_start_tree), std::move(previous_tree));
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(fingerprint_task), std::move(fingerprint_reply));
}

void ArcImportService::OnRecoveryFingerprintsComputed(
    ArcImportPreviewCallback callback,
    ArcImportPreparedState prepared,
    tab_tree::TabTreeSnapshot recovery_start_tree,
    tab_tree::TabTreeSnapshot previous_tree,
    std::array<std::string, 2> fingerprints) {
  // Hashing crossed a worker boundary. The exact captured input must still be
  // live before the fingerprint can authorize the one possible tree write.
  tab_tree::TabTreeSnapshot live;
  if (!session_bridge_ || !session_bridge_->is_ready() ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != recovery_start_tree ||
      fingerprints[0] != prepared.previous_tree_sha256) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  const ArcImportRecoveryDecision decision =
      DecideArcImportPreparedRecovery(prepared, fingerprints[1]);
  const bool restore_previous_journal =
      decision.completion ==
      ArcImportRecoveryCompletion::kRestorePreviousJournal;
  if (decision.tree_action == ArcImportRecoveryTreeAction::kNone) {
    if (restore_previous_journal) {
      RestorePreparedRecoveryJournal(std::move(callback),
                                     std::move(prepared.previous_committed));
    } else {
      MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    }
    return;
  }

  if (session_bridge_->ApplySyncedTabTreeSnapshot(previous_tree) !=
      tab_tree::TabTreeStore::Result::kOk) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  tab_tree::TabTreeSnapshot applied;
  if (!session_bridge_->ExportTabTreeSnapshot(&applied) ||
      applied != previous_tree) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  session_bridge_->FlushPersistenceForBackup(base::BindOnce(
      &ArcImportService::OnRecoveryPersistenceFlushed,
      weak_factory_.GetWeakPtr(), std::move(callback), std::move(prepared),
      std::move(previous_tree), restore_previous_journal));
}

void ArcImportService::OnRecoveryPersistenceFlushed(
    ArcImportPreviewCallback callback,
    ArcImportPreparedState prepared,
    tab_tree::TabTreeSnapshot expected_tree,
    bool restore_previous_journal,
    bool persistence_flushed) {
  tab_tree::TabTreeSnapshot durable;
  if (!persistence_flushed || !session_bridge_ ||
      !session_bridge_->ExportTabTreeSnapshot(&durable) || !profile_ ||
      durable != expected_tree) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  if (!restore_previous_journal) {
    MarkPreparedRecoveryManual(std::move(callback), std::move(prepared));
    return;
  }
  RestorePreparedRecoveryJournal(std::move(callback),
                                 std::move(prepared.previous_committed));
}

void ArcImportService::RestorePreparedRecoveryJournal(
    ArcImportPreviewCallback callback,
    std::optional<ArcImportCommittedState> previous_committed) {
  if (!profile_) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RestoreArcImportJournalAfterRollback, profile_->GetPath(),
                     std::move(previous_committed)),
      base::BindOnce(&ArcImportService::OnRecoveryJournalRestored,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ArcImportService::OnRecoveryJournalRestored(
    ArcImportPreviewCallback callback,
    bool journal_restored) {
  operation_in_progress_ = false;
  if (!journal_restored) {
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  DiscoverAndPreview(std::move(callback));
}

void ArcImportService::MarkPreparedRecoveryManual(
    ArcImportPreviewCallback callback,
    ArcImportPreparedState prepared) {
  prepared.phase = ArcImportPreparedPhase::kManualRecoveryRequired;
  if (!profile_) {
    operation_in_progress_ = false;
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                     std::move(prepared)),
      base::BindOnce(&ArcImportService::FinishPreparedRecoveryManual,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ArcImportService::FinishPreparedRecoveryManual(
    ArcImportPreviewCallback callback,
    bool /*journal_written*/) {
  operation_in_progress_ = false;
  std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
}

}  // namespace ahoi::importer::arc
