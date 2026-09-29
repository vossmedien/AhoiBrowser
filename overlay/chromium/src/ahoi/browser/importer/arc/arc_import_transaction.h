// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_H_

#include <optional>

#include "ahoi/browser/importer/arc/arc_import_types.h"

namespace ahoi::importer::arc {

enum class ArcConflictResolution {
  kRename = 0,
  kSkip,
  kMerge,
};

struct ArcImportMergeResult {
  ArcImportStatus status = ArcImportStatus::kTransactionFailed;
  std::optional<tab_tree::TabTreeSnapshot> merged_tree;
  // Carries exact add/dedup statistics while retaining every remapped folder
  // and member node required to reconstruct selected splits at runtime.
  std::optional<ArcImportPlan> applied_plan;
  size_t renamed_workspace_count = 0;
  size_t skipped_workspace_count = 0;
  size_t merged_workspace_count = 0;
  bool changed = false;
};

// Builds and validates a complete replacement snapshot without mutating the
// live profile. Existing rows are never overwritten: exact deterministic rows
// are treated as already imported, while incompatible identity collisions
// fail the whole operation. The returned snapshot is accepted by a fresh
// TabTreeStore before it can reach SessionBridge.
ArcImportMergeResult MergeArcImportPlan(
    const tab_tree::TabTreeSnapshot& current,
    const ArcImportPlan& import_plan,
    ArcConflictResolution conflict_resolution);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TRANSACTION_H_
