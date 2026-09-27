# 134 — Empty Workspace merge has no native undo receipt

Status: ready source finding and owner acceptance design; product fix and
native/visible evidence pending
Owner: desktop TabTreeStore and Workspace merge bridge
Reviewed source: `e670ff1` / ADR 0012, 27 September 2026.

## Exact gap

ADR 0012 promises undo after a same-context Workspace merge, restoring A's
identity, settings and nodes. Its separate empty-A rule waives confirmation,
not undo. In `tab_tree_store_merge_workspace.cc`, the merge still tombstones A
and stores `merged_into=B`, but line 133 writes the `kMove` undo operation only
when `!roots.empty()`. The existing
`EmptySourceLeavesNoUndoEntry` test explicitly pins `kNothingToUndo`.

Simply deleting that condition cannot work. `UndoLastMutation` in
`tab_tree_store_move_delete.cc` requires a nonempty `NodeSnapshot` list and a
snapshot for the `subject_node_id`; it revives tombstoned Workspaces only by
looking at the original Workspace of a restored live node. An empty A has no
such node. `ExportSnapshot`/`ReplaceSnapshot` also require nonempty undo-node
lists and recognize only the current local mutation kinds. A fake node
snapshot would ask `RestoreSnapshot` to delete/restore a row that never
existed. The merged source's Workspace row remains in the database, but the
current undo protocol cannot identify it as a standalone undo target.

This is a source-confirmed missing feature, not a reproduced browser gesture
failure. No native test, build, app or UI action was run by Crest.

## Required owner change

Provide a transactional, durable Workspace-level undo receipt for the empty
same-context merge. The receipt must identify A and B, bind the merge
operation, survive native snapshot export/import/reopen and permit A's
revival with its original ID/settings and a modified clock newer than its
merge tombstone. It must not turn a `record_undo=false` cross-session merge
into an undoable action. Existing node-move undo behavior, ordering and
receipt migration/validation must remain valid. The single-writer bridge must
publish the A revival and reverse any route/settings changes it made for the
merge; restoring only the SQLite row is not full ADR 0012 undo.

A dedicated local undo kind with Workspace identity or a Workspace-specific
receipt is reasonable. The exact storage shape belongs to the Desktop owner,
because it touches UndoSnapshot persistence and observer semantics. A
synthetic `subject_node_id` notification with no node needs explicit UI/
observer handling; do not assume a Node change event will refresh the
Workspace list. Handle the case where all source roots were temporary tabs
closed by the merge separately under the session-retirement rule.

## RED/GREEN and acceptance cases

1. Change the current empty-source negative test into a positive undo test:
   merge A into B, verify A is hidden and routes to B, undo once, verify A
   reappears with its exact identity, name/icon/color/settings, no extra
   folder/node, and `merged_into` cleared with newer authority. A second undo
   must not repeat the merge restoration.
2. Persist/reopen the native tree/undo snapshot between merge and undo; verify
   the receipt and Workspace row survive and restore identically. Exercise a
   crash before transaction commit: either the complete merge and receipt
   exist or neither does.
3. Keep the `record_undo=false` control. Check an empty A when B contains
   nodes, and when B is empty; undo must not mutate B's nodes or clocks.
4. Bind one Desktop context-menu and one command-bar/⌘Z visible journey to
   the exact installed candidate, including toast text and Workspace list
   refresh. Verify route/settings restoration and no accidental Sync repair
   echo. A real peer must see the newer A revival under the existing Format-3
   tombstone/target clock rules before H7 is called complete.

No foreign product file was written by Crest. This handoff records the
minimum cross-layer fix and evidence boundary; H7.2 remains open until owner
integration and exact candidate acceptance.
