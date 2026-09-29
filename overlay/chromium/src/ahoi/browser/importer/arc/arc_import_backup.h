// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"

namespace ahoi::importer::arc {

struct ArcImportBackupResult {
  ArcImportStatus status = ArcImportStatus::kBackupError;
  base::FilePath backup_directory;
  // Single path component below `Ahoi/Arc Import Backups`; safe to persist in
  // the privacy-minimal transaction journal.
  std::string backup_identifier;
  std::string manifest_sha256;
};

struct ArcImportBackupLimits {
  uint64_t max_total_bytes = 2ull * 1024 * 1024 * 1024;
  size_t max_file_count = 384;
  int64_t minimum_free_headroom_bytes = 256ll * 1024 * 1024;
  size_t max_retained_backups = 3;
};

// Creates a new write-once backup directory. The manifest contains controlled
// role names and privacy-safe root-relative source paths plus presence, source
// modification time, byte count and SHA-256--never absolute user paths,
// titles, URLs or credentials. Every payload and manifest is owner-only.
ArcImportBackupResult CreateArcImportBackup(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token);

namespace internal {

using ArcSourceUseCheck = base::RepeatingCallback<bool(const ArcSource&)>;

// Hermetic seam for backup tests. Production always supplies the fail-closed
// live Arc process/open-file check through CreateArcImportBackup().
ArcImportBackupResult CreateArcImportBackupForTesting(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token,
    ArcSourceUseCheck source_use_check);

// Pure resource-boundary seam for deterministic overflow/quota tests.
ArcImportStatus CheckArcImportBackupResources(
    const std::vector<int64_t>& present_file_sizes,
    int64_t free_disk_bytes,
    const ArcImportBackupLimits& limits,
    uint64_t* total_bytes);

// Deletes only old, fully validated importer-owned backup directories.
// Prepared-journal backup identifiers passed in |protected_identifiers| are
// never eligible, even when they exceed the normal retention count.
bool PruneArcImportBackupsForTesting(
    const base::FilePath& backup_root,
    const std::set<std::string>& protected_identifiers,
    size_t max_retained_backups);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_
