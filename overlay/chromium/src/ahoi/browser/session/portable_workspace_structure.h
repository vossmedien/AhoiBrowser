// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_STRUCTURE_H_
#define AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_STRUCTURE_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/tab_tree/portable_workspace_selection.h"

namespace ahoi::session {

struct PortableArchive {
  base::Uuid id;
  sync::SharedArchiveSnapshot snapshot;
  sync::SharedArchiveReason reason = sync::SharedArchiveReason::kManual;
  base::Time archived_at;

  bool operator==(const PortableArchive&) const = default;
};

struct PortableWorkspaceStructure {
  tab_tree::PortableWorkspaceSelection tree;
  std::vector<sync::SharedSplitMetadata> splits;
  std::vector<PortableArchive> archives;
  size_t excluded_incomplete_splits = 0;
  size_t excluded_nonportable_archives = 0;
};

// Extends the privacy-filtered normal tree selection with only complete
// logical split groups and optionally selected archives. Local runtime tokens,
// private snapshots, field clocks and original file/code URLs remain behind
// this boundary. It does not write a file or mutate either store.
std::optional<PortableWorkspaceStructure> SelectPortableWorkspaceStructure(
    const tab_tree::TabTreeSnapshot& tree,
    const WorkspaceStructureState& structure,
    const std::vector<base::Uuid>& selected_workspace_ids,
    bool include_temporary_pages,
    bool include_archives);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_STRUCTURE_H_
