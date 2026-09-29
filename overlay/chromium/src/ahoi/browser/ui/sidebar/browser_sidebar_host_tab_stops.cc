// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_stops.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_model.h"
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::sidebar {

namespace {

// Guards the parent walk against a corrupt cycle in the stored tree.
constexpr size_t kMaxFolderDepth = 1024u;

// Returns the visible row of `node_id` or, for a page hidden in a collapsed
// folder, the row of its nearest visible ancestor folder.
std::optional<size_t> FindShownRow(const SidebarTreeViewModel& model,
                                   SessionBridge& bridge,
                                   const base::Uuid& node_id,
                                   bool* hidden) {
  *hidden = false;
  std::optional<base::Uuid> current = node_id;
  for (size_t depth = 0; current.has_value() && depth < kMaxFolderDepth;
       ++depth) {
    if (const std::optional<size_t> row = model.GetRowForNode(*current)) {
      return row;
    }
    *hidden = true;
    // Children of a collapsed folder are loaded lazily, so the durable store
    // answers for nodes the view model has not cached.
    if (const tab_tree::TreeNode* cached = model.GetNode(*current)) {
      current = cached->parent_id;
      continue;
    }
    tab_tree::TreeNode stored;
    if (!bridge.tab_tree_store() ||
        bridge.tab_tree_store()->GetNode(*current, &stored) !=
            tab_tree::TabTreeStore::Result::kOk) {
      return std::nullopt;
    }
    current = stored.parent_id;
  }
  return std::nullopt;
}

// Mirrors RefreshRuntimePresentation() and SidebarTreeView::BuildVisualRows():
// saved rows (and all-saved splits) in tree order, then temporary rows and
// splits with a temporary pane in tab strip order.
SidebarTabStops BuildSidebarTabStops(const SidebarTreeViewModel& model,
                                     SessionBridge& bridge,
                                     TabStripModel& tab_strip_model) {
  const std::optional<base::Uuid>& workspace = model.workspace_id();
  const auto is_workspace_tab = [&](tabs::TabInterface* tab) {
    if (!tab) {
      return false;
    }
    const std::optional<base::Uuid> tab_workspace =
        bridge.GetWorkspaceForTab(tab);
    return !workspace.has_value() || !tab_workspace.has_value() ||
           tab_workspace == workspace;
  };

  const std::vector<SidebarTreeViewModel::Row>& rows = model.rows();
  std::vector<std::vector<int>> tabs_by_row(rows.size());
  std::vector<bool> row_is_stop(rows.size(), false);
  std::vector<std::vector<int>> temporary_stops;
  std::set<split_tabs::SplitTabId> visited_splits;

  for (tabs::TabInterface* tab : tab_strip_model) {
    if (!is_workspace_tab(tab)) {
      continue;
    }
    std::vector<tabs::TabInterface*> unit = {tab};
    if (const std::optional<split_tabs::SplitTabId> split_id =
            tab->GetSplit()) {
      if (!visited_splits.insert(*split_id).second) {
        continue;
      }
      if (const split_tabs::SplitTabData* split_data =
              tab_strip_model.GetSplitData(*split_id)) {
        unit.clear();
        for (tabs::TabInterface* pane : split_data->ListTabs()) {
          if (is_workspace_tab(pane)) {
            unit.push_back(pane);
          }
        }
        if (unit.empty()) {
          continue;
        }
      }
    }

    std::vector<int> handles;
    std::vector<base::Uuid> saved_nodes;
    for (tabs::TabInterface* pane : unit) {
      handles.push_back(pane->GetHandle().raw_value());
      if (const std::optional<base::Uuid> node_id =
              bridge.FindTreeNodeIdForTab(pane)) {
        saved_nodes.push_back(*node_id);
      }
    }
    if (saved_nodes.size() < unit.size()) {
      // A temporary row, or a split with a temporary pane, which the host
      // presents in the temporary section.
      temporary_stops.push_back(std::move(handles));
      continue;
    }

    // A saved split is one row, placed like BuildVisualRows() anchors it:
    // at its deepest visible pane, the first one among equals.
    std::optional<size_t> anchor_row;
    std::optional<size_t> hidden_row;
    for (const base::Uuid& node_id : saved_nodes) {
      bool hidden = false;
      const std::optional<size_t> row =
          FindShownRow(model, bridge, node_id, &hidden);
      if (!row.has_value()) {
        continue;
      }
      if (hidden) {
        hidden_row = hidden_row.value_or(*row);
        continue;
      }
      if (!anchor_row.has_value() ||
          rows[*row].depth > rows[*anchor_row].depth ||
          (rows[*row].depth == rows[*anchor_row].depth &&
           *row < *anchor_row)) {
        anchor_row = row;
      }
    }
    const std::optional<size_t> row = anchor_row ? anchor_row : hidden_row;
    if (!row.has_value()) {
      // Not shown at all, for example outside an active search projection:
      // stepping from such a tab starts at the ends.
      continue;
    }
    row_is_stop[*row] = row_is_stop[*row] || anchor_row.has_value();
    std::vector<int>& row_tabs = tabs_by_row[*row];
    row_tabs.insert(row_tabs.end(), handles.begin(), handles.end());
  }

  SidebarTabStops stops;
  for (size_t row = 0; row < rows.size(); ++row) {
    if (row_is_stop[row]) {
      stops.AddStop(std::move(tabs_by_row[row]));
    } else {
      stops.AddHiddenTabs(std::move(tabs_by_row[row]));
    }
  }
  for (std::vector<int>& temporary_stop : temporary_stops) {
    stops.AddStop(std::move(temporary_stop));
  }
  return stops;
}

}  // namespace

base::WeakPtr<tabs::TabInterface>
BrowserSidebarHostView::ResolveRelativeRuntimeTab(int delta) const {
  if (!tab_strip_model_ || !session_bridge_ || !controller_ || delta == 0) {
    return nullptr;
  }
  const SidebarTabStops stops = BuildSidebarTabStops(
      controller_->view_model(), *session_bridge_, *tab_strip_model_);
  tabs::TabInterface* const active = tab_strip_model_->GetActiveTab();
  const std::optional<int> target = stops.Step(
      active ? std::optional<int>(active->GetHandle().raw_value())
             : std::nullopt,
      delta);
  tabs::TabInterface* const tab =
      target.has_value() ? FindRuntimeTab(*target) : nullptr;
  return tab ? tab->GetWeakPtr() : nullptr;
}

base::WeakPtr<tabs::TabInterface>
BrowserSidebarHostView::ResolveNumberedRuntimeTab(
    std::optional<size_t> index) const {
  if (!tab_strip_model_ || !session_bridge_ || !controller_) {
    return nullptr;
  }
  const SidebarTabStops stops = BuildSidebarTabStops(
      controller_->view_model(), *session_bridge_, *tab_strip_model_);
  tabs::TabInterface* const active = tab_strip_model_->GetActiveTab();
  const std::optional<int> active_handle =
      active ? std::optional<int>(active->GetHandle().raw_value())
             : std::nullopt;
  const std::optional<int> target = index.has_value()
                                        ? stops.Nth(active_handle, *index)
                                        : stops.Last(active_handle);
  tabs::TabInterface* const tab =
      target.has_value() ? FindRuntimeTab(*target) : nullptr;
  return tab ? tab->GetWeakPtr() : nullptr;
}

}  // namespace ahoi::sidebar
