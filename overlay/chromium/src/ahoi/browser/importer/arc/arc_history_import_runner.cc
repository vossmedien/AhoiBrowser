// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_import_runner.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_history_reader.h"
#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_backend.h"

namespace ahoi::importer::arc {

struct ArcHistoryPreparation {
  ArcImportStatus status = ArcImportStatus::kBackupError;
  std::string history_key;
  std::vector<ArcHistoryEntry> entries;
  size_t excluded_pages = 0;
  size_t expired_pages = 0;
  std::optional<ArcHistoryCommittedState> previous;
};

namespace {

constexpr base::TaskTraits kWorkerTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::BLOCK_SHUTDOWN};

int ToJournalCount(size_t value) {
  return value > static_cast<size_t>(std::numeric_limits<int>::max())
             ? std::numeric_limits<int>::max()
             : static_cast<int>(value);
}

// Reads the verified backup through private, disposable copies which are
// removed before returning, on success and on failure.
ArcHistoryPreparation ReadBackupHistory(const base::FilePath& profile_path,
                                        const std::string& backup_identifier,
                                        const std::string& manifest_sha256,
                                        const std::string& snapshot_sha256) {
  ArcHistoryPreparation preparation;
  base::ScopedTempDir copies_directory;
  if (!copies_directory.CreateUniqueTempDir()) {
    preparation.status = ArcImportStatus::kIoError;
    return preparation;
  }
  const ArcHistoryBackupCopyResult copies =
      CopyArcHistoryFromBackup(profile_path, backup_identifier, manifest_sha256,
                               snapshot_sha256, copies_directory.GetPath());
  preparation.status = copies.status;
  preparation.history_key = copies.history_key;
  if (copies.status != ArcImportStatus::kOk) {
    return preparation;
  }
  const base::Time now = base::Time::Now();
  const ArcHistoryReadWindow window{
      .not_before =
          now - base::Days(history::HistoryBackend::kExpireDaysThreshold),
      .not_after = now + base::Days(1)};
  std::vector<ArcHistoryReadResult> results;
  for (const ArcHistoryBackupCopy& copy : copies.copies) {
    results.push_back(ReadArcHistoryDatabase(copy.database, window));
  }
  ArcHistoryReadResult merged = MergeArcHistoryResults(std::move(results));
  preparation.status = merged.status;
  if (merged.status != ArcImportStatus::kOk) {
    return preparation;
  }
  preparation.entries = std::move(merged.entries);
  preparation.expired_pages = merged.stats.expired_rows;
  preparation.excluded_pages =
      merged.stats.hidden_rows + merged.stats.unsafe_urls +
      merged.stats.invalid_times + merged.stats.over_limit_rows;
  return preparation;
}

ArcHistoryPreparation PrepareHistoryImport(const base::FilePath& profile_path,
                                           const std::string& backup_identifier,
                                           const std::string& manifest_sha256,
                                           const std::string& snapshot_sha256) {
  const ArcHistoryJournalReadResult journal =
      ReadArcHistoryJournal(profile_path);
  if (journal.status != ArcImportStatus::kOk || journal.prepared) {
    return {.status = journal.status != ArcImportStatus::kOk
                          ? journal.status
                          : ArcImportStatus::kRecoveryRequired};
  }
  ArcHistoryPreparation preparation = ReadBackupHistory(
      profile_path, backup_identifier, manifest_sha256, snapshot_sha256);
  preparation.previous = journal.committed;
  if (preparation.status != ArcImportStatus::kOk) {
    preparation.entries.clear();
    return preparation;
  }
  if (preparation.entries.empty() ||
      (journal.committed &&
       journal.committed->history_key == preparation.history_key)) {
    // A replay of the committed input, or nothing importable: no journal
    // change and no history database access at all.
    preparation.status = ArcImportStatus::kNoChanges;
    preparation.entries.clear();
    return preparation;
  }
  if (!WriteArcHistoryPreparedJournal(
          profile_path, {.history_key = preparation.history_key,
                         .backup_identifier = backup_identifier,
                         .manifest_sha256 = manifest_sha256,
                         .snapshot_sha256 = snapshot_sha256,
                         .previous_committed = journal.committed})) {
    preparation.status = ArcImportStatus::kJournalError;
    preparation.entries.clear();
  }
  return preparation;
}

ArcHistoryImportMetrics MetricsFor(const ArcHistoryWriteOutcome& outcome,
                                   size_t excluded_pages,
                                   size_t expired_pages) {
  return {
      .added_pages = ToJournalCount(outcome.added_pages),
      .created_urls = ToJournalCount(outcome.created_urls),
      .deduplicated_pages = ToJournalCount(outcome.deduplicated_pages),
      .expired_pages = ToJournalCount(expired_pages + outcome.expired_pages),
      .excluded_pages = ToJournalCount(excluded_pages)};
}

}  // namespace

