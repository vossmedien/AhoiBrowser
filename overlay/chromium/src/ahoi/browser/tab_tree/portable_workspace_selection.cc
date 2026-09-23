// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/tab_tree/portable_workspace_selection.h"

#include <set>
#include <utility>

namespace ahoi::tab_tree {

std::optional<PortableWorkspaceSelection> SelectPortableWorkspaceStructure(
    const TabTreeSnapshot& snapshot,
    const std::vector<base::Uuid>& selected_workspace_ids,
    bool include_temporary_pages) {
  if (selected_workspace_ids.empty()) {
    return std::nullopt;
  }

  std::set<base::Uuid> selected_ids;
  for (const base::Uuid& id : selected_workspace_ids) {
    if (!id.is_valid() || !selected_ids.insert(id).second) {
      return std::nullopt;
    }
  }

  PortableWorkspaceSelection result;
  for (const Workspace& workspace : snapshot.workspaces) {
    if (!workspace.tombstone && selected_ids.contains(workspace.id)) {
      result.workspaces.push_back({
          .id = workspace.id,
          .name = workspace.name,
          .icon = workspace.icon,
          .sort_key = workspace.sort_key,
          .accent_argb = workspace.accent_argb,
          .archive_policy = workspace.archive_policy,
      });
    }
  }
  if (result.workspaces.size() != selected_ids.size()) {
    return std::nullopt;
  }

  for (const TreeNode& node : snapshot.nodes) {
    if (node.tombstone || !selected_ids.contains(node.workspace_id)) {
      continue;
    }

    PortableWorkspaceNode exported{
        .id = node.id,
        .workspace_id = node.workspace_id,
        .parent_id = node.parent_id,
        .type = node.type,
        .title = node.type == TreeNodeType::kSavedPage
                     ? GetSharedPageTitle(node)
                     : node.title,
        .icon = node.icon,
        .accent_argb = node.accent_argb,
        .sort_key = node.sort_key,
        .is_temporary = node.is_temporary,
    };
    if (node.type == TreeNodeType::kSavedPage) {
      if (node.is_temporary && !include_temporary_pages) {
        ++result.excluded_temporary_pages;
        continue;
      }
      exported.target = GetSharedPageTarget(node);
      if (!exported.target ||
          exported.target->kind == sync::SharedTabTargetKind::kLocalOnly) {
        ++result.excluded_nonportable_pages;
        continue;
      }
      if (!node.home_url.is_empty() || node.home_target_kind ||
          node.home_local_scheme) {
        exported.home_target = GetSharedHomeTarget(node);
        if (!exported.home_target ||
            exported.home_target->kind ==
                sync::SharedTabTargetKind::kLocalOnly) {
          exported.home_target.reset();
          ++result.excluded_nonportable_home_targets;
        }
      }
    }
    result.nodes.push_back(std::move(exported));
  }
  return result;
}

}  // namespace ahoi::tab_tree
