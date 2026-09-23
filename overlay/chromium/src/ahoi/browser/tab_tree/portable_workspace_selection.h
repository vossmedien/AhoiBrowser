// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_TAB_TREE_PORTABLE_WORKSPACE_SELECTION_H_
#define AHOI_BROWSER_TAB_TREE_PORTABLE_WORKSPACE_SELECTION_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::tab_tree {

// A local export selection, not a Sync record or an on-disk file format.
// Explicit portable types prevent future local-only fields on Workspace or
// TreeNode from silently entering a user-selected file.
struct PortableWorkspace {
  base::Uuid id;
  std::u16string name;
  std::u16string icon;
  std::string sort_key;
  std::optional<uint32_t> accent_argb;
  sync::SharedArchivePolicy archive_policy =
      sync::SharedArchivePolicy::kNever;

  bool operator==(const PortableWorkspace&) const = default;
};

struct PortableWorkspaceNode {
  base::Uuid id;
  base::Uuid workspace_id;
  std::optional<base::Uuid> parent_id;
  TreeNodeType type = TreeNodeType::kFolder;
  std::u16string title;
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
  std::string sort_key;
  bool is_temporary = false;
  std::optional<sync::SharedTabTarget> target;
  std::optional<sync::SharedTabTarget> home_target;

  bool operator==(const PortableWorkspaceNode&) const = default;
};

struct PortableWorkspaceSelection {
  std::vector<PortableWorkspace> workspaces;
  std::vector<PortableWorkspaceNode> nodes;
  size_t excluded_temporary_pages = 0;
  size_t excluded_nonportable_pages = 0;
  size_t excluded_nonportable_home_targets = 0;
};

// Fails if the requested selection is empty, unknown or duplicated. Pages
// whose destinations cannot be represented as credential-free HTTP(S) or an
// explicit new-tab target are excluded and counted, never rewritten to a
// different destination. Folder hierarchy and manual ordering are retained.
std::optional<PortableWorkspaceSelection> SelectPortableWorkspaceStructure(
    const TabTreeSnapshot& snapshot,
    const std::vector<base::Uuid>& selected_workspace_ids,
    bool include_temporary_pages);

}  // namespace ahoi::tab_tree

#endif  // AHOI_BROWSER_TAB_TREE_PORTABLE_WORKSPACE_SELECTION_H_
