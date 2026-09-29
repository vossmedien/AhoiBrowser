// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SHARED_TAB_SYNC_TYPES_H_
#define AHOI_BROWSER_SYNC_SHARED_TAB_SYNC_TYPES_H_

#include <optional>
#include <vector>

#include "base/uuid.h"

namespace ahoi::sync {

// Native support is an explicit implementation claim, not implied by format 3.
struct SharedTabNativeSupport {
  bool projection = false;
  bool capture = false;

  friend bool operator==(const SharedTabNativeSupport&,
                         const SharedTabNativeSupport&) = default;
};

enum class SharedTabSyncIssue {
  kDisabled,
  kNativeNotReady,
  kBootstrapPending,
  kUnsupportedFormat,
  kRecoveryPending,
  kCaptureDeferred,
  kInvalidCapture,
  kStoreError,
  kNone,
};

// Derived cross-sequence state for native consumers. These booleans explain
// readiness; they are not authorization tokens for a queued mutation.
struct SharedTabSyncState {
  bool projection_ready = false;
  bool write_allowed = false;
  SharedTabSyncIssue issue = SharedTabSyncIssue::kDisabled;
  // Producers publish sorted, unique IDs from the same immutable snapshot.
  std::vector<base::Uuid> blocking_devices;

  friend bool operator==(const SharedTabSyncState&,
                         const SharedTabSyncState&) = default;
};

// Derived presentation only. Neither ID becomes a stored/wire creator field.
// Missing, synthetic or unknown-device provenance leaves the relevant ID empty.
struct SharedTabProvenance {
  std::optional<base::Uuid> creation_device;
  std::optional<base::Uuid> saved_device;

  friend bool operator==(const SharedTabProvenance&,
                         const SharedTabProvenance&) = default;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SHARED_TAB_SYNC_TYPES_H_
