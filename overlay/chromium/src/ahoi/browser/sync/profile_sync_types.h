// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/shared_tab_target_types.h"
#include "base/uuid.h"

namespace ahoi::sync {

// UI-thread descriptor for one ordinary local tab. It intentionally excludes
// cookies, credentials, page state and incognito metadata. Keeping this type
// independent of ProfileSyncService lets the blocking sync backend remain a
// UI-free layer.
struct LocalTabState {
  std::string stable_key;
  base::Uuid sync_id;
  std::optional<base::Uuid> workspace_id;
  std::string url;
  std::string title;
  bool pinned = false;
  bool active = false;
  // sync_id is the separate Presence identity. An absent page/target binding
  // represents deferred capture, not permission to publish an unlinked row.
  std::optional<base::Uuid> tree_node_id;
  std::optional<SharedTabTargetKind> target_kind;
  std::optional<std::string> local_scheme;

  friend bool operator==(const LocalTabState&, const LocalTabState&) = default;
};

enum class LocalTabCaptureStatus {
  kDeferred,
  kComplete,
};

// Service-issued generation and explicit completeness are local-only capture
// authority. An empty/deferred value does not imply deletion of any tab.
struct LocalTabCapture {
  uint64_t generation = 0;
  LocalTabCaptureStatus status = LocalTabCaptureStatus::kDeferred;
  std::vector<LocalTabState> tabs;

  friend bool operator==(const LocalTabCapture&,
                         const LocalTabCapture&) = default;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_
