# Desktop package 1 — installed acceptance and persistence diagnosis

Date: 2026-09-05. Source: `0a13e22ff4e9cd1ac43a508e304fcaa0fe64a997`.
Installed bundle and receipts are bound in `../ACTIVE_DESKTOP_CHECKPOINT.md`.
Evidence below is from the actual `/Applications/AhoiBrowser.app`, not a harness.

| Journey / finding | Observed result | Status |
| --- | --- | --- |
| Native App-menu Import from a normal zero-tab window | Opens the real Settings import dialog | scoped visible pass |
| Arc checkbox layout | Three real choices align with the first label line; mouse and Space work; profile/sidebar choices gate Import; splits optional | scoped visible pass |
| Duplicate consent | Mandatory translated backup notice replaces two duplicate consent checkboxes; single primary Import remains explicit | scoped visible pass |
| Arc source still running | Commit refuses with source-in-use state | scoped visible pass |
| Real Arc import after Arc exits | Preview 1/36/133/3; commit returns `backupError`, no backup root or journal created | fail, not imported |
| Nested snapshot replacement | Synthetic SQL reproduces FK failure; real store metadata has the triggering parent-before-child shape | cause established, correction pending build/E2E |
| Folder/workspace motion, splits/restart/no-op, extensions | Not yet completed on this installed candidate | open |

## Non-mutating diagnosis

- Exact dialog status was read via its `arcImportResult_.status` in DevTools;
  no call retriggered the import or changed the tree.
- Only schema, counts, FK violation count, URL scheme counts and row-order
  relationship were read from the authoritative profile store. No private
  URLs, titles, source JSON, credential stores or backup contents were dumped.
- `Default/Ahoi Tab Tree`: 1 workspace, 5 nodes, 1 nested node, 5 undo operations,
  0 FK violations, 1 parent-before-child pair. `Ahoi/TabTree.sqlite` is legacy
  and not SessionBridge's persistence target.
- `FlushPersistenceForBackup` persists through `ReplaceWithSnapshot`; false
  becomes `kBackupError` before `CreateArcImportBackup`. The backup function
  creates its root early, so the absent root and journal match this boundary.
- Existing destination `chrome:` URLs are allowed by the native tree validator;
  source-only URL filtering was inspected and is not this failure's cause.
- `ON DELETE RESTRICT` rejects deleting the parent before its child even in
  one bulk DELETE. Pinned SQLite source and the
  [official SQLite foreign-key documentation](https://www.sqlite.org/foreignkeys.html#fk_actions)
  agree. A synthetic in-memory two-node reproduction exits 19.

## Correction contract

The same transaction detaches old `parent_id` edges before replacing all rows;
foreign keys remain enabled. Failure rolls back edges, tree, workspaces and
undo together. No schema migration, profile rewrite outside the product,
constraint suppression, or source mutation is required.

Written regression coverage (execution pending corrected visible E2E):

- `AhoiTabTreeStoreTest.SnapshotRoundTripsTreeTombstonesAndUndoHistory`
- `AhoiTabTreeStoreTest.SnapshotReplacementFailurePreservesExistingTree`
- `SessionBridgeTest.BackupFlushPersistsSecondNestedTreeMutation`

The repeated-write test must exercise an already-populated nested destination.
The failure test injects an undo uniqueness violation after replacement begins
and reopens the file to verify total rollback. The bridge test observes both
production flush bools and reads the second mutation from the canonical store.
Successful compilation alone will not close this finding.
