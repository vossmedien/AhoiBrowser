// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view_model.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"

namespace ahoi::sidebar {

namespace {

bool RowHasSamePosition(const SidebarTreeViewModel::Row& lhs,
                        const SidebarTreeViewModel::Row& rhs) {
  return lhs.node_id == rhs.node_id && lhs.depth == rhs.depth;
}

bool IsNodeWellFormed(const tab_tree::TreeNode& node) {
  if (node.model_version != tab_tree::kCurrentModelVersion ||
      !node.id.is_valid() || !node.workspace_id.is_valid() ||
      node.title.empty() || node.sort_key.empty() ||
      node.created_at.is_null() || node.modified_at.is_null() ||
      (node.parent_id.has_value() && !node.parent_id->is_valid())) {
    return false;
  }
  if (node.type == tab_tree::TreeNodeType::kFolder) {
    return node.url.is_empty();
  }
  return node.type == tab_tree::TreeNodeType::kSavedPage &&
         node.url.is_valid() && !node.url.is_empty();
}

}  // namespace

SidebarTreeViewModel::SidebarTreeViewModel() = default;

SidebarTreeViewModel::~SidebarTreeViewModel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(update_depth_, 0U);
}

void SidebarTreeViewModel::AddObserver(SidebarTreeViewModelObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void SidebarTreeViewModel::RemoveObserver(
    SidebarTreeViewModelObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool SidebarTreeViewModel::ResetWorkspace(const base::Uuid& workspace_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(update_depth_, 0U);
  if (!workspace_id.is_valid()) {
    return false;
  }
  const std::optional<base::Uuid> old_selection = selected_node_id_;
  workspace_id_ = workspace_id;
  nodes_.clear();
  root_children_.clear();
  root_children_loaded_ = false;
  expanded_nodes_.clear();
  rows_.clear();
  row_by_id_.clear();
  selected_node_id_.reset();
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnTreeReset();
  }
  if (old_selection.has_value()) {
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnSelectionChanged(old_selection, std::nullopt);
    }
  }
  return true;
}

bool SidebarTreeViewModel::ReplaceChildren(
    std::optional<base::Uuid> parent_id,
    std::vector<tab_tree::TreeNode> children) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!workspace_id_.has_value() ||
      (parent_id.has_value() && !parent_id->is_valid())) {
    return false;
  }

  size_t first_row = 0;
  size_t old_count = rows_.size();
  size_t child_depth = 0;
  if (parent_id.has_value()) {
    auto parent_it = nodes_.find(*parent_id);
    if (parent_it == nodes_.end() ||
        parent_it->second.node.type != tab_tree::TreeNodeType::kFolder) {
      return false;
    }
    const auto parent_row = row_by_id_.find(*parent_id);
    if (parent_row == row_by_id_.end() ||
        !expanded_nodes_.contains(*parent_id)) {
      old_count = 0;
    } else {
      first_row = parent_row->second + 1;
      child_depth = rows_[parent_row->second].depth + 1;
      old_count = 0;
      while (first_row + old_count < rows_.size() &&
             rows_[first_row + old_count].depth >= child_depth) {
        ++old_count;
      }
    }
  }

  std::sort(children.begin(), children.end(),
            [](const tab_tree::TreeNode& lhs, const tab_tree::TreeNode& rhs) {
              if (lhs.sort_key != rhs.sort_key) {
                return lhs.sort_key < rhs.sort_key;
              }
              return lhs.id < rhs.id;
            });
  std::unordered_set<base::Uuid, base::UuidHash> child_ids_seen;
  child_ids_seen.reserve(children.size());
  std::vector<base::Uuid> child_ids;
  child_ids.reserve(children.size());
  for (const tab_tree::TreeNode& child : children) {
    if (!IsNodeWellFormed(child) || child.workspace_id != *workspace_id_ ||
        child.parent_id != parent_id || child.tombstone ||
        (parent_id.has_value() && child.id == *parent_id) ||
        !child_ids_seen.insert(child.id).second) {
      return false;
    }
    child_ids.push_back(child.id);
  }

  nodes_.reserve(nodes_.size() + children.size());
  for (const tab_tree::TreeNode& child : children) {
    CHECK(CacheNode(child));
  }
  if (parent_id.has_value()) {
    auto parent = nodes_.find(*parent_id);
    CHECK(parent != nodes_.end());
    parent->second.children = child_ids;
    parent->second.children_loaded = true;
  } else {
    root_children_ = child_ids;
    root_children_loaded_ = true;
  }

  if (parent_id.has_value() && old_count == 0 && !IsRowVisible(*parent_id)) {
    return true;
  }
  if (parent_id.has_value() && !expanded_nodes_.contains(*parent_id)) {
    return true;
  }
  ApplyVisibleSplice(first_row, old_count,
                     BuildVisibleChildren(child_ids, child_depth));
  return true;
}

