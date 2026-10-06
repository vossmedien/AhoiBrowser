// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"

#include <algorithm>

#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "base/containers/contains.h"
#include "ui/events/event.h"

namespace ahoi::sidebar {

std::vector<base::Uuid> SidebarTreeView::multi_selection() const {
  // Row order, so actions keep the visual order; rows hidden by a collapsed
  // folder or removed by another device drop out.
  std::vector<base::Uuid> ordered;
  for (const SidebarTreeViewModel::Row& row : model().rows()) {
    if (base::Contains(multi_selected_, row.node_id) &&
        !base::Contains(ordered, row.node_id)) {
      ordered.push_back(row.node_id);
    }
  }
  return ordered;
}

bool SidebarTreeView::IsMultiSelected(const base::Uuid& node_id) const {
  return base::Contains(multi_selected_, node_id);
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
  const std::optional<base::Uuid>& selected = model().selected_node_id();
  // As in Finder, the first ⌘-/⇧-click adds to the row that is already
  // selected.
  if (multi_selected_.empty() && selected.has_value() && *selected != node_id) {
    multi_selected_.push_back(*selected);
  }
  const std::optional<base::Uuid> anchor =
      multi_anchor_.has_value() ? multi_anchor_ : selected;
  const std::optional<size_t> from =
      extend && anchor.has_value() ? model().GetRowForNode(*anchor)
                                   : std::nullopt;
  const std::optional<size_t> to = model().GetRowForNode(node_id);
  if (from.has_value() && to.has_value()) {
    if (!toggle) {
      multi_selected_.clear();
    }
    const auto& rows = model().rows();
    for (size_t index = std::min(*from, *to);
         index <= std::max(*from, *to) && index < rows.size(); ++index) {
      if (!base::Contains(multi_selected_, rows[index].node_id)) {
        multi_selected_.push_back(rows[index].node_id);
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
  const std::optional<base::Uuid>& selected = model().selected_node_id();
  for (const SidebarTreeViewModel::Row& row : model().rows()) {
    if (SidebarTreeRowView* view = GetMaterializedRowForTesting(row.node_id)) {
      view->SetSelected(selected == row.node_id ||
                        IsMultiSelected(row.node_id));
    }
  }
}

}  // namespace ahoi::sidebar
