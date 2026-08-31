// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ahoi::sidebar {

void SidebarTreeView::UpdateActiveDescendant() {
  if (!model().selected_node_id().has_value()) {
    GetViewAccessibility().ClearActiveDescendant();
    return;
  }
  SidebarTreeRowView* selected =
      GetMaterializedRowForTesting(*model().selected_node_id());
  if (selected) {
    GetViewAccessibility().SetActiveDescendant(*selected);
  } else {
    GetViewAccessibility().ClearActiveDescendant();
  }
}

void SidebarTreeView::EnsureRowVisible(size_t row_index) {
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::vector<VisualPosition> positions =
      BuildVisualPositions(visual_rows);
  if (row_index >= positions.size() || !positions[row_index].present) {
    return;
  }
  const VisualRow& visual_row = visual_rows[positions[row_index].visual_row];
  ScrollRectToVisible(
      gfx::Rect(0, visual_row.y, std::max(width(), 1), visual_row.height));
  SynchronizeRows(GetVisibleBounds());
}

void SidebarTreeView::SelectRow(size_t row_index) {
  if (row_index >= model().rows().size() ||
      runtime_composite_suppressed_nodes_.contains(
          model().rows()[row_index].node_id)) {
    return;
  }
  std::ignore = controller_->SelectNode(model().rows()[row_index].node_id);
  EnsureRowVisible(row_index);
}

void SidebarTreeView::SelectRelativeRow(int delta) {
  const auto& rows = model().rows();
  if (rows.empty()) {
    return;
  }
  if (!model().selected_node_id().has_value()) {
    size_t target = delta < 0 ? rows.size() : 0;
    while (delta < 0 && target > 0) {
      --target;
      if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
        SelectRow(target);
        return;
      }
    }
    while (delta >= 0 && target < rows.size()) {
      if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
        SelectRow(target);
        return;
      }
      ++target;
    }
    return;
  }
  const std::optional<size_t> selected =
      model().GetRowForNode(*model().selected_node_id());
  if (!selected.has_value()) {
    SelectRow(0);
    return;
  }
  size_t target = *selected;
  while (delta < 0 && target > 0) {
    --target;
    if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
      SelectRow(target);
      return;
    }
  }
  while (delta >= 0 && target + 1 < rows.size()) {
    ++target;
    if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
      SelectRow(target);
      return;
    }
  }
}

void SidebarTreeView::CollapseOrSelectParent() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid selected_id = *model().selected_node_id();
  const std::optional<size_t> selected_index =
      model().GetRowForNode(selected_id);
  if (!selected_index.has_value()) {
    return;
  }
  const auto& row = model().rows()[*selected_index];
  if (row.type == tab_tree::TreeNodeType::kFolder && row.expanded &&
      !model().is_search_projection_active()) {
    std::ignore = controller_->CollapseNode(selected_id);
    return;
  }
  if (row.depth == 0) {
    return;
  }
  for (size_t index = *selected_index; index > 0; --index) {
    if (model().rows()[index - 1].depth < row.depth) {
      SelectRow(index - 1);
      return;
    }
  }
}

void SidebarTreeView::ExpandOrSelectChild() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid selected_id = *model().selected_node_id();
  const std::optional<size_t> selected_index =
      model().GetRowForNode(selected_id);
  if (!selected_index.has_value()) {
    return;
  }
  const auto& row = model().rows()[*selected_index];
  if (row.type != tab_tree::TreeNodeType::kFolder) {
    return;
  }
  if (!row.expanded) {
    const auto result = controller_->ExpandNode(selected_id);
    if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
      delegate_->OnMutationFailed(result);
    }
    return;
  }
  for (size_t index = *selected_index + 1; index < model().rows().size();
       ++index) {
    const auto& child = model().rows()[index];
    if (child.depth <= row.depth) {
      return;
    }
    if (child.depth == row.depth + 1 &&
        !runtime_composite_suppressed_nodes_.contains(child.node_id)) {
      SelectRow(index);
      return;
    }
  }
}

void SidebarTreeView::ActivateSelectedNode() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid node_id = *model().selected_node_id();
  const tab_tree::TreeNode* node = model().GetNode(node_id);
  if (!node) {
    return;
  }
  if (node->type == tab_tree::TreeNodeType::kFolder) {
    if (model().is_search_projection_active()) {
      if (delegate_) {
        delegate_->ActivateFolderSearchResult(*node);
      }
      return;
    }
    if (model().IsExpanded(node_id)) {
      std::ignore = controller_->CollapseNode(node_id);
    } else {
      const auto result = controller_->ExpandNode(node_id);
      if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
        delegate_->OnMutationFailed(result);
      }
    }
  } else if (delegate_) {
    delegate_->ActivateSavedPage(*node);
  }
}

std::optional<SidebarTreeView::VisualHit> SidebarTreeView::FindVisualHit(
    const std::vector<VisualRow>& visual_rows,
    const gfx::Point& point) const {
  if (point.y() < 0) {
    return std::nullopt;
  }
  const std::optional<size_t> visual_index =
      FindVisualRowAtY(visual_rows, point.y());
  if (!visual_index.has_value()) {
    return std::nullopt;
  }
  const VisualRow& visual_row = visual_rows[*visual_index];
  size_t closest_segment = 0;
  int closest_distance = std::numeric_limits<int>::max();
  for (size_t segment = 0; segment < visual_row.model_indices.size();
       ++segment) {
    const gfx::Rect bounds =
        GetSegmentBounds(visual_row, segment, std::max(width(), 1));
    if (bounds.Contains(point)) {
      closest_segment = segment;
      break;
    }
    const int distance = std::abs(point.x() - bounds.CenterPoint().x());
    if (distance < closest_distance) {
      closest_distance = distance;
      closest_segment = segment;
    }
  }
  return VisualHit{.visual_row = *visual_index,
                   .model_index = visual_row.model_indices[closest_segment],
                   .bounds = GetSegmentBounds(visual_row, closest_segment,
                                              std::max(width(), 1))};
}

std::optional<base::Uuid> SidebarTreeView::NodeAtPoint(
    const gfx::Point& point) const {
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::optional<VisualHit> hit = FindVisualHit(visual_rows, point);
  if (!hit.has_value() || hit->model_index >= model().rows().size()) {
    return std::nullopt;
  }
  return model().rows()[hit->model_index].node_id;
}

}  // namespace ahoi::sidebar
