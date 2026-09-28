# 078 – Desktop: store primitive for merging Workspaces (ADR 0012, part 1)

Status: integrated by owner in `cc142964` (empty-source undo added by 134)
Owner lane: desktop (apply, build, test)
Base: HEAD `8231b0a` (`git apply --check` passes); 080 applies on top.
Implements the store half of ADR 0012 section 1 (`WS-MERGE-01`–`03`, `05`). The
session bridge and UI are 080.

## Change (`078-desktop-merge-workspace-store.patch`)

Revised on 26 Sep for 080: a parameter struct, open tabs, and optional undo.

- New `TabTreeStore::WorkspaceMerge {source, target, into_folder, sort_key,
  closing_temporary_ids, record_undo, modified_at}` and
  `MergeWorkspace(merge, &folder_id)` in `tab_tree_store_merge_workspace.cc`:
  - Every active **saved** root of the source moves with its whole subtree,
    including tombstoned descendants (as in `MoveNode`), in `sort_key,id` order.
  - With `into_folder`, the saved roots go into a new folder at `sort_key`
    among the target's roots, carrying the source's name, icon and accent. The
    roots keep their own keys inside it.
  - Temporary roots (open tabs) always move flat. A flat root gets
    `sort_key + its key`, which keeps the source order after the target's last
    root. A combined key above the 1024-character sync limit is refused before
    any row changes.
  - Temporary roots in `closing_temporary_ids` (tabs the caller closes) are
    tombstoned instead, without undo, like an explicit tab close.
  - The source row is tombstoned in the same transaction.
  - With `record_undo`, there is one `kMove` undo operation: node snapshots
    first, the new folder as a `nullopt` snapshot last, the same order as
    `CreateStyledFolderAroundNodes`.
  - An empty source is only tombstoned, without undo, like `DeleteWorkspace`.
  - One `kMoved` notification, plus `kDeleted` for closed tabs.
- `UndoLastMutation` revives every tombstoned Workspace that a restored live
  node belongs to: `tombstone=0`, `modified_at=MAX(modified_at+1, now)`, so the
  revival is newer than the tombstone and wins on synced devices. A Workspace
  tombstone is last-writer-wins in the sync merge model; only archive deletion
  is terminal. This also closes an existing gap: undoing a move out of a since
  deleted Workspace used to leave live, invisible nodes in a deleted Workspace.
- No schema change. The undo kind stays `kMove`, because
  `CHECK(mutation_kind IN (0,1,2,3))` would otherwise need a table rebuild.
- New `tab_tree_store_merge_workspace_unittest.cc` in `ahoi_tab_tree_unittests`
  with seven tests: into a folder with undo, flat order, open tabs outside the
  folder with the closing tab tombstoned, refusals without change, no undo
  entry when `record_undo` is false, empty source, and survival across a
  reopen including undo.

Checked by this lane: all three `.cc` files are syntax-checked (`-fsyntax-only`)
with the AhoiDev flags of their targets. Not built or run.

## Caller duties

Implemented by 080 (`SessionBridge::MergeWorkspace`). The bridge handles open
tabs and bindings, rewrites link routing and refreshes the Workspace list
after a merge and after an undo. See 080 for the rules on different web
contexts.

## Tests for the owner

- `ahoi_tab_tree_unittests` (new file plus the existing undo tests).
