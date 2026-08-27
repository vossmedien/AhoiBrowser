// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace ahoi::sidebar {

// Extracts exactly `source` from its Chromium split without replacing or
// reloading any WebContents. For three/four-pane collections the remaining
// panes are restored under the same split id and orientation; a two-pane
// collection naturally leaves one ordinary tab. Every false return occurs
// before mutation when an identity or split precondition is stale.
bool ExtractTabFromSplitPreservingRemainder(TabStripModel* tab_strip_model,
                                            tabs::TabInterface* source);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_TAB_OPERATIONS_H_
