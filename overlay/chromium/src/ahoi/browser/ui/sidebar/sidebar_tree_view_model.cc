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
  search_exact_match_node_ids_.reset();
  search_context_groups_.clear();
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

  if (search_exact_match_node_ids_.has_value()) {
    RebuildCurrentProjection(/*preserve_selection=*/true);
    return true;
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
  const bool search_active = search_exact_match_node_ids_.has_value();
  bool search_projection_changed = false;
  for (const base::Uuid& node_id : node_ids) {
    // In the normal projection a visible row is owned by its parent's
    // ReplaceChildren splice and cannot be erased out from under that update.
    // Search rows are a derived projection, so a deleted exact match or
    // ancestor must be evicted immediately or it survives as a ghost result.
    if (!search_active && IsRowVisible(node_id)) {
      continue;
    }
    expanded_nodes_.erase(node_id);
    search_projection_changed |= nodes_.erase(node_id) > 0;
    if (search_active) {
      search_projection_changed |=
          search_exact_match_node_ids_->erase(node_id) > 0;
    }
  }
  if (search_active && search_projection_changed) {
    RebuildCurrentProjection(/*preserve_selection=*/true);
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

bool SidebarTreeViewModel::SetSearchMatches(
    std::unordered_set<base::Uuid, base::UuidHash> node_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!workspace_id_.has_value() ||
      std::ranges::any_of(
          node_ids, [](const base::Uuid& id) { return !id.is_valid(); })) {
    return false;
  }
  if (search_exact_match_node_ids_.has_value() &&
      *search_exact_match_node_ids_ == node_ids) {
    return true;
  }
  search_exact_match_node_ids_ = std::move(node_ids);
  RebuildCurrentProjection(/*preserve_selection=*/true);
  return true;
}

void SidebarTreeViewModel::ClearSearchMatches() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!search_exact_match_node_ids_.has_value()) {
    return;
  }
  search_exact_match_node_ids_.reset();
  RebuildCurrentProjection(/*preserve_selection=*/true);
  // Preserve the pre-search selection when it is still valid, but repair a
  // stale identity if the underlying tree changed while the filter was open.
  NormalizeSelection();
}

void SidebarTreeViewModel::SetSearchContextGroups(
    std::vector<std::vector<base::Uuid>> context_groups) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::vector<base::Uuid>> normalized;
  normalized.reserve(context_groups.size());
  for (std::vector<base::Uuid>& group : context_groups) {
    std::unordered_set<base::Uuid, base::UuidHash> seen;
    std::vector<base::Uuid> valid_group;
    valid_group.reserve(group.size());
    for (const base::Uuid& node_id : group) {
      if (node_id.is_valid() && seen.insert(node_id).second) {
        valid_group.push_back(node_id);
      }
    }
    if (valid_group.size() >= 2) {
      normalized.push_back(std::move(valid_group));
    }
  }
  if (search_context_groups_ == normalized) {
    return;
  }
  search_context_groups_ = std::move(normalized);
  if (search_exact_match_node_ids_.has_value()) {
    RebuildCurrentProjection(/*preserve_selection=*/true);
  }
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
  if (search_exact_match_node_ids_.has_value()) {
    return;
  }
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

bool SidebarTreeViewModel::IsSearchExactMatch(const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return search_exact_match_node_ids_.has_value() &&
         search_exact_match_node_ids_->contains(node_id);
}

bool SidebarTreeViewModel::IsSearchContext(const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return search_exact_match_node_ids_.has_value() &&
         !search_exact_match_node_ids_->contains(node_id) &&
         row_by_id_.contains(node_id);
}

bool SidebarTreeViewModel::IsSearchMatch(const base::Uuid& node_id) const {
  return IsSearchExactMatch(node_id);
}

std::vector<base::Uuid> SidebarTreeViewModel::GetSearchExactMatches() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!search_exact_match_node_ids_.has_value()) {
    return {};
  }
  return {search_exact_match_node_ids_->begin(),
          search_exact_match_node_ids_->end()};
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

