// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "base/check.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_collection.h"
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

std::optional<SplitTabExtractionSnapshot> CaptureSplitTabExtractionSnapshot(
    TabStripModel* tab_strip_model,
    tabs::TabInterface* source) {
  if (!CanExtractTabFromSplitPreservingRemainder(tab_strip_model, source)) {
    return std::nullopt;
  }
  const split_tabs::SplitTabData* const split_data =
      tab_strip_model->GetSplitData(*source->GetSplit());
  if (!split_data || !split_data->visual_data()) {
    return std::nullopt;
  }
  SplitTabExtractionSnapshot snapshot{
      .split_id = *source->GetSplit(),
      .visual_data = *split_data->visual_data()};
  for (tabs::TabInterface* pane : split_data->ListTabs()) {
    if (!pane || tab_strip_model->GetIndexOfTab(pane) < 0) {
      return std::nullopt;
    }
    snapshot.member_handles.push_back(pane->GetHandle().raw_value());
  }
  return snapshot;
}

bool RestoreSplitTabExtraction(TabStripModel* tab_strip_model,
                               const SplitTabExtractionSnapshot& snapshot) {
  if (!tab_strip_model || snapshot.member_handles.size() < 2u ||
      snapshot.member_handles.size() > tabs::SplitTabCollection::kMaxTabs) {
    return false;
  }

  const auto find_member = [tab_strip_model](int handle) {
    for (tabs::TabInterface* candidate : *tab_strip_model) {
      if (candidate && candidate->GetHandle().raw_value() == handle) {
        return candidate;
      }
    }
    return static_cast<tabs::TabInterface*>(nullptr);
  };

  // Fail before the first mutation if an original member disappeared, joined
  // another split, or an unrelated tab entered the remainder split. Removing
  // such a split would corrupt state outside this transaction.
  for (int handle : snapshot.member_handles) {
    tabs::TabInterface* const member = find_member(handle);
    if (!member || tab_strip_model->GetIndexOfTab(member) < 0 ||
        (member->GetSplit().has_value() &&
         member->GetSplit() != snapshot.split_id)) {
      return false;
    }
  }
  if (tab_strip_model->ContainsSplit(snapshot.split_id)) {
    const split_tabs::SplitTabData* const current_split =
        tab_strip_model->GetSplitData(snapshot.split_id);
    if (!current_split) {
      return false;
    }
    for (tabs::TabInterface* member : current_split->ListTabs()) {
      if (!member || std::ranges::find(snapshot.member_handles,
                                       member->GetHandle().raw_value()) ==
                         snapshot.member_handles.end()) {
        return false;
      }
    }
    tab_strip_model->RemoveSplit(snapshot.split_id);
  }

  // RemoveSplit completes a model transaction and may synchronously notify
  // observers. Never reuse pointers or indices captured before it.
  std::vector<int> indices;
  indices.reserve(snapshot.member_handles.size());
  for (int handle : snapshot.member_handles) {
    tabs::TabInterface* const member = find_member(handle);
    const int index = member ? tab_strip_model->GetIndexOfTab(member) : -1;
    if (!member || index < 0 || member->GetSplit().has_value()) {
      return false;
    }
    indices.push_back(index);
  }
  std::ranges::sort(indices);
  if (std::ranges::adjacent_find(indices) != indices.end()) {
    return false;
  }
  tab_strip_model->RestoreSplit(snapshot.split_id, indices,
                                snapshot.visual_data);
  if (!tab_strip_model->ContainsSplit(snapshot.split_id)) {
    return false;
  }
  for (size_t index = 0; index < snapshot.member_handles.size(); ++index) {
    const split_tabs::SplitTabData* const split_data =
        tab_strip_model->GetSplitData(snapshot.split_id);
    if (!split_data) {
      return false;
    }
    const std::vector<tabs::TabInterface*> current = split_data->ListTabs();
    if (current.size() != snapshot.member_handles.size() || !current[index]) {
      return false;
    }
    if (current[index]->GetHandle().raw_value() ==
        snapshot.member_handles[index]) {
      continue;
    }
    tabs::TabInterface* const desired =
        find_member(snapshot.member_handles[index]);
    if (!desired ||
        !tab_strip_model->ReorderTabInSplit(desired, current[index])) {
      return false;
    }
  }
  const split_tabs::SplitTabData* const restored =
      tab_strip_model->GetSplitData(snapshot.split_id);
  if (!restored || !restored->visual_data() ||
      *restored->visual_data() != snapshot.visual_data) {
    return false;
  }
  const std::vector<tabs::TabInterface*> restored_members =
      restored->ListTabs();
  if (restored_members.size() != snapshot.member_handles.size()) {
    return false;
  }
  for (size_t index = 0; index < restored_members.size(); ++index) {
    if (!restored_members[index] ||
        restored_members[index]->GetHandle().raw_value() !=
            snapshot.member_handles[index]) {
      return false;
    }
  }
  return true;
}

bool CanExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                               tabs::TabInterface* source) {
  if (!tab_strip_model || !source ||
      tab_strip_model->GetIndexOfTab(source) < 0 ||
      !source->GetSplit().has_value()) {
    return false;
  }
  const split_tabs::SplitTabData* const split_data =
      tab_strip_model->GetSplitData(*source->GetSplit());
  if (!split_data || !split_data->visual_data()) {
    return false;
  }
  const std::vector<tabs::TabInterface*> panes = split_data->ListTabs();
  if (panes.size() < 2u || panes.size() > tabs::SplitTabCollection::kMaxTabs ||
      std::ranges::find(panes, source) == panes.end()) {
    return false;
  }
  return std::ranges::all_of(
      panes, [tab_strip_model](tabs::TabInterface* pane) {
        return pane && tab_strip_model->GetIndexOfTab(pane) >= 0;
      });
}

bool ExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                            tabs::TabInterface* source) {
  if (!CanExtractTabFromSplitPreservingRemainder(tab_strip_model, source)) {
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