bool SidebarTreeViewModel::CacheNode(const tab_tree::TreeNode& node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!workspace_id_.has_value() || !IsNodeWellFormed(node) ||
      node.workspace_id != *workspace_id_ || node.tombstone) {
    return false;
  }
  auto [it, inserted] = nodes_.try_emplace(node.id);
  const bool same_position =
      inserted || (it->second.node.parent_id == node.parent_id &&
                   it->second.node.sort_key == node.sort_key &&
                   it->second.node.workspace_id == node.workspace_id);
  const bool content_changed = inserted || it->second.node != node;
  it->second.node = node;
  if (inserted) {
    it->second.children_loaded = false;
  }
  const auto row = row_by_id_.find(node.id);
  if (!inserted && same_position && content_changed &&
      row != row_by_id_.end()) {
    rows_[row->second].type = node.type;
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnRowsChanged(row->second, 1);
    }
  }
  return true;
}

void SidebarTreeViewModel::EraseCachedNodes(
    const std::vector<base::Uuid>& node_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const base::Uuid& node_id : node_ids) {
    if (IsRowVisible(node_id)) {
      continue;
    }
    expanded_nodes_.erase(node_id);
    nodes_.erase(node_id);
  }
}

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
  if (expanded_nodes_.contains(node_id) == expanded) {
    return true;
  }

  const size_t row_index = row->second;
  const size_t child_depth = rows_[row_index].depth + 1;
  if (expanded) {
    expanded_nodes_.insert(node_id);
    rows_[row_index].expanded = true;
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnRowsChanged(row_index, 1);
    }
    ApplyVisibleSplice(
        row_index + 1, 0,
        BuildVisibleChildren(node->second.children, child_depth));
    return true;
  }

  size_t descendant_count = 0;
  while (row_index + 1 + descendant_count < rows_.size() &&
         rows_[row_index + 1 + descendant_count].depth >= child_depth) {
    ++descendant_count;
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

bool SidebarTreeViewModel::SetSelectedNode(std::optional<base::Uuid> node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (node_id.has_value() && !IsRowVisible(*node_id)) {
    return false;
  }
  if (selected_node_id_ == node_id) {
    return true;
  }
  const std::optional<base::Uuid> old_selection = selected_node_id_;
  selected_node_id_ = node_id;
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnSelectionChanged(old_selection, selected_node_id_);
  }
  return true;
}

void SidebarTreeViewModel::BeginUpdate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (update_depth_++ != 0) {
    return;
  }
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnBatchUpdateStarted();
  }
}

void SidebarTreeViewModel::EndUpdate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_GT(update_depth_, 0U);
  if (--update_depth_ != 0) {
    return;
  }
  NormalizeSelection();
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnBatchUpdateEnded();
  }
}

void SidebarTreeViewModel::NormalizeSelection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!selected_node_id_.has_value() || IsRowVisible(*selected_node_id_)) {
    return;
  }

  std::optional<base::Uuid> cursor = selected_node_id_;
  std::unordered_set<base::Uuid, base::UuidHash> visited;
  while (cursor.has_value() && visited.insert(*cursor).second) {
    auto node = nodes_.find(*cursor);
    if (node == nodes_.end()) {
      break;
    }
    cursor = node->second.node.parent_id;
    if (cursor.has_value() && IsRowVisible(*cursor)) {
      CHECK(SetSelectedNode(cursor));
      return;
    }
  }
  CHECK(SetSelectedNode(std::nullopt));
}

