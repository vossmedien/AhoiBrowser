// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SHARED_TAB_TYPES_H_
#define AHOI_BROWSER_SYNC_PROFILE_SHARED_TAB_TYPES_H_

#include <map>
#include <set>

#include "ahoi/browser/sync/profile_sync_types.h"
#include "ahoi/browser/sync/sync_authorization.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::sync {

// Local cross-sequence command. The backend owns the committed window/key
// baseline: a commit followed by a delayed/revoked UI reply must not lose it.
// Only windows explicitly included in this complete roster may be replaced.
struct SharedTabCaptureRequest {
  LocalTabCapture capture;
  std::map<std::string, std::set<std::string>> window_keys;
  SyncAuthorization authorization;
};

enum class SharedTabCaptureDisposition {
  kDeferred,
  kInvalid,
  kStoreError,
  kApplied
};

struct SharedTabCaptureResult {
  uint64_t generation = 0;
  SharedTabCaptureDisposition disposition =
      SharedTabCaptureDisposition::kDeferred;
  bool needs_sync = false;
  SharedTabSyncState readiness;
  std::optional<DeviceTabsSnapshot> snapshot;
  SyncAuthorization authorization;
};

struct SharedTabProjection {
  std::vector<WorkspaceRecord> workspaces;
  std::vector<TreeNodeRecord> tree_nodes;
  std::vector<DeviceRecord> devices;
  SharedTabSyncState readiness;
  SyncAuthorization scope;
  SyncAuthorization authorization;
};

struct NativeTreeSyncSnapshot {
  tab_tree::TabTreeSnapshot tree;
  std::string baseline_receipt;
  SyncAuthorization authorization;
};

struct PreparedSharedTabProjection {
  tab_tree::TabTreeSnapshot tree;
  std::string baseline_receipt;
  std::vector<TreeNodeRecord> tree_nodes;
  std::vector<DeviceRecord> devices;
  SharedTabSyncState readiness;
  bool needs_sync = false;
  SyncAuthorization authorization;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SHARED_TAB_TYPES_H_
