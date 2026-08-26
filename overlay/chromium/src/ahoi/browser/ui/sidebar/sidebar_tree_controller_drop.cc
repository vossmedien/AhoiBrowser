// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "base/check.h"

namespace ahoi::sidebar {

SidebarTreeController::DropValidationResult
SidebarTreeController::ResolveDropDestination(const tab_tree::TreeNode& source,
                                              const DropTarget& target,
                                              DropOperation operation,
                                              DropPlan* plan) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::Workspace destination_workspace;
  const tab_tree::TabTreeStore::Result workspace_result =
      store_->GetWorkspace(target.workspace_id, &destination_workspace);
  if (workspace_result == tab_tree::TabTreeStore::Result::kNotFound ||
      (workspace_result == tab_tree::TabTreeStore::Result::kOk &&
       destination_workspace.tombstone)) {
    return DropValidationResult::kTargetNotFound;
  }
  if (workspace_result != tab_tree::TabTreeStore::Result::kOk) {
    return DropValidationResult::kStoreError;
  }

  std::optional<base::Uuid> parent_id;
  std::optional<base::Uuid> target_node_id = target.target_node_id;
  if (target_node_id.has_value()) {
    tab_tree::TreeNode target_node;
    tab_tree::TabTreeStore::Result target_result =
        tab_tree::TabTreeStore::Result::kNotFound;
    if (view_model_.workspace_id().has_value() &&
        *view_model_.workspace_id() == target.workspace_id) {
      if (const tab_tree::TreeNode* cached =
              view_model_.GetNode(*target_node_id)) {
        target_node = *cached;
        target_result = tab_tree::TabTreeStore::Result::kOk;
      }
    }
    if (target_result == tab_tree::TabTreeStore::Result::kNotFound) {
      target_result = store_->GetNode(*target_node_id, &target_node);
    }
    if (target_result == tab_tree::TabTreeStore::Result::kNotFound ||
        (target_result == tab_tree::TabTreeStore::Result::kOk &&
         target_node.tombstone)) {
      return DropValidationResult::kTargetNotFound;
    }
    if (target_result != tab_tree::TabTreeStore::Result::kOk) {
      return DropValidationResult::kStoreError;
    }
    if (target_node.workspace_id != target.workspace_id) {
      return DropValidationResult::kInvalidArgument;
    }
    if (target.position == DropPosition::kInside) {
      if (target_node.type != tab_tree::TreeNodeType::kFolder) {
        return DropValidationResult::kTargetNotFolder;
      }
      parent_id = target_node.id;
    } else {
      parent_id = target_node.parent_id;
    }
  }

  if (operation == DropOperation::kMove) {
    if (target_node_id == source.id &&
        target.position != DropPosition::kInside) {
      return DropValidationResult::kNoOp;
    }
    std::optional<base::Uuid> cursor = parent_id;
    std::unordered_set<base::Uuid, base::UuidHash> visited;
    while (cursor.has_value()) {
      if (*cursor == source.id || !visited.insert(*cursor).second) {
        return DropValidationResult::kCycle;
      }
      tab_tree::TreeNode ancestor;
      tab_tree::TabTreeStore::Result result =
          tab_tree::TabTreeStore::Result::kNotFound;
      if (view_model_.workspace_id().has_value() &&
          *view_model_.workspace_id() == target.workspace_id) {
        if (const tab_tree::TreeNode* cached = view_model_.GetNode(*cursor)) {
          ancestor = *cached;
          result = tab_tree::TabTreeStore::Result::kOk;
        }
      }
      if (result == tab_tree::TabTreeStore::Result::kNotFound) {
        result = store_->GetNode(*cursor, &ancestor);
      }
      if (result == tab_tree::TabTreeStore::Result::kNotFound ||
          (result == tab_tree::TabTreeStore::Result::kOk &&
           ancestor.tombstone)) {
        return DropValidationResult::kTargetNotFound;
      }
      if (result != tab_tree::TabTreeStore::Result::kOk) {
        return DropValidationResult::kStoreError;
      }
      if (ancestor.workspace_id != target.workspace_id ||
          ancestor.type != tab_tree::TreeNodeType::kFolder) {
        return DropValidationResult::kInvalidArgument;
      }
      cursor = ancestor.parent_id;
    }
  }

  std::vector<tab_tree::TreeNode> stored_siblings;
  std::vector<const tab_tree::TreeNode*> siblings;
  tab_tree::TabTreeStore::Result children_result =
      tab_tree::TabTreeStore::Result::kNotFound;
  if (view_model_.workspace_id().has_value() &&
      *view_model_.workspace_id() == target.workspace_id &&
      view_model_.GetLoadedChildren(parent_id, &siblings)) {
    children_result = tab_tree::TabTreeStore::Result::kOk;
  }
  if (children_result == tab_tree::TabTreeStore::Result::kNotFound) {
    children_result =
        store_->GetChildren(target.workspace_id, parent_id, &stored_siblings);
    siblings.reserve(stored_siblings.size());
    for (const tab_tree::TreeNode& sibling : stored_siblings) {
      siblings.push_back(&sibling);
    }
  }
  if (children_result != tab_tree::TabTreeStore::Result::kOk) {
    return children_result == tab_tree::TabTreeStore::Result::kNotFound
               ? DropValidationResult::kTargetNotFound
               : DropValidationResult::kStoreError;
  }

  std::optional<size_t> old_index;
  if (operation == DropOperation::kMove &&
      source.workspace_id == target.workspace_id &&
      source.parent_id == parent_id) {
    auto source_it = std::ranges::find(
        siblings, source.id,
        [](const tab_tree::TreeNode* node) { return node->id; });
    if (source_it != siblings.end()) {
      old_index =
          static_cast<size_t>(std::distance(siblings.begin(), source_it));
      siblings.erase(source_it);
    }
  }

