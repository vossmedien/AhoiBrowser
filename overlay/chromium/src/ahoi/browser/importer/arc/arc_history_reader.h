// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_READER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

// Part of the import security boundary, like the limits in
// arc_import_types.h. Raising one requires a memory/CPU review and tests.
inline constexpr size_t kMaxArcHistoryRows = 200'000;
inline constexpr size_t kMaxArcHistoryScannedRows = 2'000'000;
inline constexpr int64_t kMaxArcHistoryDatabaseBytes = 1024ll * 1024 * 1024;
// Chromium History schemas whose `meta` version lies in this range carry the
// `urls` columns the reader uses with unchanged meaning.
inline constexpr int kMinArcHistorySchemaVersion = 40;
inline constexpr int kMaxArcHistorySchemaVersion = 99;

// One page of Arc history. Chromium's importer seam carries one entry per
// page (its latest visit plus the counters), not Arc's per-visit timeline.
struct ArcHistoryEntry {
  GURL url;
  std::u16string title;
  int visit_count = 0;
  int typed_count = 0;
  base::Time last_visit;

  bool operator==(const ArcHistoryEntry&) const = default;
};

// Counters only; never rows, titles or URLs.
struct ArcHistoryReadStats {
  size_t source_rows = 0;
  size_t accepted_rows = 0;
  size_t hidden_rows = 0;
  size_t unsafe_urls = 0;
  size_t expired_rows = 0;
  size_t invalid_times = 0;
  size_t cleared_titles = 0;
  size_t merged_duplicates = 0;
  size_t over_limit_rows = 0;

  bool operator==(const ArcHistoryReadStats&) const = default;
};

struct ArcHistoryReadResult {
  ArcImportStatus status = ArcImportStatus::kIoError;
  // Newest first, then by URL; deterministic for equal input.
  std::vector<ArcHistoryEntry> entries;
  ArcHistoryReadStats stats;
};

// The production limits are the defaults; tests pass smaller ones.
struct ArcHistoryReadLimits {
  // Accepted pages; the newest are kept, the rest count as over limit.
  size_t max_rows = kMaxArcHistoryRows;
  // Source rows of one database; more fail closed with kLimitExceeded.
  size_t max_scanned_rows = kMaxArcHistoryScannedRows;
};

struct ArcHistoryReadWindow {
  // Rows last visited before this are counted as expired and dropped; the
  // caller passes Chromium's history retention boundary.
  base::Time not_before;
  // Rows last visited after this are counted as invalid (clock skew).
  base::Time not_after;
};

// Reads a private, disposable copy of a Chromium `History` database (see
// CopyArcHistoryFromBackup()). Fails closed with kUnsupportedSchema for an
// unknown schema or a failed integrity check and with kLimitExceeded for an
// oversized source. Only visible, credential-free HTTP(S) rows inside the
// window are returned, newest first, at most `limits.max_rows`.
ArcHistoryReadResult ReadArcHistoryDatabase(
    const base::FilePath& database,
    const ArcHistoryReadWindow& window,
    const ArcHistoryReadLimits& limits = {});

// Merges per-profile results in the given order. A URL present in several
// profiles becomes one entry: the newest visit and its title win, counters
// add up. The merged list keeps the newest `limits.max_rows` entries.
ArcHistoryReadResult MergeArcHistoryResults(
    std::vector<ArcHistoryReadResult> results,
    const ArcHistoryReadLimits& limits = {});

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_READER_H_