const tab_tree::TreeNode* SidebarTreeViewModel::GetNode(
    const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto node = nodes_.find(node_id);
  return node == nodes_.end() ? nullptr : &node->second.node;
}

std::optional<size_t> SidebarTreeViewModel::GetRowForNode(
    const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto row = row_by_id_.find(node_id);
  return row == row_by_id_.end() ? std::nullopt
                                 : std::make_optional(row->second);
}

bool SidebarTreeViewModel::IsExpanded(const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return expanded_nodes_.contains(node_id);
}

bool SidebarTreeViewModel::AreChildrenLoaded(
    std::optional<base::Uuid> parent_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!parent_id.has_value()) {
    return root_children_loaded_;
  }
  auto parent = nodes_.find(*parent_id);
  return parent != nodes_.end() && parent->second.children_loaded;
}

bool SidebarTreeViewModel::GetLoadedChildren(
    std::optional<base::Uuid> parent_id,
    std::vector<const tab_tree::TreeNode*>* children) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!children || !AreChildrenLoaded(parent_id)) {
    return false;
  }
  const std::vector<base::Uuid>* child_ids = &root_children_;
  if (parent_id.has_value()) {
    auto parent = nodes_.find(*parent_id);
    if (parent == nodes_.end()) {
      return false;
    }
    child_ids = &parent->second.children;
  }

  std::vector<const tab_tree::TreeNode*> loaded;
  loaded.reserve(child_ids->size());
  for (const base::Uuid& child_id : *child_ids) {
    auto child = nodes_.find(child_id);
    if (child == nodes_.end()) {
      return false;
    }
    loaded.push_back(&child->second.node);
  }
  *children = std::move(loaded);
  return true;
}

std::vector<SidebarTreeViewModel::Row>
SidebarTreeViewModel::BuildVisibleChildren(
    const std::vector<base::Uuid>& child_ids,
    size_t depth) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  struct PendingRow {
    base::Uuid node_id;
    size_t depth;
    size_t position_in_parent;
    size_t sibling_count;
  };
  std::vector<PendingRow> pending;
  pending.reserve(child_ids.size());
  for (size_t index = child_ids.size(); index > 0; --index) {
    pending.push_back({child_ids[index - 1], depth, index, child_ids.size()});
  }

  std::vector<Row> visible;
  std::unordered_set<base::Uuid, base::UuidHash> visited;
  while (!pending.empty()) {
    PendingRow current = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(current.node_id).second) {
      continue;
    }
    auto node = nodes_.find(current.node_id);
    if (node == nodes_.end()) {
      continue;
    }
    const bool expanded = expanded_nodes_.contains(current.node_id);
    visible.push_back({.node_id = current.node_id,
                       .depth = current.depth,
                       .position_in_parent = current.position_in_parent,
                       .sibling_count = current.sibling_count,
                       .type = node->second.node.type,
                       .expanded = expanded});
    if (!expanded || !node->second.children_loaded) {
      continue;
    }
    for (size_t index = node->second.children.size(); index > 0; --index) {
      pending.push_back({node->second.children[index - 1], current.depth + 1,
                         index, node->second.children.size()});
    }
  }
  return visible;
}

