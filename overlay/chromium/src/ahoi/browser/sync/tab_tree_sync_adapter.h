// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_TAB_TREE_SYNC_ADAPTER_H_
#define AHOI_BROWSER_SYNC_TAB_TREE_SYNC_ADAPTER_H_

#include <optional>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::sync {

WorkspaceRecord WorkspaceToSyncRecord(
    const tab_tree::Workspace& workspace,
    SyncVersion version);
TreeNodeRecord TreeNodeToSyncRecord(const tab_tree::TreeNode& node,
                                    SyncVersion version);

// Materializes merged sync records into the regular TabTreeStore model. Local
// undo history is retained. Orphans, cross-workspace moves, deleted parents and
// cycles are deterministically cut into one per-workspace "Wiederhergestellt"
// folder. This makes offline delete-vs-move conflicts visible and recoverable
// instead of rejecting an otherwise valid provider page forever.
std::optional<tab_tree::TabTreeSnapshot> ReconcileTabTreeRecords(
    const tab_tree::TabTreeSnapshot& local_snapshot,
    const std::vector<WorkspaceRecord>& workspaces,
    const std::vector<TreeNodeRecord>& nodes);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_TAB_TREE_SYNC_ADAPTER_H_