ArcHistoryImportRunner::ArcHistoryImportRunner(
    history::HistoryService* history_service)
    : history_service_(history_service) {}

ArcHistoryImportRunner::~ArcHistoryImportRunner() {
  weak_factory_.InvalidateWeakPtrs();
  tracker_.TryCancelAll();
  history_service_ = nullptr;
}

void ArcHistoryImportRunner::Import(ArcHistoryImportRequest request,
                                    ImportCallback callback) {
  if (!history_service_ || request.profile_path.empty()) {
    std::move(callback).Run(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  if (!request.backup_identifier.empty() && !request.manifest_sha256.empty()) {
    Prepare(std::move(request), std::move(callback));
    return;
  }
  const base::FilePath profile_path = request.profile_path;
  const ArcSource source = request.selected_source;
  const std::string token = request.snapshot_token;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kWorkerTraits,
      base::BindOnce(&CreateArcImportBackup, profile_path, source, token),
      base::BindOnce(&ArcHistoryImportRunner::OnBackupReady,
                     weak_factory_.GetWeakPtr(), std::move(request),
                     std::move(callback)));
}

void ArcHistoryImportRunner::OnBackupReady(ArcHistoryImportRequest request,
                                           ImportCallback callback,
                                           ArcImportBackupResult backup) {
  if (backup.status != ArcImportStatus::kOk ||
      backup.backup_identifier.empty() || backup.manifest_sha256.empty()) {
    std::move(callback).Run({.selected = true,
                             .status = backup.status == ArcImportStatus::kOk
                                           ? ArcImportStatus::kBackupError
                                           : backup.status});
    return;
  }
  request.backup_identifier = std::move(backup.backup_identifier);
  request.manifest_sha256 = std::move(backup.manifest_sha256);
  Prepare(std::move(request), std::move(callback));
}

void ArcHistoryImportRunner::Prepare(ArcHistoryImportRequest request,
                                     ImportCallback callback) {
  const base::FilePath profile_path = request.profile_path;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kWorkerTraits,
      base::BindOnce(&PrepareHistoryImport, request.profile_path,
                     request.backup_identifier, request.manifest_sha256,
                     request.snapshot_token),
      base::BindOnce(&ArcHistoryImportRunner::OnPrepared,
                     weak_factory_.GetWeakPtr(), profile_path,
                     std::move(callback)));
}

void ArcHistoryImportRunner::OnPrepared(base::FilePath profile_path,
                                        ImportCallback callback,
                                        ArcHistoryPreparation preparation) {
  ArcHistoryImportResult result{.selected = true,
                                .status = preparation.status,
                                .expired_pages = preparation.expired_pages,
                                .excluded_pages = preparation.excluded_pages};
  if (preparation.status != ArcImportStatus::kOk) {
    std::move(callback).Run(result);
    return;
  }
  ScheduleArcHistoryWrite(
      history_service_, std::move(preparation.entries), &tracker_,
      base::BindOnce(
          &ArcHistoryImportRunner::OnWritten, weak_factory_.GetWeakPtr(),
          std::move(profile_path), std::move(preparation.history_key),
          std::move(preparation.previous), result, std::move(callback)));
}

