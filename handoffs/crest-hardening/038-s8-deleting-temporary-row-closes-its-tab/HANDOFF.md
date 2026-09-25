# 038 – S8: deleting a temporary row closes its tab

Status: integrated 72109fc (default body moved out of line; build 32; visible check pending)
Owner lane: desktop (apply, build, test)
Base: HEAD `604ef57` (`git apply --check` passes on HEAD and on the current
worktree; independent of 028–036). Implementation of 011 S8 by the
crest-hardening lane; not compiled by the lane.

## Defect (checked in source)

A temporary row is a tree node bound to a live tab. "In den Papierkorb"
(`kDeleteNode`) and the Delete key only tombstone the node. The bridge's
tree observer then unbinds the live tab (`session_bridge_observers.cc:563-573`),
`UnbindTreeNodeFromTabInternal` gives it a fresh `pending_node_id` and
schedules a binding (`session_bridge_workspace.cc:423-433`), and
`CreateTemporaryNodeForTab` recreates the row under the new id. The row
reappears and Sync sees a tombstone and a new creation. This is an observer
writing in reaction to a self-caused change (ARCHITECTURE "Single writer").
For saved rows the fallback to a temporary tab is intended and stays.

## Change

The operation decides, not the observer:

- `SidebarTreeViewDelegate::CloseTemporaryPageForDeletion(node_id)`
  (default `false`).
- `BrowserSidebarHostView` implements it: a temporary saved-page row with a
  live tab closes that tab (`tab->Close()`, before-unload honoured) and
  reports it handled. The existing tab-close path
  (`ScheduleTemporaryPageClose` → `DeleteClosedTemporaryPage`) removes the
  row with its original id.
- `kDeleteNode` and the tree view's Delete/Backspace key ask the delegate
  first and delete the node only when it did not handle it. The trailing
  row action already closed live tabs first.

Not changed: remote deletions (the sync path sets
`shared_binding_invalidated`, so no recreation) and folder deletion that
contains temporary rows with live tabs, if such a tree can exist; check
that in the visible journey.

## Tests

- `SidebarTreeViewTest.DeleteKeyLetsHostCloseTemporaryPage` and
  `.DeleteKeyDeletesRowTheHostDoesNotClose` in
  `sidebar_tree_view_interaction_unittest.cc`, with the recording delegate
  extended.
- Visible journey: open a page as a temporary tab, delete its row by context
  menu and by the Delete key. Expected: the tab closes and no new row
  appears. With a before-unload page and "Stay", tab and row stay with the
  same id. Deleting a saved row with an open tab still leaves a temporary
  tab (intended).
