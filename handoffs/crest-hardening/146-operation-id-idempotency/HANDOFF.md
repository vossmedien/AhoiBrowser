# 146 — Process-local operation IDs for late/repeated completions (H2.2)

Status: owner progress — #9 via 142 R1 (`46ac0428`, bounded retry), #4 via
142 R3 (`fcd926cc`, receipt consumed once), #5 via 142 R5 (`f479b6ea`),
#7 in `7a2fd744` (close once per live tab); #2/#3 in `f415c6c6` (tracked closes); #11 already covered durably (`SyncStore::ConsumeRemoteCommand`,
`sync_store_replay.cc`: INSERT OR IGNORE per command_id → kAlreadyApplied); #1, #6, #8,
#10, #12 open; nothing run
Owner lane: desktop (session bridge, sidebar, popup/Quick Window, structure controller)
Base: `60176d7`. Evidence: section 3 of the
[H2.1 source map](../../../docs/reviews/crest-hardening-2026-09-28-single-writer-source-map.md);
defects R1/R3 in [142](../142-single-writer-residuals/HANDOFF.md).

## Requirement

Crest goal H2.2 and the integrated single-writer rule (011, `docs/ARCHITECTURE.md`):
repeated or late completions (promotion, dismiss, close, commit) are idempotent
through a process-local operation ID. At `de04e0aa` only popup dismissal
(`dismissal_generation_`, `ui/popup/popup_overlay_controller.cc:546-553`) and
the sidebar surface (`workspace_surface_generation_`,
`ui/sidebar/browser_sidebar_host_core.cc:548`, `:639`, `:658`) carry a
generation; the map lists 12 transitions without one.

## Pattern to reuse (already in product)

`sync/native_extension_setup_controller.cc` mints
`base::Uuid::GenerateRandomV4()` per request (`:145`), carries it through the
async steps (`:150`, `:187`, `:202`, `:218`) and accepts a result only if
`IsCurrentOperation(id, operation_id, revision)` (`:171`, `:230`). Apply the
same shape: **mint at the deciding call, carry in every bound callback, check
once at completion, record the completed ID so a duplicate is a no-op.** IDs
are never persisted or synced (process-local), so no wire or schema change.

A small shared helper keeps this uniform, e.g. `session::OperationLedger`
(`Begin() -> base::Uuid`, `IsCurrent(id)`, `Complete(id) -> bool first_time`,
bounded LRU of completed IDs), owned by `SessionBridge`.

## Per-path insertion points

| # | Transition (map row) | Mint | Check / complete | RED test (proposal) |
| --- | --- | --- | --- | --- |
| 1 | Archive with close (A1/A2) | `ArchiveAgreedPages` | report `done(true)` only after the tracked `ClosePage()` completions for that ID; replaces the 30 s split token | `WorkspaceStructureControllerTest.ArchiveReportsDoneAfterPagesClosed` |
| 2 | Workspace delete with own site sessions (W8) | delete command | clear the partition when all tabs closed under that ID arrive, not after a fixed 3 s | `SessionBridgeWebsiteSessionRemovalTest.PartitionClearedAfterTrackedCloses` |
| 3 | Workspace merge, separate context (W10) | merge command | same as 2 | `SessionBridgeWorkspaceMergeTest.PartitionClearedAfterTrackedCloses` |
| 4 | Merge undo (W11, 142 R3) | merge command, stored with the `11e89142` receipt | undo restores window/tab bindings once per merge ID | `SessionBridgeWorkspaceMergeTest.RepeatedUndoRestoresBindingsOnce` |
| 5 | Popup/Peek promotion (P1/P2, 142 R5) | promotion request | Workspace switch result and ownership hand-over bound to the ID; a second promotion of the same contents is a no-op | `PopupOverlayControllerTest.DuplicatePromotionIsNoOp` |
| 6 | Quick Window adoption (Q1/Q2) | adoption request in `command_bar/quick_window.cc` (free functions; no controller class) | adopt the captured tab by ID, never "the next active tab" | `quick_window_browsertest.cc`: `RepeatedAdoptionDoesNotTakeAnotherTab` |
| 7 | Delete temporary row (D1) | delete command | `tab->Close()` once per ID | `BrowserSidebarHostTest.RepeatedTemporaryRowDeleteClosesOnce` |
| 8 | Delete saved row with open tab (D2) | delete command | re-binding under the new node references the ID | `BrowserSidebarHostTest.SavedRowDeleteRebindsOnce` |
| 9 | Structure commit (A4, 142 R1) | `Persist` | own cancellation flag per ID; a cancelled ID is retried, a completed ID is not re-applied | `WorkspaceStructureControllerTest.StructureCommitSurvivesConcurrentRename` |
| 10 | Split rebuild on open (S-f) | `MaterializeSplits` | Chromium split notifications matched to the rebuild ID, not only `applying` | `WorkspaceStructureControllerTest.LateSplitNotificationAfterRebuildIsIgnored` |
| 11 | Remote focus/close (T8) | remote command intake (the signed command already has a nonce) | execute once per command nonce within the process | `SessionBridgeRemoteCommandTest.RepeatedCloseCommandExecutesOnce` |
| 12 | Workspace hand-over (W4, *abgeleitet*) | hand-over command | verify `RunHandOver` first; add an ID only if completion is async | — |

Priorities: 9 and 4 are concrete defects (142 R1/R3); 1–3 replace timing
guesses with tracked completion; 5–8 and 11 harden repeated user/remote
actions; 10 is defensive.

## Acceptance

Per path: the RED test fails before the change (duplicate/late completion
applied twice or at the wrong time), passes after it; then the affected visible
journeys on the exact candidate (split archive/restore, Workspace delete/merge
with separate context and undo, popup/Quick Window adoption, row delete), as
required by H2's DoD. No build is requested for this handoff alone.

## Crest source review of owner progress (28 September)

- **#2/#3 `f415c6c`:** Workspace deletion and separate-context merge now clear
  the retired partition once the `WeakPtr<WebContents>` set captured at the
  decision has emptied (250 ms polling, 30 s bound for a hung renderer),
  instead of after a fixed 3 s. The captured set plays the role of the
  operation identity for this completion. Order checked: the merge path marks
  `closing_with_deleted_workspace` in `CommitWorkspaceMerge`
  (`session_bridge_workspace_merge.cc:291`) before `ClosePages()`/`ClosePage()`
  and before the clear is scheduled, and deletion marks it at
  `session_bridge_website_session_removal.cc:211` before the capture at `:240`,
  so the set is never empty by mistake. Accepted as source; no test yet
  (`SessionBridgeWebsiteSessionRemovalTest.PartitionClearedAfterTrackedCloses`
  and the merge counterpart remain proposed). Not compiled or run per commit.
- **#4 `fcd926c`** (via 142 R3): receipt consumed once per source → repeated
  undo cannot restore twice. **#7 `7a2fd74`:** `CloseTabForNodeOnce` closes a
  deleted row's tab once while that tab is alive; a RED test needs a
  `beforeunload` browser test, as the owner notes. Both accepted as source.
- **#11 (`17aa591`, owner note):** confirmed in source. The backend consumes
  each command durably before delivery (`profile_sync_backend.cc:325`,
  `SyncStore::ConsumeRemoteCommand`, `INSERT OR IGNORE` per `command_id` in
  `sync_store_replay.cc:32-42`); a repeat yields `kAlreadyApplied` and is
  recorded as `replay_rejected`, even across restarts. Cosmetic: both branches
  of the ternary at `profile_sync_backend.cc:329-331` are identical. #1, #6,
  #8, #10, #12 remain.
