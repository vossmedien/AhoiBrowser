// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_PLAN_H_
#define AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_PLAN_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/session/portable_workspace_structure.h"
#include "base/time/time.h"

namespace ahoi::session {

// A detached, additive proposal. It is not write authority: the caller must
// compare its captured source state with the current durable profile and use
// one authorized tree+structure transaction. Replaying an identical file
// produces changed=false and cannot create duplicate logical IDs.
struct PortableWorkspaceImportPlan {
  tab_tree::TabTreeSnapshot tree;
  WorkspaceStructureState structure;
  std::string encoded_structure;
  bool changed = false;
};

// Explicit all-or-nothing selection of complete Workspaces from one decoded
// file. Dependent pages/splits/archives follow only their selected workspace;
// a partial subtree or unknown/duplicate selection is rejected.
std::optional<PortableWorkspaceStructure> SelectPortableWorkspaceImport(
    const PortableWorkspaceStructure& imported,
    const std::vector<base::Uuid>& selected_workspace_ids);

// Only conflict-free additions are currently planned. A changed existing ID,
// even at the same URL, is never overwritten or silently reidentified. The
// UI must obtain an explicit conflict decision before expanding that policy.
std::optional<PortableWorkspaceImportPlan> PreparePortableWorkspaceImport(
    const PortableWorkspaceStructure& imported,
    const tab_tree::TabTreeSnapshot& current_tree,
    const WorkspaceStructureState& current_structure,
    const base::Uuid& local_device_id,
    base::Time now);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_PLAN_H_
