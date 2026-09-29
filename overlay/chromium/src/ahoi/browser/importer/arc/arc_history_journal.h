// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_JOURNAL_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_JOURNAL_H_

#include <optional>
#include <string>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

// Counters of one committed history import. Never URLs or titles.
struct ArcHistoryImportMetrics {
  int added_pages = 0;
  int created_urls = 0;
  int deduplicated_pages = 0;
  int expired_pages = 0;
  int excluded_pages = 0;

  bool operator==(const ArcHistoryImportMetrics&) const = default;
};

struct ArcHistoryCommittedState {
  // Content key of the imported History databases (see
  // CopyArcHistoryFromBackup()). A replay with the same key is a no-op.
  std::string history_key;
  ArcHistoryImportMetrics metrics;

  bool operator==(const ArcHistoryCommittedState&) const = default;
};

// Written before the history database is touched. It binds the verified
// backup the pages are read from, so a restart can finish the import
// deterministically from exactly the same input.
struct ArcHistoryPreparedState {
  std::string history_key;
  std::string backup_identifier;
  std::string manifest_sha256;
  std::string snapshot_sha256;
  std::optional<ArcHistoryCommittedState> previous_committed;

  bool operator==(const ArcHistoryPreparedState&) const = default;
};

struct ArcHistoryJournalReadResult {
  ArcImportStatus status = ArcImportStatus::kOk;
  std::optional<ArcHistoryCommittedState> committed;
  std::optional<ArcHistoryPreparedState> prepared;
};

// The history journal is separate from the sidebar journal
// (arc_import_journal.h) so both transactions keep their own schema and
// recovery. It lives owner-only (0600 in a 0700 directory) below the Ahoi
// profile directory and is replaced atomically. A missing journal is an empty
// success; a linked, foreign, over-permissive, oversized or malformed journal
// fails closed with kJournalError.
ArcHistoryJournalReadResult ReadArcHistoryJournal(
    const base::FilePath& profile_path);
bool WriteArcHistoryPreparedJournal(const base::FilePath& profile_path,
                                    const ArcHistoryPreparedState& prepared);
bool WriteArcHistoryCommittedJournal(const base::FilePath& profile_path,
                                     const ArcHistoryCommittedState& committed);
// Reinstates `previous` after an exact rollback, or removes the journal when
// there was no earlier committed history import.
bool RestoreArcHistoryJournal(
    const base::FilePath& profile_path,
    const std::optional<ArcHistoryCommittedState>& previous);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_JOURNAL_H_
