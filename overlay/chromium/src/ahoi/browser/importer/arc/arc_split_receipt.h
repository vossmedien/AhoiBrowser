// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RECEIPT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RECEIPT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "components/split_tabs/split_tab_visual_data.h"

class SessionID;

namespace sessions {
struct SessionWindow;
}

namespace ahoi::importer::arc {

// kRepairableMissing is deliberately limited to the live runtime verifier. A
// durable SessionWindow readback must be exact before it can produce a receipt.
enum class ArcSplitVerification {
  kExact = 0,
  kRepairableMissing,
  kConflict,
  kUnavailable,
};

// Privacy-safe classification for a failed durable-session verification. It
// deliberately contains no tab titles, URLs, source paths, or runtime IDs.
enum class ArcSplitReceiptFailure {
  kNotAttempted = 0,
  kNone,
  kInvalidStructure,
  kInvalidTargetWindowId,
  kDuplicateTargetWindow,
  kInvalidTargetWindow,
  kDuplicateMemberMetadata,
  kInvalidMember,
  kMemberOrderMismatch,
  kInvalidSplitMembership,
  kUnexpectedSplitMembership,
  kInvalidSplitVisualRecord,
  kInvalidFocusContext,
  kFocusMismatch,
};

struct ArcSplitVisualExpectation {
  split_tabs::SplitTabVisualData visual_data;
  bool approximated_four_pane_ratios = false;
};

struct ArcSplitReceipt {
  ArcSplitVerification verification = ArcSplitVerification::kUnavailable;
  ArcSplitReceiptFailure failure = ArcSplitReceiptFailure::kNotAttempted;
  size_t verified_split_count = 0;
  bool focus_verified = false;
  std::string structure_sha256;
  std::string receipt_sha256;
};

// Converts a validated Arc descriptor to the exact native visual data expected
// both in the live model and in Chromium's durable SessionWindow readback.
std::optional<ArcSplitVisualExpectation> BuildArcSplitVisualExpectation(
    const ArcSplitDescriptor& split);

// Validates only the privacy-safe split topology and its referenced tree-node
// identities. It never consumes titles, URLs, or runtime-generated split IDs.
bool IsValidArcSplitStructure(const ArcImportPlan& plan);

// Returns a domain-separated SHA-256 for the ordered split topology, visual
// data, and stable tree-node identities. Invalid input returns an empty string.
// This is suitable for binding a prepared journal before runtime mutation.
std::string ComputeArcSplitStructureFingerprint(const ArcImportPlan& plan);

// Verifies the current-session readback returned by SessionService after its
// reset/flush barrier. Tabs are identified exclusively through successfully
// decoded kTabSessionMetadataExtraDataKey payloads. The receipt never hashes
// navigation data, titles, or runtime-generated SplitTabIds.
ArcSplitReceipt VerifyArcSplitSessionWindows(
    const ArcImportPlan& plan,
    SessionID target_window_id,
    const std::vector<std::unique_ptr<sessions::SessionWindow>>& windows,
    SessionID active_window_id,
    bool require_focus);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RECEIPT_H_
