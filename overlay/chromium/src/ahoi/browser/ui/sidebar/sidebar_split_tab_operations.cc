// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::sidebar {
namespace {

split_tabs::SplitTabVisualData VisualDataForRemainingPanes(
    size_t remaining_count,
    const split_tabs::SplitTabVisualData& previous) {
  if (remaining_count == 3u) {
    return split_tabs::SplitTabVisualData::ForThreePane(previous.split_layout(),
                                                        previous.arrangement());
  }
  return split_tabs::SplitTabVisualData(previous.split_layout());
}

}  // namespace

bool ExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                            tabs::TabInterface* source) {
  if (!tab_strip_model || !source ||
      tab_strip_model->GetIndexOfTab(source) < 0 ||
      !source->GetSplit().has_value()) {
    return false;
  }

  const split_tabs::SplitTabId split_id = *source->GetSplit();
  const split_tabs::SplitTabData* split_data =
      tab_strip_model->GetSplitData(split_id);
  if (!split_data || !split_data->visual_data()) {
    return false;
  }
  const std::vector<tabs::TabInterface*> panes = split_data->ListTabs();
  if (panes.size() < 2u || panes.size() > tabs::SplitTabCollection::kMaxTabs ||
      std::ranges::find(panes, source) == panes.end()) {
    return false;
  }

  const split_tabs::SplitTabVisualData previous_visual_data =
      *split_data->visual_data();
  std::vector<tabs::TabInterface*> remaining;
  remaining.reserve(panes.size() - 1u);
  for (tabs::TabInterface* pane : panes) {
    if (!pane || tab_strip_model->GetIndexOfTab(pane) < 0) {
      return false;
    }
    if (pane != source) {
      remaining.push_back(pane);
    }
  }

  // Both mutations operate solely on TabInterface membership. The strong
  // pointers above let us prove that no close, navigation or replacement
  // enters this extraction path.
  tab_strip_model->RemoveSplit(split_id);
  if (remaining.size() >= 2u) {
    std::vector<int> remaining_indices;
    remaining_indices.reserve(remaining.size());
    for (tabs::TabInterface* pane : remaining) {
      remaining_indices.push_back(tab_strip_model->GetIndexOfTab(pane));
    }
    std::ranges::sort(remaining_indices);
    tab_strip_model->RestoreSplit(
        split_id, remaining_indices,
        VisualDataForRemainingPanes(remaining.size(), previous_visual_data));
  }

  if (source->GetSplit().has_value()) {
    return false;
  }
  if (remaining.size() < 2u) {
    return remaining.empty() || !remaining.front()->GetSplit().has_value();
  }
  return std::ranges::all_of(remaining, [split_id](tabs::TabInterface* pane) {
    const std::optional<split_tabs::SplitTabId> pane_split = pane->GetSplit();
    return pane_split.has_value() && *pane_split == split_id;
  });
}

}  // namespace ahoi::sidebar
