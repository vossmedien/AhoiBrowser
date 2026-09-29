// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_

#include <optional>
#include <string>

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

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_H_
