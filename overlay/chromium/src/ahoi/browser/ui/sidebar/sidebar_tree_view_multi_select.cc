// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"

#include <algorithm>

#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ui/events/event.h"

namespace ahoi::sidebar {

std::vector<base::Uuid> SidebarTreeView::visible_node_order() const {
  std::vector<base::Uuid> order;
  for (const VisualRow& visual : BuildVisualRows()) {
    for (size_t index : visual.model_indices) {
      order.push_back(model().rows()[index].node_id);
    }
  }
  return order;
}

std::vector<base::Uuid> SidebarTreeView::multi_selection() const {
  // Row order, so actions keep the visual order; rows hidden by a collapsed
  // folder or removed by another device drop out.
  std::vector<base::Uuid> ordered;
  const std::vector<base::Uuid> order =
      delegate_ ? delegate_->GetMultiSelectionRowOrder() : std::vector<base::Uuid>();
  for (const base::Uuid& id : order.empty() ? visible_node_order() : order) {
    if (std::ranges::contains(multi_selected_, id) &&
        !std::ranges::contains(ordered, id)) {
      ordered.push_back(id);
    }
  }
  return ordered;
}

bool SidebarTreeView::IsMultiSelected(const base::Uuid& node_id) const {
  return std::ranges::contains(multi_selected_, node_id);
}

void SidebarTreeView::ClearMultiSelection() {
  if (multi_selected_.empty() && !multi_anchor_.has_value()) {
    return;
  }
  multi_selected_.clear();
  multi_anchor_.reset();
  RefreshMultiSelectedRows();
}

bool SidebarTreeView::HandleMultiSelectClick(const base::Uuid& node_id,
                                             const ui::MouseEvent& event) {
  const bool toggle = event.IsCommandDown();
  const bool extend = event.IsShiftDown();
  if (!toggle && !extend) {
    ClearMultiSelection();
    return false;
  }
  if (model().is_search_projection_active()) {
    return false;
  }
  auto order = delegate_ ? delegate_->GetMultiSelectionRowOrder()
                         : std::vector<base::Uuid>();
  if (order.empty()) {
    order = visible_node_order();
  }
  if (!std::ranges::contains(order, node_id)) {
    return false;
  }
  const auto active = delegate_ ? delegate_->GetMultiSelectionActiveNode()
                                 : std::nullopt;
  const auto selected = active.has_value() ? active : model().selected_node_id();
  // As in Finder, the first ⌘-/⇧-click adds to the row that is already
  // selected.
  if (multi_selected_.empty() && selected.has_value() && *selected != node_id &&
      std::ranges::contains(order, *selected)) {
    multi_selected_.push_back(*selected);
  }
  const std::optional<base::Uuid> anchor =
      multi_anchor_.has_value() ? multi_anchor_ : selected;
  const auto from = extend && anchor.has_value()
                        ? std::ranges::find(order, *anchor) : order.end();
  const auto to = std::ranges::find(order, node_id);
  if (from != order.end()) {
    if (!toggle) {
      multi_selected_.clear();
    }
    for (auto it = std::min(from, to); it <= std::max(from, to); ++it) {
      if (!std::ranges::contains(multi_selected_, *it)) {
        multi_selected_.push_back(*it);
      }
    }
  } else if (auto it = std::ranges::find(multi_selected_, node_id);
             it != multi_selected_.end()) {
    multi_selected_.erase(it);
    multi_anchor_ = node_id;
  } else {
    multi_selected_.push_back(node_id);
    multi_anchor_ = node_id;
  }
  RefreshMultiSelectedRows();
  return true;
}

void SidebarTreeView::RefreshMultiSelectedRows() {
  for (const SidebarTreeViewModel::Row& row : model().rows()) {
    if (SidebarTreeRowView* view = GetMaterializedRowForTesting(row.node_id)) {
      view->SetMultiSelected(IsMultiSelected(row.node_id));
    }
  }
  if (delegate_) {
    delegate_->OnMultiSelectionChanged();
  }
}

}  // namespace ahoi::sidebar
