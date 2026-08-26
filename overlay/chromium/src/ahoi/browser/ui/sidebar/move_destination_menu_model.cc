// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "base/memory/raw_ptr.h"

namespace ahoi::sidebar {

namespace {

struct FolderRecord {
  raw_ptr<const tab_tree::TreeNode> node = nullptr;
  bool selectable = true;
  std::vector<base::Uuid> children;
};

bool NodeLess(const tab_tree::TreeNode* left, const tab_tree::TreeNode* right) {
  if (left->sort_key != right->sort_key) {
    return left->sort_key < right->sort_key;
  }
  if (left->title != right->title) {
    return left->title < right->title;
  }
  return left->id.AsLowercaseString() < right->id.AsLowercaseString();
}

std::optional<MoveDestinationFolder> BuildFolder(
    const base::Uuid& folder_id,
    const std::map<base::Uuid, FolderRecord>& folders) {
  const auto found = folders.find(folder_id);
  if (found == folders.end() || !found->second.node) {
    return std::nullopt;
  }

  MoveDestinationFolder result{
      .id = folder_id,
      .title = found->second.node->title,
      .selectable = found->second.selectable,
  };
  result.children.reserve(found->second.children.size());
  for (const base::Uuid& child_id : found->second.children) {
    std::optional<MoveDestinationFolder> child = BuildFolder(child_id, folders);
    if (child.has_value()) {
      result.children.push_back(std::move(*child));
    }
  }
  if (!result.selectable && result.children.empty()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace

std::vector<MoveDestinationWorkspace> BuildMoveDestinationMenuModel(
    const std::vector<tab_tree::Workspace>& ordered_workspaces,
    const tab_tree::TabTreeSnapshot& snapshot,
    const tab_tree::TreeNode* source) {
  std::set<base::Uuid> excluded_ids;
  if (source) {
    excluded_ids.insert(source->id);
    std::map<base::Uuid, std::vector<base::Uuid>> children_by_parent;
    for (const tab_tree::TreeNode& node : snapshot.nodes) {
      if (!node.tombstone && node.parent_id.has_value()) {
        children_by_parent[*node.parent_id].push_back(node.id);
      }
    }
    std::vector<base::Uuid> pending = {source->id};
    for (size_t index = 0; index < pending.size(); ++index) {
      const auto children = children_by_parent.find(pending[index]);
      if (children == children_by_parent.end()) {
        continue;
      }
      for (const base::Uuid& child_id : children->second) {
        if (excluded_ids.insert(child_id).second) {
          pending.push_back(child_id);
        }
      }
    }
  }

  std::vector<MoveDestinationWorkspace> result;
  result.reserve(ordered_workspaces.size());
  for (const tab_tree::Workspace& workspace : ordered_workspaces) {
    if (workspace.tombstone) {
      continue;
    }
    const bool source_is_here = source && source->workspace_id == workspace.id;
    MoveDestinationWorkspace workspace_result{
        .id = workspace.id,
        .name = workspace.name,
        .icon = workspace.icon,
        .root_selectable = !source_is_here || source->parent_id.has_value(),
    };

    std::map<base::Uuid, FolderRecord> folders;
    for (const tab_tree::TreeNode& node : snapshot.nodes) {
      if (node.tombstone || node.workspace_id != workspace.id ||
          node.type != tab_tree::TreeNodeType::kFolder ||
          excluded_ids.contains(node.id)) {
        continue;
      }
      folders.emplace(
          node.id,
          FolderRecord{
              .node = &node,
              .selectable = !source_is_here || !source->parent_id.has_value() ||
                            *source->parent_id != node.id,
          });
    }

    std::vector<base::Uuid> root_ids;
    for (auto& [folder_id, folder] : folders) {
      if (folder.node->parent_id.has_value()) {
        const auto parent = folders.find(*folder.node->parent_id);
        if (parent != folders.end()) {
          parent->second.children.push_back(folder_id);
          continue;
        }
      }
      root_ids.push_back(folder_id);
    }
    const auto sort_ids = [&folders](std::vector<base::Uuid>* ids) {
      std::ranges::sort(
          *ids, [&folders](const base::Uuid& left, const base::Uuid& right) {
            return NodeLess(folders.at(left).node, folders.at(right).node);
          });
    };
    sort_ids(&root_ids);
    for (auto& [folder_id, folder] : folders) {
      sort_ids(&folder.children);
    }

    for (const base::Uuid& root_id : root_ids) {
      std::optional<MoveDestinationFolder> folder =
          BuildFolder(root_id, folders);
      if (folder.has_value()) {
        workspace_result.folders.push_back(std::move(*folder));
      }
    }
    if (workspace_result.root_selectable || !workspace_result.folders.empty()) {
      result.push_back(std::move(workspace_result));
    }
  }
  return result;
}

}  // namespace ahoi::sidebar
