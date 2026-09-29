// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_MANUAL_RECOVERY_PLAN_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_MANUAL_RECOVERY_PLAN_H_

#include <optional>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_journal.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::importer::arc {

struct ArcImportManualRecoveryPlan {
  tab_tree::TabTreeSnapshot recovery_tree;
  std::vector<base::Uuid> removed_workspaces;
};

// Plans only the explicit recovery of an unchanged import. Baseline rows not
// owned by the import retain their current values; independent new temporary
// pages may survive in pre-existing destinations. Only the comparison copy
// normalizes those rows before requiring the exact prepared fingerprint.
// Changed undo history, unknown new saved rows/workspaces and dependencies on
// removed import rows fail closed. No live tree, file or native tab is changed.
std::optional<ArcImportManualRecoveryPlan> BuildArcImportManualRecoveryPlan(
    const ArcImportPreparedState& prepared,
    const tab_tree::TabTreeSnapshot& previous,
    const tab_tree::TabTreeSnapshot& current);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_MANUAL_RECOVERY_PLAN_H_
