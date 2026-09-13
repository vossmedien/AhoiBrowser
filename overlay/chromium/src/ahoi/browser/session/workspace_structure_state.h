// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AHOI_BROWSER_SESSION_WORKSPACE_STRUCTURE_STATE_H_
#define AHOI_BROWSER_SESSION_WORKSPACE_STRUCTURE_STATE_H_

#include <map>
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::session {
struct WorkspaceStructureEntry {
  sync::SyncRecord record;
  std::string baseline;
  std::string pending;
  std::string pending_expected;
  std::string native_split_token;
  std::optional<sync::SharedSplitMetadata> observed_split;
  std::vector<tab_tree::TreeNode> private_nodes;
  bool archived_locally = false;
  bool restore_pending = false;
  bool operator==(const WorkspaceStructureEntry&) const = default;
};
struct WorkspaceStructureState {
  std::map<base::Uuid, WorkspaceStructureEntry> entries;
  sync::HlcStamp clock;
};
std::optional<std::string> EncodeWorkspaceStructureState(
    const WorkspaceStructureState& state);
std::optional<WorkspaceStructureState> DecodeWorkspaceStructureState(
    std::string_view value);
}  // namespace ahoi::session
#endif
