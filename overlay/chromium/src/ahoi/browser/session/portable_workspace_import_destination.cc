// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/portable_workspace_import_destination.h"

#include <algorithm>
#include <map>

namespace ahoi::session {
namespace {

bool SamePortableWorkspace(const tab_tree::PortableWorkspace& imported,
                           const tab_tree::Workspace& current) {
  return !current.tombstone && imported.id == current.id &&
         imported.name == current.name && imported.icon == current.icon &&
         imported.sort_key == current.sort_key &&
         imported.accent_argb == current.accent_argb &&
         imported.archive_policy == current.archive_policy;
}

bool SamePortableNode(const tab_tree::PortableWorkspaceNode& imported,
                      const tab_tree::TreeNode& current) {
  if (current.tombstone || imported.id != current.id ||
      imported.workspace_id != current.workspace_id ||
      imported.parent_id != current.parent_id ||
      imported.type != current.type || imported.title != current.title ||
      imported.icon != current.icon ||
      imported.accent_argb != current.accent_argb ||
      imported.sort_key != current.sort_key ||
      imported.is_temporary != current.is_temporary) {
    return false;
  }
  if (imported.type == tab_tree::TreeNodeType::kFolder) {
    return true;
  }
  return imported.target == tab_tree::GetSharedPageTarget(current) &&
         imported.home_target == tab_tree::GetSharedHomeTarget(current);
}

}  // namespace

PortableImportDestination AnalyzePortableWorkspaceDestination(
    const PortableWorkspaceStructure& imported,
    const tab_tree::TabTreeSnapshot& current_tree,
    const WorkspaceStructureState& current_structure) {
  PortableImportDestination result;
  std::map<base::Uuid, const tab_tree::Workspace*> workspaces;
  std::map<base::Uuid, const tab_tree::TreeNode*> nodes;
  for (const auto& workspace : current_tree.workspaces) {
    workspaces.emplace(workspace.id, &workspace);
  }
  for (const auto& node : current_tree.nodes) {
    nodes.emplace(node.id, &node);
  }
  for (const auto& workspace : imported.tree.workspaces) {
    PortableDestinationKind kind = PortableDestinationKind::kNew;
    const auto existing = workspaces.find(workspace.id);
    if (existing != workspaces.end()) {
      kind = SamePortableWorkspace(workspace, *existing->second)
                 ? PortableDestinationKind::kIdentical
                 : PortableDestinationKind::kConflict;
    } else if (std::ranges::any_of(
                   current_tree.workspaces, [&](const auto& other) {
                     return !other.tombstone && other.name == workspace.name;
                   })) {
      kind = PortableDestinationKind::kConflict;
    }
    result.workspaces.push_back({workspace.id, workspace.name, kind});
  }
  for (const auto& node : imported.tree.nodes) {
    const auto existing = nodes.find(node.id);
    if (existing == nodes.end()) {
      ++result.new_nodes;
    } else if (SamePortableNode(node, *existing->second)) {
      ++result.identical_nodes;
    } else {
      ++result.conflicting_nodes;
    }
  }
  for (const auto& split : imported.splits) {
    const auto existing = current_structure.entries.find(split.id);
    if (existing == current_structure.entries.end()) {
      ++result.new_splits;
      continue;
    }
    const auto* record =
        std::get_if<sync::SplitGroupRecord>(&existing->second.record);
    if (record && !record->tombstone && record->id == split.id &&
        record->workspace_id == split.workspace_id &&
        record->topology == split.topology && record->ratios == split.ratios) {
      ++result.identical_splits;
    } else {
      ++result.conflicting_splits;
    }
  }
  for (const auto& archive : imported.archives) {
    const auto existing = current_structure.entries.find(archive.id);
    if (existing == current_structure.entries.end()) {
      const bool page_id_collision = std::ranges::any_of(
          archive.snapshot.pages,
          [&](const auto& page) { return nodes.contains(page.tree_node_id); });
      if (page_id_collision) {
        ++result.conflicting_archives;
      } else {
        ++result.new_archives;
      }
      continue;
    }
    const auto* record =
        std::get_if<sync::TabArchiveEntryRecord>(&existing->second.record);
    if (record && !record->tombstone && !record->restored &&
        record->id == archive.id && record->snapshot == archive.snapshot &&
        record->reason == archive.reason &&
        record->archived_at == archive.archived_at &&
        existing->second.archived_locally &&
        existing->second.private_nodes.size() ==
            archive.snapshot.pages.size() &&
        std::ranges::equal(existing->second.private_nodes,
                           archive.snapshot.pages, std::ranges::equal_to{},
                           &tab_tree::TreeNode::id,
                           &sync::SharedArchivePageSnapshot::tree_node_id)) {
      ++result.identical_archives;
    } else {
      ++result.conflicting_archives;
    }
  }
  return result;
}

}  // namespace ahoi::session
