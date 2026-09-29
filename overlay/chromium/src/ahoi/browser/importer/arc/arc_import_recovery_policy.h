// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_POLICY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_POLICY_H_

#include <string_view>

#include "ahoi/browser/importer/arc/arc_import_journal.h"

namespace ahoi::importer::arc {

enum class ArcImportRecoveryTreeAction {
  kNone = 0,
  kApplyPreviousTree,
};

enum class ArcImportRecoveryCompletion {
  kRestorePreviousJournal = 0,
  kManualRecoveryRequired,
};

// Pure, fail-closed service seam. There is exactly one action that authorizes a
// tree write; every foreign, malformed, or native-runtime-uncertain state keeps
// the journal in manual recovery.
struct ArcImportRecoveryDecision {
  ArcImportRecoveryTreeAction tree_action = ArcImportRecoveryTreeAction::kNone;
  ArcImportRecoveryCompletion completion =
      ArcImportRecoveryCompletion::kManualRecoveryRequired;
};

ArcImportRecoveryDecision DecideArcImportPreparedRecovery(
    const ArcImportPreparedState& prepared,
    std::string_view current_tree_fingerprint);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_RECOVERY_POLICY_H_