void SidebarTreeViewModel::IncludeLoadedSearchChain(
    const base::Uuid& leaf_id,
    std::unordered_set<base::Uuid, base::UuidHash>* included) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(included);
  CHECK(workspace_id_.has_value());
  std::vector<base::Uuid> chain;
  std::unordered_set<base::Uuid, base::UuidHash> visited;
  std::optional<base::Uuid> cursor = leaf_id;
  while (cursor.has_value() && visited.insert(*cursor).second) {
    const auto node = nodes_.find(*cursor);
    if (node == nodes_.end() ||
        node->second.node.workspace_id != *workspace_id_ ||
        node->second.node.tombstone) {
      return;
    }
    chain.push_back(*cursor);
    cursor = node->second.node.parent_id;
  }
  if (!cursor.has_value()) {
    included->insert(chain.begin(), chain.end());
  }
}

std::vector<SidebarTreeViewModel::Row>
SidebarTreeViewModel::BuildSearchProjection() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(search_exact_match_node_ids_.has_value());

  std::unordered_set<base::Uuid, base::UuidHash> included;
  included.reserve(search_exact_match_node_ids_->size() * 2U);

  for (const base::Uuid& match_id : *search_exact_match_node_ids_) {
    IncludeLoadedSearchChain(match_id, &included);
  }
  for (const std::vector<base::Uuid>& group : search_context_groups_) {
    bool contains_match = false;
    for (const base::Uuid& node_id : group) {
      const auto node = nodes_.find(node_id);
      if (search_exact_match_node_ids_->contains(node_id) &&
          node != nodes_.end() &&
          node->second.node.type == tab_tree::TreeNodeType::kSavedPage) {
        contains_match = true;
        break;
      }
    }
    if (!contains_match) {
      continue;
    }
    for (const base::Uuid& context_id : group) {
      const auto context = nodes_.find(context_id);
      if (context != nodes_.end() &&
          context->second.node.type == tab_tree::TreeNodeType::kSavedPage) {
        IncludeLoadedSearchChain(context_id, &included);
      }
    }
  }

  struct PendingRow {
    base::Uuid node_id;
    size_t depth;
    size_t position_in_parent;
    size_t sibling_count;
  };
  std::vector<PendingRow> pending;
  pending.reserve(root_children_.size());
  for (size_t index = root_children_.size(); index > 0; --index) {
    if (included.contains(root_children_[index - 1])) {
      pending.push_back(
          {root_children_[index - 1], 0, index, root_children_.size()});
    }
  }

  std::vector<Row> projection;
  projection.reserve(included.size());
  std::unordered_set<base::Uuid, base::UuidHash> visited;
  while (!pending.empty()) {
    PendingRow current = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(current.node_id).second) {
      continue;
    }
    const auto node = nodes_.find(current.node_id);
    if (node == nodes_.end()) {
      continue;
    }
    bool has_projected_child = false;
    if (node->second.children_loaded) {
      has_projected_child = std::ranges::any_of(
          node->second.children, [&included](const base::Uuid& child_id) {
            return included.contains(child_id);
          });
    }
    projection.push_back({.node_id = current.node_id,
                          .depth = current.depth,
                          .position_in_parent = current.position_in_parent,
                          .sibling_count = current.sibling_count,
                          .type = node->second.node.type,
                          .expanded = node->second.node.type ==
                                          tab_tree::TreeNodeType::kFolder &&
                                      has_projected_child});
    if (!node->second.children_loaded) {
      continue;
    }
    for (size_t index = node->second.children.size(); index > 0; --index) {
      const base::Uuid& child_id = node->second.children[index - 1];
      if (included.contains(child_id)) {
        pending.push_back(
            {child_id, current.depth + 1, index, node->second.children.size()});
      }
    }
  }
  return projection;
}

void SidebarTreeViewModel::RebuildCurrentProjection(bool preserve_selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<Row> replacement =
      search_exact_match_node_ids_.has_value()
          ? BuildSearchProjection()
          : BuildVisibleChildren(root_children_, /*depth=*/0);
  ApplyVisibleSplice(/*first_row=*/0, rows_.size(), std::move(replacement),
                     preserve_selection);
}

void SidebarTreeViewModel::ApplyVisibleSplice(size_t first_row,
                                              size_t old_count,
                                              std::vector<Row> replacement,
                                              bool preserve_selection) {
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
  if (update_depth_ == 0 && !preserve_selection) {
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