  size_t insertion_index = siblings.size();
  if (target_node_id.has_value() && target.position != DropPosition::kInside) {
    auto target_it = std::ranges::find(
        siblings, *target_node_id,
        [](const tab_tree::TreeNode* node) { return node->id; });
    if (target_it == siblings.end()) {
      return DropValidationResult::kTargetNotFound;
    }
    insertion_index =
        static_cast<size_t>(std::distance(siblings.begin(), target_it));
    if (target.position == DropPosition::kAfter) {
      ++insertion_index;
    }
  }
  if (old_index.has_value() && insertion_index == *old_index) {
    return DropValidationResult::kNoOp;
  }

  std::optional<std::string> left;
  std::optional<std::string> right;
  if (insertion_index > 0) {
    left = siblings[insertion_index - 1]->sort_key;
  }
  if (insertion_index < siblings.size()) {
    right = siblings[insertion_index]->sort_key;
  }
  const std::optional<std::string> sort_key =
      GenerateSortKeyBetween(left, right);
  if (!sort_key.has_value()) {
    return DropValidationResult::kNoOrderingSpace;
  }

  *plan = {.source_node_id = source.id,
           .workspace_id = target.workspace_id,
           .parent_id = parent_id,
           .insertion_index = insertion_index,
           .sort_key = *sort_key,
           .operation = operation};
  return DropValidationResult::kAllowed;
}

tab_tree::TabTreeStore::Result SidebarTreeController::CopySubtree(
    const DropPlan& plan,
    base::Time modified_at,
    base::Uuid* copied_root_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<tab_tree::TreeNode> source_nodes;
  const tab_tree::TabTreeStore::Result read_result =
      store_->GetSubtree(plan.source_node_id, &source_nodes);
  if (read_result != tab_tree::TabTreeStore::Result::kOk) {
    return read_result;
  }

  std::unordered_map<base::Uuid, base::Uuid, base::UuidHash> copied_ids;
  std::unordered_set<base::Uuid, base::UuidHash> generated_ids;
  copied_ids.reserve(source_nodes.size());
  generated_ids.reserve(source_nodes.size());
  for (const tab_tree::TreeNode& source_node : source_nodes) {
    base::Uuid generated_id;
    do {
      generated_id = base::Uuid::GenerateRandomV4();
    } while (!generated_ids.insert(generated_id).second);
    copied_ids.emplace(source_node.id, generated_id);
  }
  auto copied_root = copied_ids.find(plan.source_node_id);
  if (copied_root == copied_ids.end()) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }

  std::vector<tab_tree::TreeNode> copies;
  copies.reserve(source_nodes.size());
  for (const tab_tree::TreeNode& source_node : source_nodes) {
    tab_tree::TreeNode copy = source_node;
    copy.id = copied_ids.at(source_node.id);
    copy.workspace_id = plan.workspace_id;
    if (source_node.id == plan.source_node_id) {
      copy.parent_id = plan.parent_id;
      copy.sort_key = plan.sort_key;
    } else {
      if (!source_node.parent_id.has_value() ||
          !copied_ids.contains(*source_node.parent_id)) {
        return tab_tree::TabTreeStore::Result::kDatabaseError;
      }
      copy.parent_id = copied_ids.at(*source_node.parent_id);
    }
    copy.created_at = modified_at;
    copy.modified_at = modified_at;
    copy.tombstone = false;
    copies.push_back(std::move(copy));
  }

  const tab_tree::TabTreeStore::Result result =
      store_->CreateNodesAtomically(copies);
  if (result == tab_tree::TabTreeStore::Result::kOk) {
    *copied_root_id = copied_root->second;
  }
  return result;
}

std::optional<std::string> SidebarTreeController::GenerateSortKeyBetween(
    const std::optional<std::string>& left,
    const std::optional<std::string>& right) {
  if ((left.has_value() && left->empty()) ||
      (right.has_value() && right->empty()) ||
      (left.has_value() && right.has_value() && *left >= *right)) {
    return std::nullopt;
  }
  constexpr unsigned char kMiddleByte = 0x40;
  if (!left.has_value() && !right.has_value()) {
    return std::string(1, static_cast<char>(kMiddleByte));
  }
  if (left.has_value() && !right.has_value()) {
    return *left + static_cast<char>(kMiddleByte);
  }
  if (!left.has_value()) {
    const unsigned char first = static_cast<unsigned char>(right->front());
    if (first > 1) {
      return std::string(1, static_cast<char>(first / 2));
    }
    if (right->size() > 1) {
      return right->substr(0, 1);
    }
    return std::nullopt;
  }

  size_t common = 0;
  while (common < left->size() && common < right->size() &&
         (*left)[common] == (*right)[common]) {
    ++common;
  }
  if (common == left->size()) {
    const unsigned char right_byte =
        static_cast<unsigned char>((*right)[common]);
    if (right_byte > 1) {
      return left->substr(0, common) + static_cast<char>(right_byte / 2);
    }
    if (right->size() > common + 1) {
      return right->substr(0, common + 1);
    }
    return std::nullopt;
  }

  const unsigned char left_byte = static_cast<unsigned char>((*left)[common]);
  const unsigned char right_byte = static_cast<unsigned char>((*right)[common]);
  if (left_byte + 1 < right_byte) {
    return left->substr(0, common) +
           static_cast<char>(left_byte + (right_byte - left_byte) / 2);
  }
  return *left + static_cast<char>(kMiddleByte);
}

}  // namespace ahoi::sidebar
