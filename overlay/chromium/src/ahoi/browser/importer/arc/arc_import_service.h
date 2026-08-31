// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_journal.h"
#include "ahoi/browser/importer/arc/arc_import_transaction.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"

class BrowserWindowInterface;
class Profile;
class SessionID;

namespace sessions {
struct SessionWindow;
}

namespace ahoi {
class SessionBridge;
}

namespace ahoi::importer::arc {

struct ArcImportBackupRecoveryResult;
struct ArcImportBackupResult;

struct ArcImportPreview {
  ArcImportStatus status = ArcImportStatus::kNotFound;
  std::string snapshot_token;
  ArcImportStats stats;
  std::vector<std::u16string> target_workspace_names;
  std::vector<std::string> available_browser_profiles;
  size_t conflicting_workspace_count = 0;
  bool already_imported = false;
  bool arc_is_running = false;
};

// Explicit user choices carried from the mutation-free preview to Commit().
// Sidebar import is the supported category today; native split recreation can
// be independently disabled, in which case split members remain in a named
// folder. Selected profiles determine which local Arc profile artifacts are
// included in the pre-commit safety backup.
struct ArcImportSelection {
  bool import_sidebar = true;
  bool reconstruct_splits = true;
  bool backup_confirmed = false;
  bool commit_confirmed = false;
  std::vector<std::string> selected_browser_profiles;
};

struct ArcImportCommitResult {
  ArcImportStatus status = ArcImportStatus::kTransactionFailed;
  ArcImportStats stats;
  size_t renamed_workspace_count = 0;
  size_t skipped_workspace_count = 0;
  size_t merged_workspace_count = 0;
  size_t reconstructed_split_count = 0;
  size_t approximated_four_pane_ratio_count = 0;
};

using ArcImportPreviewCallback = base::OnceCallback<void(ArcImportPreview)>;
using ArcImportCommitCallback = base::OnceCallback<void(ArcImportCommitResult)>;

// Profile-scoped orchestration boundary for Arc import. Discovery, snapshot
// capture, parsing, and journal I/O run on MayBlock workers. Only Commit(),
// after an exact preview-token check and an explicit conflict policy, may
// mutate SessionBridge's authoritative tree.
class ArcImportService : public KeyedService {
 public:
  ArcImportService(Profile* profile, SessionBridge* session_bridge);
  ArcImportService(const ArcImportService&) = delete;
  ArcImportService& operator=(const ArcImportService&) = delete;
  ~ArcImportService() override;

  void Shutdown() override;

  void DiscoverAndPreview(ArcImportPreviewCallback callback);
  void Commit(std::string snapshot_token,
              ArcConflictResolution conflict_resolution,
              ArcImportSelection selection,
              BrowserWindowInterface* browser,
              ArcImportCommitCallback callback);

  bool operation_in_progress() const { return operation_in_progress_; }

 private:
  struct DiscoveryResult;
  struct CommitContext;

  static DiscoveryResult DiscoverImport(const base::FilePath& profile_path);

