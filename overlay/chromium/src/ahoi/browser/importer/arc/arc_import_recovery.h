// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

struct ArcImportBackupRecoveryResult {
  ArcImportStatus status = ArcImportStatus::kBackupError;
  std::optional<tab_tree::TabTreeSnapshot> previous_tree;
};

// Verifies the owner-only backup directory, the complete manifest and every
// listed payload before opening a disposable copy of the backed-up TabTree
// database. The immutable backup itself is never opened writable.
ArcImportBackupRecoveryResult VerifyAndLoadArcImportBackup(
    const base::FilePath& profile_path,
    const std::string& backup_identifier,
    const std::string& expected_manifest_sha256,
    const std::string& expected_snapshot_sha256);

struct ArcHistoryBackupCopy {
  // SHA-256 of the Arc profile directory name, as used by the backup.
  std::string profile_key;
  // Private, disposable copy of the backed-up `History` database (plus its
  // WAL when one was backed up) below the caller's destination directory.
  base::FilePath database;
};

struct ArcHistoryBackupCopyResult {
  ArcImportStatus status = ArcImportStatus::kBackupError;
  // Privacy-safe content key over the backed-up History databases and WALs of
  // all selected profiles. Equal backups yield equal keys.
  std::string history_key;
  // Sorted by profile key; empty when no selected profile had a History file.
  std::vector<ArcHistoryBackupCopy> copies;
};

// Verifies the backup exactly like VerifyAndLoadArcImportBackup() and copies
// every backed-up Arc `History` database with its WAL into `destination`, an
// existing owner-only directory. Neither Arc nor the immutable backup is ever
// opened writable; SQLite may freely checkpoint the disposable copies.
ArcHistoryBackupCopyResult CopyArcHistoryFromBackup(
    const base::FilePath& profile_path,
    const std::string& backup_identifier,
    const std::string& expected_manifest_sha256,
    const std::string& expected_snapshot_sha256,
    const base::FilePath& destination);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_
