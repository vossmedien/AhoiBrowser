# 032 – S6: the split record follows its members' Workspace

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `982aa3b` (`git apply --check` passes). Implementation of 011 S6
by the crest-hardening lane; not compiled by the lane.

## Change

`WorkspaceStructureController::CaptureSplits` adopted a new `workspace_id`
only together with an observed native split change
(`changed_native_splits_`). Moving a split's pages to another Workspace in
the tree changes no native split. The stored record therefore kept the old
Workspace, was synced and archived with it, and
`MaterializeNativeSplit` later rejected it as a Workspace mismatch.

The decision is now the pure function `ClassifySplitCapture`:

- `kNativeChange`: previous behaviour.
- `kWorkspaceOnly`: no native change, but every member is now in another
  Workspace while topology and ratios are unchanged. Only
  `record.workspace_id` follows, and the record is stamped as a local change.
- `kNone`: unchanged, tombstoned or never observed. An unobserved layout
  difference is also `kNone`, so native changes are never adopted without
  their observation.

## Tests

- New `ahoi_session_unittests` file
  `workspace_structure_split_capture_unittest.cc` (5 cases), added to
  `session/BUILD.gn`.
- Visible: move a live two-pane split in the sidebar to another Workspace,
  restart, and open that Workspace. The split is materialized there, and the
  synced record carries the new `workspace_id`.

## Not patched: the pane left behind

`GetMoveGroupNodeIds` falls back to the single source pane when a member
has no tree node id yet. In the current code every bound pane has a node,
temporary panes included. The fallback only triggers while a freshly
inserted pane waits for its asynchronous binding. Reproduce it first:
create a split and move it immediately. If it reproduces, refuse the move
until all members are bound instead of moving one pane.
