// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <optional>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::sidebar {

bool BrowserSidebarHostView::ResizeSavedPageSplit(
    const std::vector<base::Uuid>& node_ids,
    size_t divider_index,
    double ratio,
    bool done_resizing) {
  const auto reject = [this, done_resizing]() {
    if (done_resizing) {
      sidebar_split_resize_active_ = false;
      MaybeScheduleDeferredRuntimePresentationRefresh();
    }
    return false;
  };
  if (node_ids.size() < 2 || !session_bridge_) {
    return reject();
  }
  tabs::TabInterface* first =
      session_bridge_->FindTabByTreeNodeId(node_ids.front());
  if (!first || !first->GetSplit().has_value() ||
      session_bridge_->FindTabStripModelForTab(first) != tab_strip_model_) {
    return reject();
  }
  const split_tabs::SplitTabData* split_data =
      tab_strip_model_->GetSplitData(*first->GetSplit());
  if (!split_data || split_data->ListTabs().size() != node_ids.size() ||
      divider_index >= node_ids.size() - 1u) {
    return reject();
  }
  const std::vector<tabs::TabInterface*> split_tabs = split_data->ListTabs();
  for (size_t index = 0; index < split_tabs.size(); ++index) {
    if (session_bridge_->FindTreeNodeIdForTab(split_tabs[index]) !=
        std::optional<base::Uuid>(node_ids[index])) {
      return reject();
    }
  }
  return ResizeSidebarSplit(*first->GetSplit(), divider_index, ratio,
                            done_resizing);
}

bool BrowserSidebarHostView::ResizeSidebarSplit(split_tabs::SplitTabId split_id,
                                                size_t divider_index,
                                                double ratio,
                                                bool done_resizing) {
  if (!tab_strip_model_ || !tab_strip_model_->ContainsSplit(split_id)) {
    if (done_resizing) {
      sidebar_split_resize_active_ = false;
      MaybeScheduleDeferredRuntimePresentationRefresh();
    }
    return false;
  }
  const split_tabs::SplitTabData* split_data =
      tab_strip_model_->GetSplitData(split_id);
  if (!split_data || divider_index >= split_data->ListTabs().size() - 1u ||
      !split_tabs::SplitTabVisualData::IsValidRatio(ratio)) {
    if (done_resizing) {
      sidebar_split_resize_active_ = false;
      MaybeScheduleDeferredRuntimePresentationRefresh();
    }
    return false;
  }

  // TabStripModel notifies observers synchronously. Hold the rebuild guard for
  // both intermediate and final writes, then schedule the final presentation
  // only after ResizeArea has returned from its captured mouse event.
  if (!done_resizing) {
    sidebar_split_resize_active_ = true;
  }
  sidebar_split_resize_update_in_progress_ = true;
  tab_strip_model_->UpdateSplitRatio(split_id, divider_index, ratio,
                                     /*is_intermediate=*/!done_resizing);
  sidebar_split_resize_update_in_progress_ = false;
  if (done_resizing) {
    sidebar_split_resize_active_ = false;
    if (tree_view_) {
      tree_view_->OnSplitGroupsChanged();
    }
    MaybeScheduleDeferredRuntimePresentationRefresh();
    ScheduleRuntimePresentationRefresh();
  }
  return true;
}

}  // namespace ahoi::sidebar
