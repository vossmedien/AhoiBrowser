// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_recovery_policy.h"

#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"

namespace ahoi::importer::arc {

ArcImportRecoveryDecision DecideArcImportPreparedRecovery(
    const ArcImportPreparedState& prepared,
    std::string_view current_tree_fingerprint) {
  ArcImportRecoveryDecision decision;
  if (!IsArcImportTreeFingerprint(prepared.previous_tree_sha256) ||
      !IsArcImportTreeFingerprint(prepared.expected_tree_sha256) ||
      !IsArcImportTreeFingerprint(current_tree_fingerprint)) {
    return decision;
  }

  if (prepared.phase == ArcImportPreparedPhase::kRuntimePersisted) {
    // The journal persists hashes and the intended committed marker, but not
    // the complete runtime plan required to obtain a fresh Current Session
    // receipt after restart. Never turn a stale receipt into a success claim.
    return decision;
  }
  if (prepared.phase != ArcImportPreparedPhase::kTreeOnly &&
      prepared.phase != ArcImportPreparedPhase::kRuntimeMayHaveStarted) {
    return decision;
  }
  const bool native_runtime_uncertain =
      prepared.phase == ArcImportPreparedPhase::kRuntimeMayHaveStarted;
  // Once native tabs or split membership may have changed, the tree is no
  // longer an independently recoverable authority. Replacing it without an
  // equally exact native-session rollback would manufacture a cross-store
  // state that never existed. Keep both stores untouched for explicit manual
  // recovery, even when the tree alone still matches a known fingerprint.
  if (native_runtime_uncertain) {
    return decision;
  }
  if (current_tree_fingerprint == prepared.previous_tree_sha256) {
    decision.completion = ArcImportRecoveryCompletion::kRestorePreviousJournal;
    return decision;
  }
  if (current_tree_fingerprint != prepared.expected_tree_sha256) {
    return decision;
  }

  decision.tree_action = ArcImportRecoveryTreeAction::kApplyPreviousTree;
  decision.completion = ArcImportRecoveryCompletion::kRestorePreviousJournal;
  return decision;
}

}  // namespace ahoi::importer::arc
