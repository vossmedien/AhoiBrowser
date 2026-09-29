// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_WRITER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_WRITER_H_

#include <cstddef>
#include <vector>

#include "ahoi/browser/importer/arc/arc_history_reader.h"
#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/functional/callback.h"

namespace base {
class CancelableTaskTracker;
}

namespace history {
class HistoryBackend;
class HistoryDatabase;
class HistoryService;
}  // namespace history

namespace ahoi::importer::arc {

struct ArcHistoryWriteOutcome {
  // kOk: pages were added and committed. kNoChanges: every page was already
  // present (or expired). kTransactionFailed: nothing was written, or the
  // written pages were removed again exactly. kRecoveryRequired: the removal
  // could not be verified; the prepared journal stays and the next discovery
  // completes the import deterministically.
  ArcImportStatus status = ArcImportStatus::kTransactionFailed;
  size_t added_pages = 0;
  // Pages whose URL did not exist in Ahoi history before this import.
  size_t created_urls = 0;
  // Pages that already had a visit at exactly their Arc visit time.
  size_t deduplicated_pages = 0;
  // Pages older than Chromium's history retention window.
  size_t expired_pages = 0;

  bool operator==(const ArcHistoryWriteOutcome&) const = default;
};

struct ArcHistoryWriteTestHooks {
  // Treats the written pages as unverified so the exact rollback runs.
  bool fail_after_write = false;
};

// Runs on the history database sequence, inside one HistoryDBTask run, so no
// other history work interleaves between check, write, verification and
// rollback. The batch is bracketed by HistoryBackend::CommitForAhoiImport():
// a crash before the closing commit loses the whole batch atomically.
//
// A page is written only if its URL has no visit at exactly `last_visit`,
// which makes a repeated import a no-op. Visits carry SOURCE_ARC_IMPORTED.
// If verification fails, visits added to existing URLs are deleted row by
// row and URLs created by this batch are deleted, restoring the prior state.
ArcHistoryWriteOutcome ApplyArcHistoryEntries(
    history::HistoryBackend* backend,
    history::HistoryDatabase* database,
    const std::vector<ArcHistoryEntry>& entries,
    const ArcHistoryWriteTestHooks& hooks = {});

using ArcHistoryWriteCallback =
    base::OnceCallback<void(ArcHistoryWriteOutcome)>;

// Schedules ApplyArcHistoryEntries() as one HistoryDBTask. `callback` runs on
// the calling sequence; it is dropped if `tracker` cancels the task.
void ScheduleArcHistoryWrite(history::HistoryService* history_service,
                             std::vector<ArcHistoryEntry> entries,
                             base::CancelableTaskTracker* tracker,
                             ArcHistoryWriteCallback callback,
                             ArcHistoryWriteTestHooks hooks = {});

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_WRITER_H_