void SidebarTreeViewModel::ApplyVisibleSplice(size_t first_row,
                                              size_t old_count,
                                              std::vector<Row> replacement) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_LE(first_row + old_count, rows_.size());

  std::vector<Row> old_rows(
      rows_.begin() + static_cast<ptrdiff_t>(first_row),
      rows_.begin() + static_cast<ptrdiff_t>(first_row + old_count));
  size_t prefix_count = 0;
  while (
      prefix_count < old_rows.size() && prefix_count < replacement.size() &&
      RowHasSamePosition(old_rows[prefix_count], replacement[prefix_count])) {
    ++prefix_count;
  }
  size_t suffix_count = 0;
  while (
      suffix_count < old_rows.size() - prefix_count &&
      suffix_count < replacement.size() - prefix_count &&
      RowHasSamePosition(old_rows[old_rows.size() - 1 - suffix_count],
                         replacement[replacement.size() - 1 - suffix_count])) {
    ++suffix_count;
  }

  const size_t removed_count = old_rows.size() - prefix_count - suffix_count;
  const size_t inserted_count =
      replacement.size() - prefix_count - suffix_count;
  const size_t mutation_row = first_row + prefix_count;
  if (removed_count > 0) {
    rows_.erase(
        rows_.begin() + static_cast<ptrdiff_t>(mutation_row),
        rows_.begin() + static_cast<ptrdiff_t>(mutation_row + removed_count));
    RebuildRowIndex(mutation_row);
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnRowsRemoved(mutation_row, removed_count);
    }
  }
  if (inserted_count > 0) {
    rows_.insert(rows_.begin() + static_cast<ptrdiff_t>(mutation_row),
                 replacement.begin() + static_cast<ptrdiff_t>(prefix_count),
                 replacement.end() - static_cast<ptrdiff_t>(suffix_count));
    RebuildRowIndex(mutation_row);
    for (SidebarTreeViewModelObserver& observer : observers_) {
      observer.OnRowsInserted(mutation_row, inserted_count);
    }
  }

  for (size_t index = 0; index < replacement.size(); ++index) {
    rows_[first_row + index] = replacement[index];
  }
  RebuildRowIndex(first_row);
  NotifyChangedRuns(first_row, old_rows, replacement, prefix_count,
                    suffix_count);
  if (update_depth_ == 0) {
    NormalizeSelection();
  }
}

void SidebarTreeViewModel::RebuildRowIndex(size_t first_row) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  row_by_id_.reserve(rows_.size());
  if (first_row == 0) {
    row_by_id_.clear();
  } else {
    for (auto it = row_by_id_.begin(); it != row_by_id_.end();) {
      if (it->second >= first_row) {
        it = row_by_id_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (size_t index = first_row; index < rows_.size(); ++index) {
    row_by_id_[rows_[index].node_id] = index;
  }
}

void SidebarTreeViewModel::NotifyChangedRuns(size_t first_row,
                                             const std::vector<Row>& old_rows,
                                             const std::vector<Row>& new_rows,
                                             size_t prefix_count,
                                             size_t suffix_count) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t run_start = prefix_count;
  bool in_run = false;
  for (size_t index = 0; index < prefix_count; ++index) {
    if (old_rows[index] != new_rows[index] && !in_run) {
      run_start = index;
      in_run = true;
    } else if (old_rows[index] == new_rows[index] && in_run) {
      NotifyRowsChanged(first_row + run_start, index - run_start);
      in_run = false;
    }
  }
  if (in_run) {
    NotifyRowsChanged(first_row + run_start, prefix_count - run_start);
  }

  if (suffix_count == 0) {
    return;
  }
  const size_t old_suffix = old_rows.size() - suffix_count;
  const size_t new_suffix = new_rows.size() - suffix_count;
  run_start = 0;
  in_run = false;
  for (size_t offset = 0; offset < suffix_count; ++offset) {
    if (old_rows[old_suffix + offset] != new_rows[new_suffix + offset] &&
        !in_run) {
      run_start = new_suffix + offset;
      in_run = true;
    } else if (old_rows[old_suffix + offset] == new_rows[new_suffix + offset] &&
               in_run) {
      NotifyRowsChanged(first_row + run_start, new_suffix + offset - run_start);
      in_run = false;
    }
  }
  if (in_run) {
    NotifyRowsChanged(first_row + run_start, new_rows.size() - run_start);
  }
}

void SidebarTreeViewModel::NotifyRowsChanged(size_t first_row, size_t count) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (count == 0) {
    return;
  }
  for (SidebarTreeViewModelObserver& observer : observers_) {
    observer.OnRowsChanged(first_row, count);
  }
}

bool SidebarTreeViewModel::IsRowVisible(const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return row_by_id_.contains(node_id);
}

}  // namespace ahoi::sidebar
