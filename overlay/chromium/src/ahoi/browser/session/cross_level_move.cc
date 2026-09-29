// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/cross_level_move.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <variant>

#include "ahoi/browser/tab_tree/portable_workspace_selection.h"
#include "base/no_destructor.h"

namespace ahoi::session {
namespace {

std::optional<CrossLevelMoveReceipt>& LatestReceipt() {
  static base::NoDestructor<std::optional<CrossLevelMoveReceipt>> receipt;
  return *receipt;
}

std::set<base::Uuid> ArchivedPageIds(const WorkspaceStructureState& state) {
  std::set<base::Uuid> ids;
  for (const auto& [id, entry] : state.entries) {
    const auto* archive =
        std::get_if<sync::TabArchiveEntryRecord>(&entry.record);
    if (!archive || archive->tombstone || archive->restored) {
      continue;
    }
    for (const auto& page : archive->snapshot.pages) {
      ids.insert(page.tree_node_id);
    }
  }
  return ids;
}

}  // namespace

CrossLevelMoveCheck ExtractCrossLevelMove(
    const tab_tree::TabTreeSnapshot& tree,
    const WorkspaceStructureState& structure,
    const std::vector<base::Uuid>& root_ids,
    CrossLevelMovePayload* payload) {
  *payload = {};
  if (root_ids.empty()) {
    return CrossLevelMoveCheck::kNothingToMove;
  }
  std::map<base::Uuid, const tab_tree::TreeNode*> live;
  std::map<base::Uuid, std::vector<const tab_tree::TreeNode*>> children;
  for (const tab_tree::TreeNode& node : tree.nodes) {
    if (!node.tombstone) {
      live.emplace(node.id, &node);
    }
  }
  const auto root = live.find(root_ids.front());
  if (root == live.end()) {
    return CrossLevelMoveCheck::kNothingToMove;
  }
  const base::Uuid workspace_id = root->second->workspace_id;
  for (const auto& [id, node] : live) {
    if (node->workspace_id == workspace_id && node->parent_id) {
      children[*node->parent_id].push_back(node);
    }
  }
  for (auto& [parent, list] : children) {
    std::ranges::sort(list, [](const auto* a, const auto* b) {
      return a->sort_key < b->sort_key;
    });
  }

  const std::set<base::Uuid> archived = ArchivedPageIds(structure);
  // Parent before child, roots in the given order.
  std::vector<const tab_tree::TreeNode*> ordered;
  std::set<base::Uuid> seen;
  for (const base::Uuid& id : root_ids) {
    const auto it = live.find(id);
    if (it == live.end() || it->second->workspace_id != workspace_id ||
        archived.contains(id) || seen.contains(id)) {
      return CrossLevelMoveCheck::kNothingToMove;
    }
    std::vector<const tab_tree::TreeNode*> pending = {it->second};
    for (size_t index = 0; index < pending.size(); ++index) {
      const tab_tree::TreeNode* node = pending[index];
      if (!seen.insert(node->id).second) {
        // Overlapping roots.
        return CrossLevelMoveCheck::kNothingToMove;
      }
      if (archived.contains(node->id)) {
        // Archived pages stay in the source's archive.
        continue;
      }
      ordered.push_back(node);
      if (const auto kids = children.find(node->id); kids != children.end()) {
        pending.insert(pending.end(), kids->second.begin(),
                       kids->second.end());
      }
    }
  }

  const std::optional<PortableWorkspaceStructure> portable =
      SelectPortableWorkspaceStructure(tree, structure, {workspace_id},
                                       /*include_temporary_pages=*/true,
                                       /*include_archives=*/false);
  if (!portable) {
    return CrossLevelMoveCheck::kNothingToMove;
  }
  std::map<base::Uuid, const tab_tree::PortableWorkspaceNode*> portable_nodes;
  for (const auto& node : portable->tree.nodes) {
    portable_nodes.emplace(node.id, &node);
  }

  std::set<base::Uuid> moved_pages;
  for (const tab_tree::TreeNode* node : ordered) {
    const auto it = portable_nodes.find(node->id);
    if (it == portable_nodes.end()) {
      return CrossLevelMoveCheck::kLocalOnlyPage;
    }
    tab_tree::PortableWorkspaceNode moved = *it->second;
    if (moved.type == tab_tree::TreeNodeType::kSavedPage) {
      if (!moved.target ||
          moved.target->kind != sync::SharedTabTargetKind::kWeb) {
        // An empty new tab has nothing to carry over.
        return CrossLevelMoveCheck::kLocalOnlyPage;
      }
      moved.is_temporary = false;
      moved_pages.insert(moved.id);
      ++payload->pages;
    } else {
      ++payload->folders;
    }
    payload->nodes.push_back(std::move(moved));
  }
  if (payload->nodes.empty()) {
    return CrossLevelMoveCheck::kNothingToMove;
  }

  std::set<base::Uuid> complete_splits;
  for (const auto& split : portable->splits) {
    complete_splits.insert(split.id);
  }
  for (const auto& [id, entry] : structure.entries) {
    const auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
    if (!split || split->tombstone) {
      continue;
    }
    const size_t inside = static_cast<size_t>(std::ranges::count_if(
        split->topology.member_ids, [&](const base::Uuid& member) {
          return moved_pages.contains(member);
        }));
    if (inside == 0) {
      continue;
    }
    if (inside != split->topology.member_ids.size()) {
      return CrossLevelMoveCheck::kSplitWouldMix;
    }
    if (!complete_splits.contains(split->id)) {
      return CrossLevelMoveCheck::kLocalOnlyPage;
    }
    payload->splits.push_back({.id = split->id,
                               .workspace_id = split->workspace_id,
                               .topology = split->topology,
                               .ratios = split->ratios});
  }
  payload->root_ids = root_ids;
  return CrossLevelMoveCheck::kOk;
}

std::optional<CrossLevelMovePlacement> PlaceCrossLevelMove(
    const CrossLevelMovePayload& payload,
    const tab_tree::TabTreeSnapshot& target_tree,
    const base::Uuid& target_workspace_id,
    const base::RepeatingCallback<base::Uuid()>& make_id) {
  std::optional<tab_tree::PortableWorkspaceSelection> target =
      tab_tree::SelectPortableWorkspaceStructure(
          target_tree, {target_workspace_id},
          /*include_temporary_pages=*/false);
  if (!target || target->workspaces.size() != 1 || payload.nodes.empty()) {
    return std::nullopt;
  }
  // Same convention as a merge or a new temporary page: after the last root.
  std::optional<std::string> last_root;
  for (const tab_tree::TreeNode& node : target_tree.nodes) {
    if (!node.tombstone && node.workspace_id == target_workspace_id &&
        !node.parent_id && (!last_root || node.sort_key > *last_root)) {
      last_root = node.sort_key;
    }
  }
  const std::string prefix = last_root.value_or(std::string()) + "@";
  const std::set<base::Uuid> roots(payload.root_ids.begin(),
                                   payload.root_ids.end());

  CrossLevelMovePlacement placement;
  placement.import.tree.workspaces = std::move(target->workspaces);
  for (const auto& node : payload.nodes) {
    const base::Uuid id = make_id.Run();
    if (!id.is_valid() || !placement.new_ids.emplace(node.id, id).second) {
      return std::nullopt;
    }
  }
  for (const auto& source : payload.nodes) {
    tab_tree::PortableWorkspaceNode node = source;
    node.id = placement.new_ids.at(source.id);
    node.workspace_id = target_workspace_id;
    if (roots.contains(source.id) || !source.parent_id) {
      node.parent_id.reset();
      node.sort_key = prefix + source.sort_key;
      placement.root_ids.push_back(node.id);
    } else {
      const auto parent = placement.new_ids.find(*source.parent_id);
      if (parent == placement.new_ids.end()) {
        return std::nullopt;
      }
      node.parent_id = parent->second;
    }
    placement.import.tree.nodes.push_back(std::move(node));
  }
  for (sync::SharedSplitMetadata split : payload.splits) {
    split.id = make_id.Run();
    split.workspace_id = target_workspace_id;
    for (base::Uuid& member : split.topology.member_ids) {
      const auto moved = placement.new_ids.find(member);
      if (moved == placement.new_ids.end()) {
        return std::nullopt;
      }
      member = moved->second;
    }
    placement.import.splits.push_back(std::move(split));
  }
  return placement;
}

void RememberCrossLevelMove(CrossLevelMoveReceipt receipt) {
  LatestReceipt() = std::move(receipt);
}

const CrossLevelMoveReceipt* GetLatestCrossLevelMove() {
  const auto& receipt = LatestReceipt();
  return receipt ? &*receipt : nullptr;
}

void ForgetCrossLevelMove() {
  LatestReceipt().reset();
}

std::optional<LatestTreeUndo> FindLatestTreeUndo(
    const tab_tree::TabTreeSnapshot& tree) {
  const tab_tree::UndoOperationSnapshot* latest = nullptr;
  for (const auto& operation : tree.undo_operations) {
    if (!latest || operation.operation_id > latest->operation_id) {
      latest = &operation;
    }
  }
  if (!latest) {
    return std::nullopt;
  }
  return LatestTreeUndo{.kind = latest->kind,
                        .subject_node_id = latest->subject_node_id,
                        .created_at = latest->created_at};
}

bool IsCrossLevelMoveLatestOnSource(
    const CrossLevelMoveReceipt& receipt,
    const std::optional<LatestTreeUndo>& latest) {
  if (receipt.source_deletion_subject) {
    return latest && latest->kind == tab_tree::UndoMutationKind::kDelete &&
           latest->subject_node_id == *receipt.source_deletion_subject;
  }
  return !latest || latest->created_at < receipt.moved_at;
}

bool IsCrossLevelMoveLatestOnTarget(
    const CrossLevelMoveReceipt& receipt,
    const std::optional<LatestTreeUndo>& latest) {
  return !latest || latest->created_at < receipt.moved_at;
}

}  // namespace ahoi::session
