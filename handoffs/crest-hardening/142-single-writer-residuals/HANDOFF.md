# 142 — Single-writer residuals at current source (H2.1/H2.3)

Status: ready (source findings with proposed minimal fixes and RED tests; no patch)
Owner lane: desktop (session bridge, sidebar, structure controller)
Base: `de04e0aa`. Map: [H2.1 source map](../../../docs/reviews/crest-hardening-2026-09-28-single-writer-source-map.md).

The 25 September audit aggregated transitions and 011 deferred S2–S8. The new
per-transition map (≈45 rows, file:line at `de04e0aa`) shows S2, S4, S7, S8 and
finding 4 fixed (`5f1ce2c`/`739cea6`, `00381ba`, `047d739`, `72109fc`,
`17f5319`). The residuals below were re-checked in source by the Crest main
session. They are source findings, not runtime failures; the empty-Workspace
navigation defect stays with Desktop. Paths are relative to
`overlay/chromium/src/ahoi/browser/`.

| ID | Residual | Evidence | Minimal fix | RED test (name proposal) |
| --- | --- | --- | --- | --- |
| R1 (S3 rest, high) | Any tree change, including rename/title metadata, cancels an in-flight structure commit. | `OnTabTreeChanged` → `session/session_bridge_observers.cc:605` `ScheduleTabTreePersistence` → `session/session_bridge.cc:319-321` `CancelPendingSyncedTabTreeApply` → sets `pending_tree_apply_cancelled_`, which the structure commit shares (`session/session_bridge_structure_persistence.cc:133`, `:143-153` → `kCancelled`). Each local persist also calls `OnNativeChanged` (`session/session_bridge_sync_persistence.cc:225-227`). A cancelled commit stays `dirty_` and is rescheduled only by the next event (`session/workspace_structure_controller.cc:563`, `:164-168`), so continuous title/tree activity can starve archive/split persistence (starvation inferred, not reproduced). | Give the structure commit its own cancellation flag; cancel it only for changes classified by `TreeChangeInvalidatesStructure`. Do not bump the epoch from `OnLocalTabTreePersisted` for metadata-only persists. | `WorkspaceStructureControllerTest.StructureCommitSurvivesConcurrentRename` — start a commit, rename a node before the persistence task runs, expect `kOk` and the archive record present. |
| R2 (S5 rest) | Sidebar discovery "open tab" activates a hidden tab directly and lets `ReconcileWorkspaceSurface` switch the Workspace afterwards. | `ui/sidebar/browser_sidebar_host_discovery.cc:476-479` `ActivateTabAt`; the S5 path in `session/session_bridge.cc` (switch first) is not used here. | Route through the S5 helper (switch Workspace via the service, then activate). | `BrowserSidebarHostTest.DiscoveryActivationOfHiddenTabSwitchesWorkspaceFirst` — observer records service switch before activation. |
| R3 (merge undo, ADR 0012) | Store undo of a Workspace merge revives records, but the session bridge only refreshes the Workspace list. Since `11e89142` (after this map) the retargeted link-routing rules/default route are restored from a receipt; windows switched to the target and rerouted tabs without a tree node are still not restored. | `session/session_bridge_observers.cc:552-555` `RefreshWorkspacesAfterUndo` on `kUndone`; merge path `session/session_bridge_workspace_merge.cc:230-273` records undo (`023e612`) without an operation record the undo could replay. Complements 134 (empty source). | Extend the `11e89142` receipt with the moved windows, unbound tabs and previous active Workspace per window; on `kUndone` of that receipt restore window/tab bindings, idempotent by operation ID. | `SessionBridgeWorkspaceMergeTest.UndoRestoresWindowAndUnboundTabs`. |
| R4 (finding 3 rest) | Empty-state surface reads the sidebar view model; patch 0054 reads `WorkspaceService`. They diverge whenever `ActivateWorkspace` in the view model fails. | `ui/sidebar/browser_sidebar_host_core.cc:535-536` vs `session/session_bridge_session.cc:161-162`; failure branch `ui/sidebar/browser_sidebar_host_core.cc:383-386` (trigger not found, inferred). | Read the service in `EnsureWorkspaceSurface`; treat the view model as a projection. | `BrowserSidebarHostTest.EmptySurfaceFollowsServiceWhenViewModelActivationFails`. |
| R5 (S7 rest) | Popup promotion ignores the result of the opener-Workspace switch; on failure the tab lands in the displayed Workspace. | `ui/popup/popup_overlay_controller.cc:85-86`. | Abort or bind explicitly when the switch fails. | `PopupOverlayControllerTest.PromotionKeepsOpenerWorkspaceWhenSwitchFails`. |
| R6 (S6 rest) | Moving a split whose member has no node id moves only the source pane. | `ui/sidebar/browser_sidebar_host_tree_actions.cc:453-458` (`return {source_node_id}`); left unpatched by 032 pending a reproduction. | Refuse the move (no partial split) until every member is bound. | `BrowserSidebarHostTest.SplitMoveWithUnboundMemberIsRefused`. |

## Operation-ID gaps (H2.2)

Map section 3 lists 12 transitions whose late/repeated completion has no
process-local operation ID (archive close reports success before pages close,
Workspace delete/merge clear partitions after a fixed 3 s, merge undo, popup/
peek promotion, Quick Window adoption, temporary/saved row delete, structure
commit, split rebuild on open, remote focus/close, hand-over *abgeleitet*).
Only popup dismissal and the sidebar surface carry a generation. R1 and R3 are
the concrete defects; the rest should gain an operation ID when their path is
next touched, per the integrated single-writer rule.

## Acceptance

Each fix with its RED→GREEN unit test in the next Desktop package; then the
affected visible journeys on the exact candidate (split archive/restore,
Workspace switch, merge + undo, popup/Quick Window adoption), per H2 DoD.
No extra build is requested for this handoff alone.
