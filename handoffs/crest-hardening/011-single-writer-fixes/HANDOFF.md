# 011 – Single-writer rule and fixes for tree/session double writers (H2)

Status: ready
Owner lane: desktop (session, sidebar, split, architecture doc)
Base: `b141fce`. Audit: `docs/reviews/crest-hardening-2026-09-25-single-writer-audit.md`.
Deletion re-homing is tracked separately as handoff 010 R6.

## 1. Architecture rule

Add to `docs/ARCHITECTURE.md` after "## Tree model":

> ### Single writer
> An Ahoi operation decides a semantic transition (Workspace switch, move,
> archive, delete, split) and commits it once. Chromium executes and reports
> completion. Observers of a self-caused change do not write again. They
> recognize it by an operation guard that lasts until Chromium's asynchronous
> execution has finished, not merely until the call returns. Only externally
> caused changes (native UI, extensions, restore) become new facts. Late or
> repeated completions are idempotent through a process-local operation ID.
> Authority epochs expire only on changes to the guarded precondition.

## 2. Fixes, in priority order

| # | Finding | Fix idea | Test |
| --- | --- | --- | --- |
| S1 | Moving the active tab switches only the sidebar view, not `WorkspaceService` (`browser_sidebar_host_command_dispatch.cc:211`, `:239`) | call `session_bridge_->SetActiveWorkspaceForWindow(...)` there; the sidebar follows the service notification | `BrowserSidebarHostTest.MoveActiveTabToWorkspaceUpdatesWorkspaceService`: after the move, `GetActiveWorkspaceForWindow == dest` and `IsTabInActiveWorkspace(active)` |
| S2 | Archiving a live split tombstones its split record (inferred) | record the native split tokens being closed for the archive before `ClosePages()`; `OnSplitChanged` does not tombstone those tokens, or members for which `IsNodeArchived` holds | `WorkspaceStructureControllerTest.ArchiveLiveSplitKeepsSplitRecordRestorable` |
| S3 | Unrelated events cancel structure commits (inferred) | bump the epoch only for split, tree-structure and archive-membership changes, not for `kSelectionOnly`, title or resource changes | `...ArchiveSurvivesUnrelatedTitleChange` |
| S4 | Restore applies window metadata before tab metadata (inferred) | apply tab metadata first, or hold the sidebar reaction until one "restore committed" notification, then `EnsureWorkspaceSurface` | `...RestoreKeepsSelectedTabAndHidesEmptyState` |
| S5 | Activating a hidden tab (command bar, remote focus, `tabs.update`) switches the Workspace afterwards | callers that activate a tab in another Workspace switch the Workspace first; `ReconcileWorkspaceSurface` only aligns the view | `BrowserSidebarHostTest.BackgroundActivationOfHiddenTabDoesNotSwitchWorkspace` |
| S6 | Split record `workspace_id` is not updated when members move; a temporary pane stays behind | derive the split Workspace from its members, or update the record in the same tree operation; include temporary panes in the move group | `...MovingSplitGroupUpdatesSplitRecordWorkspace` |
| S7 | Promoted popup or Quick Window tab lands in the active or first Workspace, not the opener's | pass the opener's Workspace explicitly and bind it at insertion | `...PromotedPopupKeepsOpenerWorkspace` |
| S8 | Deleting a temporary row with an open tab recreates it under a new ID | distinguish "unsave" from "delete row"; delete closes the tab or leaves it unbound | `...DeletingTemporaryRowDoesNotRecreateNode` |

Visible journeys after the fixes, on the exact candidate: moving the active
page and then typing a URL; archiving and restoring an open split;
restoring at startup with a non-first Workspace selected; promoting a popup
from a second Workspace.
