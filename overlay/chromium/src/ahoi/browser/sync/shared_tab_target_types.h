// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SHARED_TAB_TARGET_TYPES_H_
#define AHOI_BROWSER_SYNC_SHARED_TAB_TARGET_TYPES_H_

#include <optional>
#include <string>

namespace ahoi::sync {

// Shared value types only. Native target classification and navigation remain
// with the native owner; the wire validator enforces the target contract.
enum class SharedTabTargetKind {
  kWeb = 0,
  kNewTab = 1,
  kLocalOnly = 2,
};

// This is one atomic URL field group. Local-only targets contain an empty URL
// and an allowed scheme class, never the original native path or code.
struct SharedTabTarget {
  SharedTabTargetKind kind = SharedTabTargetKind::kWeb;
  std::string url;
  std::optional<std::string> local_scheme;

  friend bool operator==(const SharedTabTarget&,
                         const SharedTabTarget&) = default;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SHARED_TAB_TARGET_TYPES_H_
