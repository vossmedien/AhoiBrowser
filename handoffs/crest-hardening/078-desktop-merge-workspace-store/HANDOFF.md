# 078 – Desktop: store primitive for merging Workspaces (ADR 0012, part 1)

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `e356fbc` (`git apply --check` passes).
Implements the store half of ADR 0012 section 1 (`WS-MERGE-01`–`03`, `05`). The
session bridge and UI follow in 080.

## Change (`078-desktop-merge-workspace-store.patch`)

- New `TabTreeStore::MergeWorkspace(source, target, into_folder, sort_key,
  modified_at, &folder_id)` in `tab_tree_store_merge_workspace.cc`:
  - Every active root of the source moves with its whole subtree, including
    tombstoned descendants (as in `MoveNode`), in `sort_key,id` order.
  - With `into_folder`, the roots go into a new folder at `sort_key` among
    the target's roots, carrying the source's name, icon and accent. The roots
    keep their own keys inside it.
  - Flat, a root gets `sort_key + its key`, which keeps the source order after the
    target's last root. A combined key above the 1024-character sync limit is
    refused before any row changes.
  - The source row is tombstoned in the same transaction, with one `kMove` undo
    operation: node snapshots first, the new folder as a `nullopt` snapshot
    last. That's the same order as `CreateStyledFolderAroundNodes`, so undo
    deletes the folder after its children are back.
  - An empty source is only tombstoned, without an undo entry, like `DeleteWorkspace`.
  - One `kMoved` notification.
- `UndoLastMutation` revives every tombstoned Workspace that a restored live
  node belongs to: `tombstone=0`, `modified_at=MAX(modified_at+1, now)`, so the
  revival is newer than the tombstone and wins on synced devices. The sync
  merge model treats a Workspace tombstone as last-writer-wins; only archive
  deletion is terminal. The same rule also closes an existing gap: undoing a
  move out of a Workspace that was deleted in the meantime used to leave live,
  invisible nodes in a deleted Workspace.
- No schema change. The undo kind stays `kMove`, because the schema's
  `CHECK(mutation_kind IN (0,1,2,3))` would otherwise need a table rebuild.
- New `tab_tree_store_merge_workspace_unittest.cc` in `ahoi_tab_tree_unittests`
  with five tests: into a folder with undo, flat order, refusals without change,
  empty source without undo, and survival across a reopen including undo.

Checked by this lane: all three `.cc` files are syntax-checked (`-fsyntax-only`)
with the AhoiDev flags of their targets. Not built or run.

## Caller duties (080)

The store only moves rows. The bridge call must, in this order:

1. Pick `sort_key` after the target's last root (`GenerateSortKeyBetween`).
2. Before the store call, handle live tabs of the source:
   - With the same web context (`shared` into `shared`), rebind the tabs to the target.
   - Otherwise, collect the URLs, close the tabs with before-unload
     (`GroupPageClose::Ask`, as `ConvertWorkspaceToIsolated` does) and cancel
     on refusal.
3. Rewrite the structure state (archive entries, split bindings, routing rules
   that name the source) to the target, then retire the source's
   website-session binding or isolated profile through its normal deletion path.
4. Call `RefreshWorkspaceSnapshot()` after the merge and after an undo, since
   undo can now revive a Workspace.

## Tests for the owner

- `ahoi_tab_tree_unittests` (new file plus the existing undo tests).
