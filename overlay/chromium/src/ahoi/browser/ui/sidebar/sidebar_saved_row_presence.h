// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SAVED_ROW_PRESENCE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SAVED_ROW_PRESENCE_H_

#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::sidebar {

// Runtime facts about one tree page that the store itself does not carry.
struct SavedRowRuntimeFacts {
  // A local tab (in any window) is bound to the node.
  bool has_live_tab = false;
  // The archive owns the node; it is presented there, not here.
  bool archived = false;
  // Sync provenance names another device as the page's creator.
  bool created_on_other_device = false;
  // A restored archive entry lists the page. Restore exposes the retained
  // rows unloaded; opening one rebuilds its split.
  bool restored_from_archive = false;
};

// Whether the "Saved tabs" tree must leave out this node's row.
//
// A temporary page this device created and whose tab no longer exists is a
// closed temporary tab (for example after an unclean exit that was not
// restored). It is not a saved page: activating it would reopen it as a
// temporary tab in "Open tabs", so the row would seem to vanish on click.
// Such a row is hidden until a restored tab binds it again. A page that an
// archive restore brought back is not such an orphan and stays. Saved pages are
// never hidden: closing their tab only releases the live WebContents. A
// temporary page from another device stays visible with its origin, as the
// shared workspace tab structure requires.
bool ShouldHideClosedTemporaryPageRow(const tab_tree::TreeNode& node,
                                      const SavedRowRuntimeFacts& facts);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SAVED_ROW_PRESENCE_H_