  void OnDiscoveryComplete(uint64_t generation,
                           ArcImportPreviewCallback callback,
                           DiscoveryResult result);
  void BeginPreparedRecovery(ArcImportPreviewCallback callback,
                             ArcImportPreparedState prepared);
  void OnRecoveryBackupLoaded(ArcImportPreviewCallback callback,
                              ArcImportPreparedState prepared,
                              tab_tree::TabTreeSnapshot recovery_start_tree,
                              ArcImportBackupRecoveryResult recovery);
  void OnRecoveryFingerprintsComputed(
      ArcImportPreviewCallback callback,
      ArcImportPreparedState prepared,
      tab_tree::TabTreeSnapshot recovery_start_tree,
      tab_tree::TabTreeSnapshot previous_tree,
      std::array<std::string, 2> fingerprints);
  void OnRecoveryPersistenceFlushed(ArcImportPreviewCallback callback,
                                    ArcImportPreparedState prepared,
                                    tab_tree::TabTreeSnapshot expected_tree,
                                    bool restore_previous_journal,
                                    bool persistence_flushed);
  void RestorePreparedRecoveryJournal(
      ArcImportPreviewCallback callback,
      std::optional<ArcImportCommittedState> previous_committed);
  void OnRecoveryJournalRestored(ArcImportPreviewCallback callback,
                                 bool journal_restored);
  void MarkPreparedRecoveryManual(ArcImportPreviewCallback callback,
                                  ArcImportPreparedState prepared);
  void FinishPreparedRecoveryManual(ArcImportPreviewCallback callback,
                                    bool journal_written);
  void OnCommitSourceValidated(std::string snapshot_token,
                               ArcConflictResolution conflict_resolution,
                               ArcImportSelection selection,
                               base::WeakPtr<BrowserWindowInterface> browser,
                               ArcImportCommitCallback callback,
                               ArcImportStatus validation_status);
  void OnPersistenceFlushedBeforeBackup(std::unique_ptr<CommitContext> context,
                                        bool persistence_flushed);
  void OnBackupComplete(std::unique_ptr<CommitContext> context,
                        ArcImportBackupResult backup);
  void OnBackupVerified(std::unique_ptr<CommitContext> context,
                        ArcImportBackupRecoveryResult recovery);
  void OnPreparedTreeFingerprintsComputed(
      std::unique_ptr<CommitContext> context,
      tab_tree::TabTreeSnapshot fingerprint_start_tree,
      std::array<std::string, 2> fingerprints);
  void OnPreparedJournalWritten(std::unique_ptr<CommitContext> context,
                                bool journal_written);
  void OnPreparedTreeFlushed(std::unique_ptr<CommitContext> context,
                             bool persistence_flushed);
  void OnRuntimePhaseWritten(std::unique_ptr<CommitContext> context,
                             bool journal_written);
  void BeginNativeSessionReceipt(std::unique_ptr<CommitContext> context);
  void OnNativeSessionReadback(
      std::unique_ptr<CommitContext> context,
      std::vector<std::unique_ptr<sessions::SessionWindow>> windows,
      SessionID active_window_id,
      bool read_error);
  void OnRuntimePersistedJournalWritten(std::unique_ptr<CommitContext> context,
                                        bool journal_written);
  void OnCommittedPersistenceFlushed(std::unique_ptr<CommitContext> context,
                                     bool persistence_flushed);
  void FinishJournalWrite(std::unique_ptr<CommitContext> context,
                          bool journal_written);
  void OnPreparedAfterCommitFailure(std::unique_ptr<CommitContext> context,
                                    bool journal_written);
  void AbortPreparedAndFinish(std::unique_ptr<CommitContext> context,
                              ArcImportStatus failure_status);
  void RollbackAndFinish(std::unique_ptr<CommitContext> context,
                         ArcImportStatus failure_status);
  void MarkManualRecoveryAndFinish(std::unique_ptr<CommitContext> context);
  void FinishRecoveryRequired(std::unique_ptr<CommitContext> context,
                              bool journal_written);
  void FinishRollback(std::unique_ptr<CommitContext> context,
                      ArcImportStatus failure_status,
                      bool persistence_flushed);
  void FinishRollbackJournal(std::unique_ptr<CommitContext> context,
                             ArcImportStatus failure_status,
                             bool journal_restored);

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<SessionBridge> session_bridge_ = nullptr;
  std::optional<ArcImportPlan> pending_plan_;
  std::optional<ArcSource> pending_source_;
  std::string pending_snapshot_token_;
  std::optional<ArcImportCommittedState> committed_journal_state_;
  uint64_t discovery_generation_ = 0;
  bool operation_in_progress_ = false;

  base::WeakPtrFactory<ArcImportService> weak_factory_{this};
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_H_