void ArcHistoryImportRunner::OnWritten(
    base::FilePath profile_path,
    std::string history_key,
    std::optional<ArcHistoryCommittedState> previous,
    ArcHistoryImportResult result,
    ImportCallback callback,
    ArcHistoryWriteOutcome outcome) {
  const size_t read_expired_pages = result.expired_pages;
  result.status = outcome.status;
  result.added_pages = outcome.added_pages;
  result.deduplicated_pages = outcome.deduplicated_pages;
  result.expired_pages += outcome.expired_pages;
  if (outcome.status == ArcImportStatus::kRecoveryRequired) {
    // The prepared journal stays; the next discovery completes the import.
    std::move(callback).Run(result);
    return;
  }
  const bool committed = outcome.status == ArcImportStatus::kOk ||
                         outcome.status == ArcImportStatus::kNoChanges;
  const ArcHistoryCommittedState next{
      .history_key = std::move(history_key),
      .metrics =
          MetricsFor(outcome, result.excluded_pages, read_expired_pages)};
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kWorkerTraits,
      base::BindOnce(
          [](base::FilePath path, bool committed, ArcHistoryCommittedState next,
             std::optional<ArcHistoryCommittedState> previous) {
            // An exact rollback reinstates the prior journal state.
            return committed ? WriteArcHistoryCommittedJournal(path, next)
                             : RestoreArcHistoryJournal(path, previous);
          },
          std::move(profile_path), committed, next, std::move(previous)),
      base::BindOnce(
          [](ArcHistoryImportResult result, ImportCallback callback,
             bool journal_written) {
            if (!journal_written) {
              // The prepared marker remains and makes the next discovery
              // finish the import from the same verified backup.
              result.status = ArcImportStatus::kRecoveryRequired;
            }
            std::move(callback).Run(result);
          },
          result, std::move(callback)));
}

void ArcHistoryImportRunner::Recover(const base::FilePath& profile_path,
                                     ArcHistoryPreparedState prepared,
                                     RecoveryCallback callback) {
  if (!history_service_) {
    std::move(callback).Run(ArcImportStatus::kRecoveryRequired);
    return;
  }
  const std::string backup_identifier = prepared.backup_identifier;
  const std::string manifest_sha256 = prepared.manifest_sha256;
  const std::string snapshot_sha256 = prepared.snapshot_sha256;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kWorkerTraits,
      base::BindOnce(&ReadBackupHistory, profile_path, backup_identifier,
                     manifest_sha256, snapshot_sha256),
      base::BindOnce(&ArcHistoryImportRunner::OnRecoveryRead,
                     weak_factory_.GetWeakPtr(), profile_path,
                     std::move(prepared), std::move(callback)));
}

void ArcHistoryImportRunner::OnRecoveryRead(base::FilePath profile_path,
                                            ArcHistoryPreparedState prepared,
                                            RecoveryCallback callback,
                                            ArcHistoryPreparation preparation) {
  if (preparation.status != ArcImportStatus::kOk ||
      preparation.history_key != prepared.history_key) {
    // The exact input is gone. Release the marker instead of guessing; see
    // Recover() for why this cannot duplicate or corrupt history.
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kWorkerTraits,
        base::BindOnce(&RestoreArcHistoryJournal, std::move(profile_path),
                       std::move(prepared.previous_committed)),
        base::BindOnce(
            [](RecoveryCallback callback, bool restored) {
              std::move(callback).Run(restored
                                          ? ArcImportStatus::kOk
                                          : ArcImportStatus::kJournalError);
            },
            std::move(callback)));
    return;
  }
  ScheduleArcHistoryWrite(
      history_service_, std::move(preparation.entries), &tracker_,
      base::BindOnce(&ArcHistoryImportRunner::OnRecoveryWritten,
                     weak_factory_.GetWeakPtr(), std::move(profile_path),
                     std::move(preparation.history_key),
                     preparation.excluded_pages, std::move(callback)));
}

void ArcHistoryImportRunner::OnRecoveryWritten(base::FilePath profile_path,
                                               std::string history_key,
                                               size_t excluded_pages,
                                               RecoveryCallback callback,
                                               ArcHistoryWriteOutcome outcome) {
  if (outcome.status != ArcImportStatus::kOk &&
      outcome.status != ArcImportStatus::kNoChanges) {
    // Fail closed: the prepared marker stays for the next attempt.
    std::move(callback).Run(outcome.status);
    return;
  }
  const ArcHistoryCommittedState committed{
      .history_key = std::move(history_key),
      .metrics = MetricsFor(outcome, excluded_pages, 0)};
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kWorkerTraits,
      base::BindOnce(&WriteArcHistoryCommittedJournal, std::move(profile_path),
                     committed),
      base::BindOnce(
          [](RecoveryCallback callback, bool written) {
            std::move(callback).Run(written ? ArcImportStatus::kOk
                                            : ArcImportStatus::kJournalError);
          },
          std::move(callback)));
}

}  // namespace ahoi::importer::arc
