# 084 – Sync: a merged Workspace's tombstone names its target (`mergedInto`)

Status: ready (design; wire change for the sync owner)
Owner lane: sync (format, C++ and Swift appliers); desktop and mobile apply
Base: HEAD `e138e11`.
ADR 0012 section 1, acceptance case `WS-MERGE-06`.

## Problem

A merge (078/080 desktop, 086 mobile) syncs as node moves plus the source's
Workspace tombstone. When a peer adds page X to the source while offline, X
arrives after the merge in a tombstoned Workspace. Today:

- Desktop: `tab_tree_sync_adapter.cc:222-228` moves every live node of a
  missing or tombstoned Workspace to `fallback_workspace`, the first active
  Workspace in order (`:169`), inside a recovery folder (`recover`,
  `:234ff.`). X is not lost, but it lands in the wrong Workspace, under a
  recovery folder.
- Mobile: `deleteWorkspace` tombstones the nodes it knows. An incoming live node
  of a tombstoned Workspace is not shown in the library, because
  `visibleTreeNodes` filters by live Workspaces. The owner should verify this
  while implementing.

## Proposal

1. `WorkspaceRecord` gains an optional `merged_into` (Workspace id). It is
   only set together with `tombstone = true` by a merge, and belongs to the
   `tombstone` field group, so both merge as one unit (last writer wins). An
   undo that revives the source clears it in the same write.
   - Format: `config/sync-format.json` plus both codecs (C++ `sync_model.h:127`
     `WorkspaceRecord`; Swift `Workspace` in `CompanionModels.swift`).
   - Before launch (ADR 0009), an additive optional field needs no migration.
     Older peers ignore it and keep today's fallback.
2. Apply rule, identical in both appliers: a live node whose Workspace is
   tombstoned is re-homed to that Workspace's `merged_into`, following the
   chain at most N times (N = number of Workspaces, stopping at a cycle). This
   applies only if the target is live, and then to its root end, without a
   recovery folder, because the merge was deliberate. Otherwise today's
   fallback applies.
   - Desktop: `fallback_workspace` becomes a per-node choice at
     `tab_tree_sync_adapter.cc:224`.
   - Mobile: the same rule in the snapshot apply path, before
     `visibleTreeNodes` hides the node.
3. Writers:
   - `TabTreeStore::MergeWorkspace` (078) writes `merged_into` with the
     tombstone, and undo clears it. That needs a `workspaces.merged_into`
     column (schema 5, additive `ALTER TABLE ... ADD COLUMN`), which only the
     desktop owner should cut, together with the codec.
   - Mobile `mergeWorkspace` (086) sets the field on the tombstoned `Workspace`.

## Conformance (H1, this lane)

Once the field exists, this lane adds merge vectors to
`fixtures/sync-conformance/`:

- tombstone plus `merged_into` against a concurrent rename: the tombstone group
  wins as a unit;
- a revival (undo) newer than the merge tombstone clears `merged_into`;
- an older revival loses.

The re-homing is apply logic, not a field merge. It needs a small shared apply
fixture instead: records in, expected Workspace per node out. This lane can
write that once the owner has picked the file location.

## Acceptance

`WS-MERGE-06` (ADR 0012): device 1 merges A into B while device 2 adds a page
to A offline. After both sync, the page is in B on both devices. Until this
lands, the documented result is today's fallback: the page sits in the first
Workspace's recovery folder on desktop.
