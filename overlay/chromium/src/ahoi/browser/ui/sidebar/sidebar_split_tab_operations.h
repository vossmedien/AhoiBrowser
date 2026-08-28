// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_

#include <optional>
#include <vector>

#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace ahoi::sidebar {

struct SplitTabExtractionSnapshot {
  split_tabs::SplitTabId split_id;
  split_tabs::SplitTabVisualData visual_data;
  std::vector<int> member_handles;
};

std::optional<SplitTabExtractionSnapshot> CaptureSplitTabExtractionSnapshot(
    TabStripModel* tab_strip_model,
    tabs::TabInterface* source);

// Restores the exact split id, layout and pane order captured before an
// extraction. This is used only for transaction rollback while every original
// WebContents remains in the same TabStripModel.
bool RestoreSplitTabExtraction(TabStripModel* tab_strip_model,
                               const SplitTabExtractionSnapshot& snapshot);

// Performs every fallible identity, membership and split-shape lookup without
// mutating TabStripModel. Callers that must update a durable model first use
// this as their transaction planning boundary.
bool CanExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                               tabs::TabInterface* source);

// Extracts exactly `source` from its Chromium split without replacing or
// reloading any WebContents. For three/four-pane collections the remaining
// panes are restored under the same split id and orientation; a two-pane
// collection naturally leaves one ordinary tab. Every false return occurs
// before mutation when an identity or split precondition is stale.
bool ExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                            tabs::TabInterface* source);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_
