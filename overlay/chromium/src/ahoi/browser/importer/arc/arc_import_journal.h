// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_JOURNAL_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_JOURNAL_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

struct ArcImportCommitResult;

enum class ArcImportJournalState {
  kEmpty = 0,
  kCommitted,
  kPrepared,
};

enum class ArcImportPreparedPhase {
  // The durable record exists, but native split reconstruction has definitely
  // not started yet.
  kTreeOnly = 0,
  // The record was durably advanced immediately before runtime mutation. A
  // crash in this phase cannot claim exact native-window recovery.
  kRuntimeMayHaveStarted,
  // The native Chromium current-session file was reset from live state,
  // flushed, read back, decoded, and matched against the exact runtime split
  // structure. The same live transaction may now publish its committed marker.
  // A restarted process cannot revalidate without the full runtime plan and
  // therefore remains in manual recovery.
  kRuntimePersisted,
  // A concurrent tree mutation made automatic rollback unsafe. Recovery must
  // remain blocked without replacing the user's newer authoritative tree.
  kManualRecoveryRequired,
};

struct ArcImportJournalMetrics {
  int workspaces = 0;
  int folders = 0;
  int pages = 0;
  int splits = 0;
  int degraded_splits = 0;
  int renamed_workspaces = 0;
  int skipped_workspaces = 0;
  int merged_workspaces = 0;
  int reconstructed_splits = 0;
  int approximated_four_pane_ratios = 0;

  bool operator==(const ArcImportJournalMetrics&) const = default;
};

struct ArcImportCommittedState {
  std::string snapshot_hash;
  std::string selection_fingerprint;
  std::string idempotency_key;
  ArcImportJournalMetrics metrics;

  bool operator==(const ArcImportCommittedState&) const = default;
};

struct ArcImportPreparedState {
  std::string transaction_id;
  std::string snapshot_hash;
  std::string selection_fingerprint;
  std::string idempotency_key;
  std::string backup_identifier;
  std::string manifest_sha256;
  // Privacy-safe ownership proof for crash recovery. Raw tree fields never
  // enter the journal.
  std::string previous_tree_sha256;
  std::string expected_tree_sha256;
  // Native split ownership is privacy-safe: only canonical UUIDs and hashes
  // over order/layout/ratios/focus enter the journal, never titles or URLs.
  std::string expected_native_structure_sha256;
  std::string native_receipt_sha256;
  std::vector<std::string> native_member_ids;
  std::vector<std::string> affected_ids;
  ArcImportPreparedPhase phase = ArcImportPreparedPhase::kTreeOnly;
  bool runtime_mutation_planned = false;
  std::optional<ArcImportCommittedState> previous_committed;
  // Exact new-state intent published only by the live transaction after the
  // native current-session receipt has been durably verified. It remains
  // diagnostic crash state; restart recovery never publishes it verbatim.
  std::optional<ArcImportCommittedState> intended_committed;
};

struct ArcImportJournalReadResult {
  ArcImportStatus status = ArcImportStatus::kOk;
  ArcImportJournalState state = ArcImportJournalState::kEmpty;
  std::optional<ArcImportCommittedState> committed;
  std::optional<ArcImportPreparedState> prepared;
};

// Reads and atomically replaces the owner-only import journal through a
// no-follow directory descriptor. Missing journals are a successful empty
// state; malformed, oversized, linked, foreign, or over-permissive state fails
// closed with kJournalError.
ArcImportJournalReadResult ReadArcImportJournal(
    const base::FilePath& profile_path);
ArcImportCommittedState MakeArcImportCommittedState(
    const std::string& snapshot_hash,
    const std::string& selection_fingerprint,
    const std::string& idempotency_key,
    const ArcImportCommitResult& result);
bool WriteArcImportCommittedJournal(const base::FilePath& profile_path,
                                    const ArcImportCommittedState& committed);
bool WriteArcImportCommittedJournal(const base::FilePath& profile_path,
                                    const std::string& snapshot_hash,
                                    const std::string& selection_fingerprint,
                                    const std::string& idempotency_key,
                                    const ArcImportCommitResult& result);
bool WriteArcImportPreparedJournal(const base::FilePath& profile_path,
                                   const ArcImportPreparedState& prepared);
// Called only after the exact backed-up tree has been applied and durably
// flushed. It atomically reinstates the prior committed marker or removes the
// prepared marker when this was the first import.
bool RestoreArcImportJournalAfterRollback(
    const base::FilePath& profile_path,
    const std::optional<ArcImportCommittedState>& previous_committed);

// Compatibility seam for callers/tests that do not yet carry the richer
// selection key. New service code must use WriteArcImportCommittedJournal().
bool WriteArcImportJournal(const base::FilePath& profile_path,
                           const std::string& snapshot_hash,
                           const ArcImportCommitResult& result);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_JOURNAL_H_
