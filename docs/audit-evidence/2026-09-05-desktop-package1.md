# Desktop package 1 — installed acceptance and persistence diagnosis

Date: 2026-09-05. Original visible candidate:
`0a13e22ff4e9cd1ac43a508e304fcaa0fe64a997`.
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

## Corrective candidate `3d413ef` — build/install and independent tests

- Guarded combined build `83719` and atomic installation `89774` both exited 0.
  Receipt: `artifacts/install/ahoi-dev-3d413ef-20260905T074543Z.json`; the recorded
  source, executable and bundle-tree hashes match installed readback.
- Corrected visible E2E is technically unavailable in this main session:
  Computer Use returned `Sky Computer Use native pipe startup failed` on app
  selection, after runtime reset/reselection, and on the surface inventory.
  No corrected Arc runtime result is inferred from installation or tests.
- A bounded isolated-profile Bookmark UI slot was explicitly delegated to its
  owner, with no default-profile Arc mutation or build/checkout permission.
- Under the documented technical-E2E exception, the exact built
  `ahoi_tab_tree_unittests` ran `AhoiTabTreeStoreTest.Snapshot*`, single-job,
  no retries: **2/2 PASS, exit 0**. This covers repeated nested replacement and
  complete rollback after a SQL uniqueness failure. It is not an Arc E2E pass.
  Logs and JSON: `artifacts/tests/persistence-3d413ef-20260905/tree-snapshot.*`.
- `SessionBridgeTest.BackupFlushPersistsSecondNestedTreeMutation`: **1/1 PASS,
  exit 0**, single-job and no retries. Both production flush bools and the
  second mutation's durable readback succeeded. Logs/JSON:
  `artifacts/tests/persistence-3d413ef-20260905/session-backup-flush.*`.
- The remaining tree-store tests ran without repeating the two snapshot cases:
  **18/18 PASS, exit 0**. Total tree-store coverage is **20/20 PASS** on this
  candidate. Logs/JSON: `artifacts/tests/persistence-3d413ef-20260905/tree-remaining.*`.
- The session test emitted a duplicate `ANGLESwapCGLLayer` class warning from
  component dylibs. It did not fail the test; no causal crash or idle-CPU claim
  is inferred. The warning remains a separate development-build observation.
- The remaining SessionBridge cases ran with the new flush case excluded:
  **13/14 PASS, exit 1**. Combined SessionBridge result is **14/15**, not green.
  `RemovesUrlUserinfoBeforeCommandIndexing` failed its global
  `Query(u"username").empty()` assertion; URL, title and secondary-text
  sanitization assertions passed. Logs/JSON:
  `artifacts/tests/persistence-3d413ef-20260905/session-remaining.*`.
- Read-only triage found a test-oracle problem: `ScoreText` permits a subsequence
  across the concatenated searchable fields. Repeating the already-sanitized
  `https://example.test/private-document` in title, secondary text and keywords
  allows `username` at offsets 32,42,46,60,73,86,87,90 without containing that
  literal text or credentials. The default workspace is `Ahoi`; the synthetic
  reconstruction agrees with the production indexing and scoring source.
- The test source now uses credential-only sentinels that cannot appear in the
  sanitized data and additionally checks both indexed keyword fields exactly.
  Product search/redaction code is unchanged. This test-only correction is NOT
  compiled or rerun yet; include it in the next coherent UI package, not an
  extra standalone browser build. The recorded 3d413ef failure remains visible.
- Visible import/restart/no-op stays open.

Exact test executable SHA-256 at execution:

- `ahoi_tab_tree_unittests`:
  `0f2c86289964d0ff4eedd80e09e37d6968bd20ef803cc9ce2380ef19003197d9`
- `ahoi_session_unittests`:
  `d2f6e767f88bd6b41ef7719529d594f88dfef703b27098be1176ab7ee746ebe7`

## Normal-folder motion follow-through (read-only review, not yet fixed)

The bookmark owner relayed the user's rejection of expand/collapse motion.
Three concrete source findings are retained for Desktop's next correction:

1. `sidebar_tree_view_projection.cc::StartPreferredHeightAnimation` sets its
   active flag before `SlideAnimation::Reset`; interrupting a running animation
   synchronously clears that flag through `AnimationCanceled`. Its new starting
   height also comes from the previous target, not the displayed intermediate
   height. Capture the displayed height, reset, then establish new active state.
2. `SynchronizeVisibleRows` starts each new child at its preceding visual row's
   position, overlapping transparent labels. Collapse instead immediately
   recycles descendants. Use one bounded reveal below the folder for both entry
   and exit, while keeping model state authoritative and hidden rows noninteractive.
3. Split clips use final group bounds while animated rows still have intermediate
   bounds. `UpdateSplitGroupClipPath` subtracts the latter; a displaced split can
   become fully clipped. Clip and animated group must share current geometry.

Visible acceptance must cover multi-child expansion, fast reversals, viewport
edges, and a split below the folder. Check intermediate frames, focus and hit
targets as well as endpoints. No causal connection to the sampled idle CPU is
established. These findings do not modify the frozen `3d413ef` build.
