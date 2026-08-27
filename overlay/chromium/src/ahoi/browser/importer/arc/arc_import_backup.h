// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_

#include <string>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

struct ArcImportBackupResult {
  ArcImportStatus status = ArcImportStatus::kBackupError;
  base::FilePath backup_directory;
};

// Creates a new write-once backup directory. The manifest contains controlled
// role names and privacy-safe root-relative source paths plus presence, source
// modification time, byte count and SHA-256--never absolute user paths,
// titles, URLs or credentials. Every payload and manifest is owner-only.
ArcImportBackupResult CreateArcImportBackup(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_BACKUP_H_
