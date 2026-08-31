// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_KEY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_KEY_H_

#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_transaction.h"

namespace ahoi::importer::arc {

struct ArcImportTransactionSelection {
  bool import_sidebar = true;
  bool reconstruct_splits = true;
  ArcConflictResolution conflict_resolution = ArcConflictResolution::kRename;
  std::vector<std::string> selected_browser_profiles;
};

// Privacy-minimal stable keys: local profile labels are hashed before they
// participate, and the returned journal values never contain labels or paths.
std::string ComputeArcImportSelectionFingerprint(
    const ArcImportTransactionSelection& selection);
std::string ComputeArcImportIdempotencyKey(
    const std::string& snapshot_hash,
    const std::string& selection_fingerprint);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_KEY_H_
