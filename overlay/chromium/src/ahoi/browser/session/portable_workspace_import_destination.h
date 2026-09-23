// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_DESTINATION_H_
#define AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_DESTINATION_H_

#include <cstddef>
#include <string>
#include <vector>

#include "ahoi/browser/session/portable_workspace_structure.h"

namespace ahoi::session {

enum class PortableDestinationKind { kNew, kIdentical, kConflict };

struct PortableWorkspaceDestination {
  base::Uuid id;
  std::u16string name;
  PortableDestinationKind kind = PortableDestinationKind::kConflict;
};

// Read-only comparison against the exact current local tree and structure.
// Identical means the portable fields match, not that the file has been
// imported. A conflict is never silently resolved by URL or name matching.
struct PortableImportDestination {
  std::vector<PortableWorkspaceDestination> workspaces;
  size_t new_nodes = 0;
  size_t identical_nodes = 0;
  size_t conflicting_nodes = 0;
  size_t new_splits = 0;
  size_t identical_splits = 0;
  size_t conflicting_splits = 0;
  size_t new_archives = 0;
  size_t identical_archives = 0;
  size_t conflicting_archives = 0;
};

PortableImportDestination AnalyzePortableWorkspaceDestination(
    const PortableWorkspaceStructure& imported,
    const tab_tree::TabTreeSnapshot& current_tree,
    const WorkspaceStructureState& current_structure);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_IMPORT_DESTINATION_H_
