// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_IMPORT_RUNNER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_IMPORT_RUNNER_H_

#include <cstddef>
#include <optional>
#include <string>

#include "ahoi/browser/importer/arc/arc_history_journal.h"
#include "ahoi/browser/importer/arc/arc_history_writer.h"
#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"

namespace history {
class HistoryService;
}

namespace ahoi::importer::arc {

struct ArcImportBackupResult;
// Worker-side read and journal result; defined in the .cc file.
struct ArcHistoryPreparation;

// User-visible outcome of the separately selectable history category.
struct ArcHistoryImportResult {
  bool selected = false;
  // kOk: pages were added. kNoChanges: nothing new (replay, no History file,
  // or every page already present). kRecoveryRequired: pages may have been
  // written but the journal could not confirm them; the next discovery
  // completes the import. Any other status left Ahoi history unchanged.
  ArcImportStatus status = ArcImportStatus::kNoChanges;
  size_t added_pages = 0;
  size_t deduplicated_pages = 0;
  size_t expired_pages = 0;
  // Hidden, unsafe (non-HTTP(S), credentials, oversized), invalid-time and
  // over-limit source rows.
  size_t excluded_pages = 0;
};

struct ArcHistoryImportRequest {
  base::FilePath profile_path;
  // The selected Arc profiles and the sidebar snapshot token the commit was
  // validated against. Used only when a fresh backup is needed.
  ArcSource selected_source;
  std::string snapshot_token;
  // The verified backup created by this commit's sidebar transaction, if
  // any. When empty the runner creates a new backup first. History is read
  // only from such a backup, never from Arc's live files.
  std::string backup_identifier;
  std::string manifest_sha256;
};

// Owns the history phase of an Arc import: backup, verified read, prepared
// journal, the history database write (arc_history_writer.h) and the final
// journal state. Profile-scoped and owned by ArcImportService.
class ArcHistoryImportRunner {
 public:
  using ImportCallback = base::OnceCallback<void(ArcHistoryImportResult)>;
  using RecoveryCallback = base::OnceCallback<void(ArcImportStatus)>;

  explicit ArcHistoryImportRunner(history::HistoryService* history_service);
  ArcHistoryImportRunner(const ArcHistoryImportRunner&) = delete;
  ArcHistoryImportRunner& operator=(const ArcHistoryImportRunner&) = delete;
  ~ArcHistoryImportRunner();

  void Import(ArcHistoryImportRequest request, ImportCallback callback);

  // Completes a prepared history import found at discovery by re-reading the
  // same verified backup; the pair-idempotent writer adds only what the
  // interrupted run did not commit. If the backup can no longer be verified,
  // the prepared marker is released: every page already written is a
  // complete Arc visit, and a later import skips it as a duplicate.
  void Recover(const base::FilePath& profile_path,
               ArcHistoryPreparedState prepared,
               RecoveryCallback callback);

 private:
  void OnBackupReady(ArcHistoryImportRequest request,
                     ImportCallback callback,
                     ArcImportBackupResult backup);
  void Prepare(ArcHistoryImportRequest request, ImportCallback callback);
  void OnPrepared(base::FilePath profile_path,
                  ImportCallback callback,
                  ArcHistoryPreparation preparation);
  void OnWritten(base::FilePath profile_path,
                 std::string history_key,
                 std::optional<ArcHistoryCommittedState> previous,
                 ArcHistoryImportResult result,
                 ImportCallback callback,
                 ArcHistoryWriteOutcome outcome);
  void OnRecoveryRead(base::FilePath profile_path,
                      ArcHistoryPreparedState prepared,
                      RecoveryCallback callback,
                      ArcHistoryPreparation preparation);
  void OnRecoveryWritten(base::FilePath profile_path,
                         std::string history_key,
                         size_t excluded_pages,
                         RecoveryCallback callback,
                         ArcHistoryWriteOutcome outcome);

  raw_ptr<history::HistoryService> history_service_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<ArcHistoryImportRunner> weak_factory_{this};
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_IMPORT_RUNNER_H_
