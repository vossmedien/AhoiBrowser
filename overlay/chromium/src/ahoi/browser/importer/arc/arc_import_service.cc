// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_service.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_commit_support.h"
#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "ahoi/browser/importer/arc/arc_import_service_internal.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "ahoi/browser/importer/arc/arc_import_transaction_key.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/importer/arc/arc_split_receipt.h"
#include "ahoi/browser/importer/arc/arc_split_runtime.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

namespace ahoi::importer::arc {

struct ArcImportService::DiscoveryResult {
  ArcImportStatus status = ArcImportStatus::kNotFound;
  std::optional<ArcImportPlan> plan;
  std::string snapshot_token;
  std::optional<ArcImportCommittedState> committed;
  std::optional<ArcImportPreparedState> prepared;
  std::optional<ArcSource> source;
  bool arc_is_running = false;
};

ArcImportService::DiscoveryResult ArcImportService::DiscoverImport(
    const base::FilePath& profile_path) {
  DiscoveryResult result;
  const ArcImportJournalReadResult journal = ReadArcImportJournal(profile_path);
  if (journal.status != ArcImportStatus::kOk) {
    result.status = journal.status;
    return result;
  }
  if (journal.state == ArcImportJournalState::kPrepared) {
    result.status = ArcImportStatus::kRecoveryRequired;
    result.prepared = journal.prepared;
    return result;
  }
  result.committed = journal.committed;

  if (IsArcApplicationRunning()) {
    result.status = ArcImportStatus::kSourceInUse;
    result.arc_is_running = true;
    return result;
  }
  const ArcDiscoveryResult discovery = DiscoverDefaultArcSource();
  if (discovery.status != ArcImportStatus::kOk || !discovery.source) {
    result.status = discovery.status;
    return result;
  }
  if (AreArcProfileFilesOpen(*discovery.source)) {
    result.status = ArcImportStatus::kSourceInUse;
    result.arc_is_running = true;
    return result;
  }
  ArcSnapshotResult snapshot = CaptureArcSnapshot(*discovery.source);
  if (snapshot.status != ArcImportStatus::kOk || !snapshot.snapshot) {
    result.status = snapshot.status;
    return result;
  }
  result.snapshot_token = ArcImportSnapshotToken(*snapshot.snapshot);
  ArcParseResult parsed = ParseArcSnapshot(*snapshot.snapshot);
  if (parsed.status != ArcImportStatus::kOk || !parsed.plan) {
    result.status = parsed.status;
    return result;
  }
  result.status = ArcImportStatus::kOk;
  result.source = discovery.source;
  result.plan = std::move(parsed.plan);
  return result;
}

ArcImportService::ArcImportService(Profile* profile,
                                   SessionBridge* session_bridge)
    : profile_(profile), session_bridge_(session_bridge) {}

ArcImportService::~ArcImportService() = default;

void ArcImportService::Shutdown() {
  ++discovery_generation_;
  operation_in_progress_ = false;
  pending_plan_.reset();
  pending_source_.reset();
  pending_snapshot_token_.clear();
  committed_journal_state_.reset();
  weak_factory_.InvalidateWeakPtrs();
  session_bridge_ = nullptr;
  profile_ = nullptr;
}

void ArcImportService::DiscoverAndPreview(ArcImportPreviewCallback callback) {
  if (!callback) {
    return;
  }
  if (!profile_ || !session_bridge_ || operation_in_progress_) {
    std::move(callback).Run({.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  operation_in_progress_ = true;
  pending_plan_.reset();
  pending_source_.reset();
  pending_snapshot_token_.clear();
  const uint64_t generation = ++discovery_generation_;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ArcImportService::DiscoverImport, profile_->GetPath()),
      base::BindOnce(&ArcImportService::OnDiscoveryComplete,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

void ArcImportService::OnDiscoveryComplete(uint64_t generation,
                                           ArcImportPreviewCallback callback,
                                           DiscoveryResult result) {
  if (generation != discovery_generation_) {
    return;
  }
  if (result.prepared) {
    BeginPreparedRecovery(std::move(callback), std::move(*result.prepared));
    return;
  }
  operation_in_progress_ = false;
  ArcImportPreview preview{.status = result.status};
  preview.arc_is_running = result.arc_is_running;
  if (result.status != ArcImportStatus::kOk || !result.plan || !result.source ||
      !session_bridge_ || !session_bridge_->is_ready()) {
    if (result.status == ArcImportStatus::kOk) {
      preview.status = ArcImportStatus::kTransactionFailed;
    }
    std::move(callback).Run(std::move(preview));
    return;
  }

  tab_tree::TabTreeSnapshot current;
  if (!session_bridge_->ExportTabTreeSnapshot(&current)) {
    preview.status = ArcImportStatus::kTransactionFailed;
    std::move(callback).Run(std::move(preview));
    return;
  }
  std::set<std::u16string> existing_names;
  std::set<base::Uuid> existing_ids;
  for (const tab_tree::Workspace& workspace : current.workspaces) {
    if (!workspace.tombstone) {
      existing_names.insert(workspace.name);
      existing_ids.insert(workspace.id);
    }
  }
  for (const tab_tree::Workspace& workspace : result.plan->tree.workspaces) {
    preview.target_workspace_names.push_back(workspace.name);
    if (!existing_ids.contains(workspace.id) &&
        existing_names.contains(workspace.name)) {
      ++preview.conflicting_workspace_count;
    }
  }
  for (const ArcBrowserProfile& profile : result.source->browser_profiles) {
    preview.available_browser_profiles.push_back(profile.directory_name);
  }
  const ArcImportMergeResult idempotence =
      MergeArcImportPlan(current, *result.plan, ArcConflictResolution::kRename);
  preview.already_imported =
      result.committed &&
      result.committed->snapshot_hash == result.snapshot_token &&
      idempotence.status == ArcImportStatus::kNoChanges;
  preview.snapshot_token = result.snapshot_token;
  preview.stats = result.plan->stats;
  committed_journal_state_ = std::move(result.committed);
  pending_snapshot_token_ = result.snapshot_token;
  pending_source_ = std::move(result.source);
  pending_plan_ = std::move(result.plan);
  std::move(callback).Run(std::move(preview));
}

void ArcImportService::Commit(std::string snapshot_token,
                              ArcConflictResolution conflict_resolution,
                              ArcImportSelection selection,
                              BrowserWindowInterface* browser,
                              ArcImportCommitCallback callback) {
  ArcImportCommitResult result;
  if (!callback) {
    return;
  }
  if (!profile_ || !session_bridge_ || !session_bridge_->is_ready() ||
      operation_in_progress_ || !browser || !pending_plan_ ||
      !pending_source_ || browser->GetProfile() != profile_ ||
      !IsValidArcImportSelection(selection, *pending_source_) ||
      snapshot_token.empty() || snapshot_token != pending_snapshot_token_) {
    result.status = snapshot_token == pending_snapshot_token_
                        ? ArcImportStatus::kTransactionFailed
                        : ArcImportStatus::kStalePreview;
    std::move(callback).Run(std::move(result));
    return;
  }
  operation_in_progress_ = true;
  const std::string validation_token = snapshot_token;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ValidateArcImportCommitSource, profile_->GetPath(),
                     *pending_source_, validation_token),
      base::BindOnce(&ArcImportService::OnCommitSourceValidated,
                     weak_factory_.GetWeakPtr(), std::move(snapshot_token),
                     conflict_resolution, std::move(selection),
                     browser->GetWeakPtr(), std::move(callback)));
}

void ArcImportService::OnCommitSourceValidated(
    std::string snapshot_token,
    ArcConflictResolution conflict_resolution,
    ArcImportSelection selection,
    base::WeakPtr<BrowserWindowInterface> browser,
    ArcImportCommitCallback callback,
    ArcImportStatus validation_status) {
  ArcImportCommitResult result;
  if (validation_status != ArcImportStatus::kOk || !profile_ ||
      !session_bridge_ || !session_bridge_->is_ready() || !browser ||
      browser->GetProfile() != profile_ || !pending_plan_ || !pending_source_ ||
      !IsValidArcImportSelection(selection, *pending_source_) ||
      snapshot_token != pending_snapshot_token_) {
    operation_in_progress_ = false;
    result.status = validation_status == ArcImportStatus::kOk
                        ? ArcImportStatus::kStalePreview
                        : validation_status;
    std::move(callback).Run(std::move(result));
    return;
  }

  ArcImportPlan selected_plan =
      SelectArcImportCategories(*pending_plan_, selection);
  tab_tree::TabTreeSnapshot current;
  if (!session_bridge_->ExportTabTreeSnapshot(&current)) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  ArcImportMergeResult merge =
      MergeArcImportPlan(current, selected_plan, conflict_resolution);
  result.status = merge.status;
  result.stats = selected_plan.stats;
  result.renamed_workspace_count = merge.renamed_workspace_count;
  result.skipped_workspace_count = merge.skipped_workspace_count;
  result.merged_workspace_count = merge.merged_workspace_count;
  if (merge.status != ArcImportStatus::kOk &&
      merge.status != ArcImportStatus::kNoChanges) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  const std::string selection_fingerprint =
      ComputeArcImportSelectionFingerprint(
          {.import_sidebar = selection.import_sidebar,
           .reconstruct_splits = selection.reconstruct_splits,
           .conflict_resolution = conflict_resolution,
           .selected_browser_profiles = selection.selected_browser_profiles});
  const std::string idempotency_key =
      ComputeArcImportIdempotencyKey(snapshot_token, selection_fingerprint);
  const bool same_key_replay =
      merge.status == ArcImportStatus::kNoChanges && committed_journal_state_ &&
      committed_journal_state_->idempotency_key == idempotency_key;
  if (!merge.merged_tree || !merge.applied_plan) {
    operation_in_progress_ = false;
    result.status = ArcImportStatus::kTransactionFailed;
    std::move(callback).Run(std::move(result));
    return;
  }

  auto context = std::make_unique<CommitContext>();
  context->callback = std::move(callback);
  context->result = result;
  context->browser = std::move(browser);
  context->selected_source =
      SelectArcImportBrowserProfiles(*pending_source_, selection);
  context->selected_plan = selected_plan;
  context->tree_changed = merge.status == ArcImportStatus::kOk;
  context->merged_tree = std::move(merge.merged_tree);
  context->previous_tree = std::move(current);
  context->snapshot_hash = std::move(snapshot_token);
  context->selection_fingerprint = selection_fingerprint;
  context->idempotency_key = idempotency_key;
  context->prepared.previous_committed = committed_journal_state_;
  context->same_key_replay = same_key_replay;
  const bool recover_existing_snapshot =
      merge.status == ArcImportStatus::kNoChanges &&
      merge.skipped_workspace_count == 0;
  context->runtime_plan = recover_existing_snapshot
                              ? context->selected_plan
                              : std::move(*merge.applied_plan);

  if (!context->runtime_plan.splits.empty()) {
    context->prepared.expected_native_structure_sha256 =
        ComputeArcSplitStructureFingerprint(context->runtime_plan);
    std::set<std::string> native_member_ids;
    for (const ArcSplitDescriptor& split : context->runtime_plan.splits) {
      for (const base::Uuid& member_id : split.member_node_ids) {
        native_member_ids.insert(member_id.AsLowercaseString());
      }
    }
    context->prepared.native_member_ids.assign(native_member_ids.begin(),
                                               native_member_ids.end());
    if (context->prepared.expected_native_structure_sha256.empty() ||
        context->prepared.native_member_ids.empty()) {
      operation_in_progress_ = false;
      context->result.status = ArcImportStatus::kRuntimeFailed;
      std::move(context->callback).Run(std::move(context->result));
      return;
    }
  }

  if (same_key_replay) {
    if (context->runtime_plan.splits.empty()) {
      operation_in_progress_ = false;
      context->result.status = ArcImportStatus::kNoChanges;
      std::move(context->callback).Run(std::move(context->result));
      return;
    }
    const ArcSplitVerification verification = VerifyArcSplitRuntime(
        context->browser.get(), session_bridge_, context->runtime_plan);
    if (verification == ArcSplitVerification::kExact) {
      BeginNativeSessionReceipt(std::move(context));
      return;
    }
    if (verification != ArcSplitVerification::kRepairableMissing) {
      operation_in_progress_ = false;
      context->result.status = verification == ArcSplitVerification::kConflict
                                   ? ArcImportStatus::kConflict
                                   : ArcImportStatus::kRuntimeFailed;
      std::move(context->callback).Run(std::move(context->result));
      return;
    }
  }

  if (!context->tree_changed && context->runtime_plan.splits.empty()) {
    session_bridge_->FlushPersistenceForBackup(
        base::BindOnce(&ArcImportService::OnCommittedPersistenceFlushed,
                       weak_factory_.GetWeakPtr(), std::move(context)));
    return;
  }
  session_bridge_->FlushPersistenceForBackup(
      base::BindOnce(&ArcImportService::OnPersistenceFlushedBeforeBackup,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnPersistenceFlushedBeforeBackup(
    std::unique_ptr<CommitContext> context,
    bool persistence_flushed) {
  if (!persistence_flushed || !profile_) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kBackupError;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&CreateArcImportBackup, profile_->GetPath(),
                     context->selected_source, context->snapshot_hash),
      base::BindOnce(&ArcImportService::OnBackupComplete,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnBackupComplete(std::unique_ptr<CommitContext> context,
                                        ArcImportBackupResult backup) {
  if (backup.status != ArcImportStatus::kOk || !profile_ ||
      backup.backup_identifier.empty() || backup.manifest_sha256.empty()) {
    operation_in_progress_ = false;
    context->result.status = backup.status;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  context->prepared.transaction_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  context->prepared.snapshot_hash = context->snapshot_hash;
  context->prepared.selection_fingerprint = context->selection_fingerprint;
  context->prepared.idempotency_key = context->idempotency_key;
  context->prepared.backup_identifier = std::move(backup.backup_identifier);
  context->prepared.manifest_sha256 = std::move(backup.manifest_sha256);
  context->prepared.affected_ids = ArcImportAffectedIds(context->runtime_plan);
  context->prepared.runtime_mutation_planned =
      !context->runtime_plan.splits.empty();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&VerifyAndLoadArcImportBackup, profile_->GetPath(),
                     context->prepared.backup_identifier,
                     context->prepared.manifest_sha256,
                     context->prepared.snapshot_hash),
      base::BindOnce(&ArcImportService::OnBackupVerified,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnBackupVerified(
    std::unique_ptr<CommitContext> context,
    ArcImportBackupRecoveryResult recovery) {
  tab_tree::TabTreeSnapshot live;
  if (recovery.status != ArcImportStatus::kOk || !recovery.previous_tree ||
      !session_bridge_ || !session_bridge_->is_ready() ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != context->previous_tree ||
      *recovery.previous_tree != context->previous_tree || !profile_ ||
      (context->tree_changed && !context->merged_tree)) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kBackupError;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  context->previous_tree = std::move(*recovery.previous_tree);
  tab_tree::TabTreeSnapshot expected_tree =
      context->tree_changed ? *context->merged_tree : context->previous_tree;
  auto fingerprint_task = base::BindOnce(
      [](tab_tree::TabTreeSnapshot previous,
         tab_tree::TabTreeSnapshot expected) {
        return std::array<std::string, 2>{
            ComputeArcImportTreeFingerprint(previous),
            ComputeArcImportTreeFingerprint(expected)};
      },
      context->previous_tree, std::move(expected_tree));
  auto fingerprint_reply = base::BindOnce(
      &ArcImportService::OnPreparedTreeFingerprintsComputed,
      weak_factory_.GetWeakPtr(), std::move(context), std::move(live));
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(fingerprint_task), std::move(fingerprint_reply));
}

void ArcImportService::OnPreparedTreeFingerprintsComputed(
    std::unique_ptr<CommitContext> context,
    tab_tree::TabTreeSnapshot fingerprint_start_tree,
    std::array<std::string, 2> fingerprints) {
  // Bind the prepared record only if no local/sync mutation crossed the
  // fingerprint worker boundary.
  tab_tree::TabTreeSnapshot live;
  if (!profile_ || !session_bridge_ || !session_bridge_->is_ready() ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != fingerprint_start_tree || live != context->previous_tree ||
      !IsArcImportTreeFingerprint(fingerprints[0]) ||
      !IsArcImportTreeFingerprint(fingerprints[1])) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kStalePreview;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  context->prepared.previous_tree_sha256 = std::move(fingerprints[0]);
  context->prepared.expected_tree_sha256 = std::move(fingerprints[1]);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                     context->prepared),
      base::BindOnce(&ArcImportService::OnPreparedJournalWritten,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnPreparedJournalWritten(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  if (!journal_written || !session_bridge_ || !session_bridge_->is_ready()) {
    AbortPreparedAndFinish(std::move(context), ArcImportStatus::kJournalError);
    return;
  }
  tab_tree::TabTreeSnapshot live;
  if (!session_bridge_->ExportTabTreeSnapshot(&live)) {
    AbortPreparedAndFinish(std::move(context),
                           ArcImportStatus::kTransactionFailed);
    return;
  }
  if (live != context->previous_tree) {
    // Preserve a newer tree mutation crossed by the journal worker boundary.
    AbortPreparedAndFinish(std::move(context), ArcImportStatus::kStalePreview);
    return;
  }
  if (context->tree_changed &&
      (!context->merged_tree ||
       session_bridge_->ApplySyncedTabTreeSnapshot(*context->merged_tree) !=
           tab_tree::TabTreeStore::Result::kOk)) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kTransactionFailed);
    return;
  }
  session_bridge_->FlushPersistenceForBackup(
      base::BindOnce(&ArcImportService::OnPreparedTreeFlushed,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::AbortPreparedAndFinish(
    std::unique_ptr<CommitContext> context,
    ArcImportStatus failure_status) {
  if (!profile_) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kRecoveryRequired;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RestoreArcImportJournalAfterRollback, profile_->GetPath(),
                     context->prepared.previous_committed),
      base::BindOnce(&ArcImportService::FinishRollbackJournal,
                     weak_factory_.GetWeakPtr(), std::move(context),
                     failure_status));
}

void ArcImportService::RollbackAndFinish(std::unique_ptr<CommitContext> context,
                                         ArcImportStatus failure_status) {
  if (context->runtime_started) {
    // Native split state cannot be compensated by restoring only the Ahoi
    // tree. Preserve both live stores and retain a durable manual gate.
    MarkManualRecoveryAndFinish(std::move(context));
    return;
  }
  tab_tree::TabTreeSnapshot live;
  if (!session_bridge_ || !session_bridge_->ExportTabTreeSnapshot(&live)) {
    MarkManualRecoveryAndFinish(std::move(context));
    return;
  }
  const bool importer_owned_tree =
      live == context->previous_tree ||
      (context->tree_changed && context->merged_tree &&
       live == *context->merged_tree);
  if (!importer_owned_tree ||
      (live != context->previous_tree &&
       session_bridge_->ApplySyncedTabTreeSnapshot(context->previous_tree) !=
           tab_tree::TabTreeStore::Result::kOk)) {
    MarkManualRecoveryAndFinish(std::move(context));
    return;
  }
  CloseArcImportRuntimeTabs(std::move(context->opened_tabs));
  session_bridge_->FlushPersistenceForBackup(base::BindOnce(
      &ArcImportService::FinishRollback, weak_factory_.GetWeakPtr(),
      std::move(context), failure_status));
}

void ArcImportService::MarkManualRecoveryAndFinish(
    std::unique_ptr<CommitContext> context) {
  if (!context->runtime_started) {
    CloseArcImportRuntimeTabs(std::move(context->opened_tabs));
  }
  context->prepared.phase = ArcImportPreparedPhase::kManualRecoveryRequired;
  if (!profile_) {
    FinishRecoveryRequired(std::move(context), false);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteArcImportPreparedJournal, profile_->GetPath(),
                     context->prepared),
      base::BindOnce(&ArcImportService::FinishRecoveryRequired,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::FinishRecoveryRequired(
    std::unique_ptr<CommitContext> context,
    bool /*journal_written*/) {
  operation_in_progress_ = false;
  context->result.status = ArcImportStatus::kRecoveryRequired;
  std::move(context->callback).Run(std::move(context->result));
}

void ArcImportService::FinishRollback(std::unique_ptr<CommitContext> context,
                                      ArcImportStatus failure_status,
                                      bool persistence_flushed) {
  tab_tree::TabTreeSnapshot restored;
  if (!persistence_flushed || !session_bridge_ ||
      !session_bridge_->ExportTabTreeSnapshot(&restored) || !profile_) {
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kRecoveryRequired;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  if (restored != context->previous_tree) {
    MarkManualRecoveryAndFinish(std::move(context));
    return;
  }
  if (context->runtime_started) {
    // Existing native split regrouping cannot be exactly undone here.
    operation_in_progress_ = false;
    context->result.status = ArcImportStatus::kRecoveryRequired;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RestoreArcImportJournalAfterRollback, profile_->GetPath(),
                     context->prepared.previous_committed),
      base::BindOnce(&ArcImportService::FinishRollbackJournal,
                     weak_factory_.GetWeakPtr(), std::move(context),
                     failure_status));
}

void ArcImportService::FinishRollbackJournal(
    std::unique_ptr<CommitContext> context,
    ArcImportStatus failure_status,
    bool journal_restored) {
  operation_in_progress_ = false;
  context->result.status =
      journal_restored ? failure_status : ArcImportStatus::kRecoveryRequired;
  std::move(context->callback).Run(std::move(context->result));
}

}  // namespace ahoi::importer::arc
