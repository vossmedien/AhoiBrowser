// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view_model.h"

#include <utility>

#include "base/check.h"

namespace ahoi::sidebar {

bool SidebarTreeViewModel::SetExpanded(const base::Uuid& node_id,
                                       bool expanded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto node = nodes_.find(node_id);
  auto row = row_by_id_.find(node_id);
  if (node == nodes_.end() || row == row_by_id_.end() ||
      node->second.node.type != tab_tree::TreeNodeType::kFolder ||
      (expanded && !node->second.children_loaded)) {
    return false;
  }
  // Search forces only the ancestor paths needed by the projection to look
  // expanded. Toggling one of those rows must not leak into the user's normal
  // expansion state.
  if (search_exact_match_node_ids_.has_value()) {
    return true;
  }
  if (expanded_nodes_.contains(node_id) == expanded) {
    return true;
  }

  const size_t row_index = row->second;
  const size_t child_depth = rows_[row_index].depth + 1;
  if (expanded) {
    std::vector<Row> children =
        BuildVisibleChildren(node->second.children, child_depth);
    if (!children.empty()) {
      for (auto& observer : observers_) {
        observer.OnFolderExpansionChanging(node_id, true);
      }
    }
    expanded_nodes_.insert(node_id);
    rows_[row_index].expanded = true;
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnRowsChanged(row_index, 1);
    }
    ApplyVisibleSplice(row_index + 1, 0, std::move(children));
    return true;
  }

  size_t descendant_count = 0;
  while (row_index + 1 + descendant_count < rows_.size() &&
         rows_[row_index + 1 + descendant_count].depth >= child_depth) {
    ++descendant_count;
  }
  if (descendant_count > 0) {
    for (auto& observer : observers_) {
      observer.OnFolderExpansionChanging(node_id, false);
    }
  }
  expanded_nodes_.erase(node_id);
  rows_[row_index].expanded = false;
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnRowsChanged(row_index, 1);
  }
  if (selected_node_id_.has_value()) {
    const auto selected_row = row_by_id_.find(*selected_node_id_);
    if (selected_row != row_by_id_.end() &&
        selected_row->second >= row_index + 1 &&
        selected_row->second < row_index + 1 + descendant_count) {
      CHECK(SetSelectedNode(node_id));
    }
  }
  ApplyVisibleSplice(row_index + 1, descendant_count, {});
  return true;
}

}  // namespace ahoi::sidebar
