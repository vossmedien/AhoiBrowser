# 010 – Review of the integrated handoffs 003 and 006 (H6.4, H2)

Status: integrated 990c7bb, 17f5319 (R4 and R5 deferred; tests pending on the next candidate)
Owner lane: desktop
Base: `15ec908`. Reviewed commits: `5cfc9d2` (GroupPageClose, patch 0055),
`52bfd3c` (Workspace deletion), `87a6b89` (archive and close-all).
All findings come from source reading; none has been reproduced at runtime yet.

## R1 (high) – One overlap disables archive, close-all or deletion until restart

`GroupPageClose::Ask` (`session/group_page_close.cc`) calls `Finish(false)`
**synchronously** when a page is already part of another running question.
All three callers store the result in a member and gate new questions on it:

- `archive_close_ = GroupPageClose::Ask(...)`, gated at
  `workspace_structure_controller_archive.cc:160`
- `close_all_temporary_ = …`, gated at `browser_sidebar_host_runtime_actions.cc:667`
- `workspace_deletion_close_ = …`, gated at
  `session_bridge_website_session_removal.cc:87`

Their `done` callback resets or moves that member while it is still empty.
After `Ask` returns, the already finished group is stored and never cleared,
so every later request of that kind is refused for the rest of the session.

Example: "close all temporary tabs" shows a before-unload prompt on page X,
and automatic archiving selects X at the same moment. Automatic archiving then
stays silently disabled until restart.

The same early return has already `Register()`ed the watchers of earlier
pages without calling `DispatchBeforeUnload`. `Finish(false)` moves them to
`Leftovers()`, where they stay in `PendingPages()`. The next ordinary close of
such a tab is swallowed by `HandleBeforeUnloadFired`, so the first close
attempt does nothing.

Fix: `group-page-close-async-overlap.patch` checks every page for overlap
before registering any, and reports the rejection through a posted task, as
the no-prompt path already does. The header now states that `done` always runs
asynchronously.

Test (browser test, needs pages with before-unload handlers):
- **GPC-01**: Page X in a running group with an open prompt; start a second
  group containing X. `done(false)` arrives after `Ask` returned; a third
  group without X starts normally.
- **GPC-02**: Same as GPC-01, but the second group has page Y (with a
  handler) before X. Afterwards Y closes on the first attempt.
- **GPC-03**: A caller that stores the group; overlap; a second request of
  the same kind is accepted.

## R2 (medium) – Startup removal loads the partition it is about to delete

`ResumeWebsiteSessionRemovals` resolves the path of an orphaned binding with
`profile_->GetStoragePartition(config)` (default `can_create = true`). That
creates and loads the partition, and then the same function clears it and
deletes its directory on the thread pool while it is loaded. Its cookie and
database files may be rewritten after the delete, and
`CompleteWebsiteSessionRemoval` then records the removal as complete with data
left on disk.

Fix idea: compute the partition path without loading it (the path is
deterministic from the `StoragePartitionConfig`), or pass
`can_create = false`. Delete the directory only when the partition is not
loaded, and otherwise leave the intent pending for the next launch.

- **WS-DEL-06**: Crash after the tree commit; restart. The partition is
  never loaded during startup cleanup, the directory is absent afterwards,
  and the pending intent is cleared only after the directory is really gone.

## R3 (medium) – A page opened during the deletion prompt stays with the deleted accounts

The group is fixed when `Ask` runs. `CommitWorkspaceDeletion` recomputes
`IsolatedPagesOfWorkspace` and skips re-homing those pages, but only the
group's pages are closed. An isolated page opened in that Workspace while the
prompt was open is neither closed nor moved, and keeps
`runtime.workspace_id` of the deleted Workspace.

Fix idea: after the commit, close every page of the deleted binding (first the
group, then the rest through the normal close path), or reject the deletion
when the set changed between question and commit.

- **WS-DEL-07**: Prompt open on page A; open page B in the same Workspace;
  confirm. Neither A nor B survives with the deleted Workspace's accounts.

## R6 (high) – Pages of the deleted Workspace are re-homed before they close

Found by the H2 audit and checked against the code. `CommitWorkspaceDeletion`
calls `UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false)` for the
isolated pages and skips re-homing them (`session_bridge_website_session_removal.cc:194-200`).
The unbind itself calls `ScheduleTreeNodeBinding(tab)`
(`session_bridge_workspace.cc:405-407`). The delayed `EnsureTreeNodeForTab`
then finds no existing Workspace and falls back to the window's active
Workspace (`session_bridge_runtime.cc:225-227`). The delayed
`ReconcileWorkspaces` also assigns the fallback
(`session_bridge_observers.cc:407-423`). Both run before `ClosePage()`
finishes its unload. The pages therefore appear, briefly or permanently if
an unload hangs, as temporary rows of the fallback Workspace, still carrying
the deleted Workspace's cookies. That is exactly what handoff 003 forbids.

Fix idea: mark these tabs "closing, excluded" before the commit. Binding,
`EnsureTreeNodeForTab` and `ReconcileWorkspaces` skip them, and
`UnbindTreeNodeFromTabInternal` is called with `clear_workspace = true` for
them.

- **WS-DEL-08** (`SessionBridgeTest.DeletedIsolatedWorkspacePagesAreNotRehomed`):
  open a page in an isolated Workspace, delete it and agree, run pending tasks
  before the close completes, then assert that no temporary node exists for
  it and that its runtime Workspace is not the fallback.

## R4 (low) – Clearing races with closing pages

`OnWorkspaceDeletionPagesAnswered` clears the partition 3 s after
`ClosePages()`. A page whose unload handler runs longer can write again after
the clear. The next-launch directory deletion bounds the impact. Clearing
after the last page of the binding was destroyed would remove the fixed delay.

## R5 (open, from 006) – Popup overlays at quit

Deferred by desktop (needs an UnloadController/BrowserCloseManager seam).
Keep CLOSE-GRP-03 open.

## Owner intake (desktop, 2026-09-25)

- R1: patch applied unchanged (`990c7bb`). GPC-01..03 need a browser test with
  before-unload pages; queued with the visible CLOSE-GRP journeys.
- R2: startup cleanup computes the directory (`WebsiteSessionPartitionPath`,
  pinned by `SessionBridgeTest.ComputedPartitionPathMatchesTheLoadedPartition`)
  and never loads the partition; a partition held by a restored page is
  cleared natively and its directory deleted at the next launch (`17f5319`).
- R3: pages opened during the prompt close after the confirmed commit.
- R6: pages of the deleted sessions are marked closing; binding, temporary
  nodes and reconciliation skip them, and they are unbound with
  `clear_workspace = true`. WS-DEL-08 as a unit test needs a page in a fixed
  partition in `TestingProfile`; covered by the visible WS-DEL journey instead.
- R4 deferred: the 3 s clear is bounded by the next-launch directory
  deletion; replacing the delay by "last page destroyed" follows with S2.
- R5 stays deferred (UnloadController seam), CLOSE-GRP-03 open.
