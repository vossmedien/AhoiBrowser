// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "base/check.h"
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
  std::vector<int> remaining_indices;
  remaining_indices.reserve(panes.size() - 1u);
  for (tabs::TabInterface* pane : panes) {
    const int pane_index = pane ? tab_strip_model->GetIndexOfTab(pane) : -1;
    if (pane_index < 0) {
      return false;
    }
    if (pane != source) {
      remaining.push_back(pane);
      remaining_indices.push_back(pane_index);
    }
  }
  std::ranges::sort(remaining_indices);

  // Complete every fallible lookup before the first mutation. Both operations
  // below are synchronous TabStripModel membership mutations with hard input
  // contracts, so returning false can never expose a partially updated split.
  tab_strip_model->RemoveSplit(split_id);
  if (remaining.size() >= 2u) {
    tab_strip_model->RestoreSplit(
        split_id, remaining_indices,
        VisualDataForRemainingPanes(remaining.size(), previous_visual_data));
  }

  CHECK(!source->GetSplit().has_value());
  if (remaining.size() < 2u) {
    CHECK(remaining.empty() || !remaining.front()->GetSplit().has_value());
  } else {
    const bool remainder_restored =
        std::ranges::all_of(remaining, [split_id](tabs::TabInterface* pane) {
          const std::optional<split_tabs::SplitTabId> pane_split =
              pane->GetSplit();
          return pane_split.has_value() && *pane_split == split_id;
        });
    CHECK(remainder_restored);
  }
  return true;
}

}  // namespace ahoi::sidebar
