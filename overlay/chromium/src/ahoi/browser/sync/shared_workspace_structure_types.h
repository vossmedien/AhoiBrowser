// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SHARED_WORKSPACE_STRUCTURE_TYPES_H_
#define AHOI_BROWSER_SYNC_SHARED_WORKSPACE_STRUCTURE_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/shared_tab_target_types.h"
#include "base/uuid.h"

namespace ahoi::sync {

// Portable format-3 metadata. Native IDs, windows, focus, website sessions and
// active navigation never enter these values. These types do not enable a
// writer or claim that a native projection has been implemented.
enum class SharedSplitAxis { kHorizontal = 0, kVertical = 1 };
enum class SharedSplitArrangement {
  kLinear = 0,
  kMainStart = 1,
  kMainEnd = 2,
};
enum class SharedArchivePolicy {
  kNever = 0,
  kTwelveHours = 1,
  kTwentyFourHours = 2,
  kSevenDays = 3,
  kThirtyDays = 4,
};
enum class SharedArchiveReason { kAutomatic = 0, kManual = 1 };

// Capture rounds the finite native ratio once to millionths. Readers require
// integral JSON values in [0, 1000000], never coercing Boolean/fractional input.
struct SharedSplitRatios {
  static constexpr uint32_t kScale = 1000000;
  uint32_t primary = kScale / 2;
  uint32_t secondary = kScale / 2;
  friend bool operator==(const SharedSplitRatios&,
                         const SharedSplitRatios&) = default;
};

// Atomic topology group: 2..4 distinct normal page IDs. For two/four members
// arrangement is canonical kLinear. Four members are row-major: TL, TR, BL, BR.
// Ratios have their own atomic clock so a divider edit cannot undo membership.
struct SharedSplitTopology {
  std::vector<base::Uuid> member_ids;
  SharedSplitAxis axis = SharedSplitAxis::kHorizontal;
  SharedSplitArrangement arrangement = SharedSplitArrangement::kLinear;
  friend bool operator==(const SharedSplitTopology&,
                         const SharedSplitTopology&) = default;
};

struct SharedSplitMetadata {
  base::Uuid id;
  base::Uuid workspace_id;
  SharedSplitTopology topology;
  SharedSplitRatios ratios;
  friend bool operator==(const SharedSplitMetadata&,
                         const SharedSplitMetadata&) = default;
};

// The same native domain store retains the archive snapshot. This is bounded
// restoration data, not another browser/session/history or secret store.
struct SharedArchivePageSnapshot {
  base::Uuid tree_node_id;
  std::optional<base::Uuid> parent_id;
  std::string sort_key;
  std::string title;
  SharedTabTarget target;
  std::optional<SharedTabTarget> home_target;
  friend bool operator==(const SharedArchivePageSnapshot&,
                         const SharedArchivePageSnapshot&) = default;
};

// One ordinary page, or the complete 2..4-page group. Optional split metadata
// must name exactly the same ordered page IDs and workspace. No partial group.
struct SharedArchiveSnapshot {
  base::Uuid workspace_id;
  std::vector<SharedArchivePageSnapshot> pages;
  std::optional<SharedSplitMetadata> split;
  friend bool operator==(const SharedArchiveSnapshot&,
                         const SharedArchiveSnapshot&) = default;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SHARED_WORKSPACE_STRUCTURE_TYPES_H_
