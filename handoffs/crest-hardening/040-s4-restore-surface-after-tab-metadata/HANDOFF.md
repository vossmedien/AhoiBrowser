# 040 – S4: restore aligns the Workspace surface after the tabs' metadata

Status: integrated 00381ba (syntax-checked; reproduced 5/5 on build 32: the window shows the second Workspace's tab while the selector and sidebar stay in Inbox; acceptance on build 33 by restore-surface-journey)
Owner lane: desktop (reproduce, apply, build, test)
Base: HEAD `755e3a9` (`git apply --check` passes on HEAD and on the current
worktree, and together with 038). Implementation of 011 S4 by the
crest-hardening lane; not compiled by the lane.

## Reproduce first (S4 was inferred from source)

Quit with a second (non-first) Workspace selected, its own tab active, and
tabs in several Workspaces; start again. Expected defect, intermittently:
the window shows the empty-state overlay over a restored page, activates a
different tab than the one that was selected, or ends in the first Workspace.

Cause, from source:

- SessionRestore applies the window's extra data at window creation, before
  any tab (`0001` seam, `CreateRestoredBrowser` → `RestoreWindowSessionExtraData`),
  and `TrackBrowser` first forces the first Workspace
  (`session_bridge_observers.cc:68-72`).
- While the tab tree is still loading, `ApplyPendingSessionMetadata` also
  applies windows before tabs (`session_bridge_session.cc:247-258`).
- Each `OnActiveWorkspaceChanged` and tab-strip notification makes the
  sidebar align the surface (`browser_sidebar_host_core.cc:459-528`,
  `ReconcileWorkspaceSurface` with `follow_selected_tab`) against a
  half-applied state: tabs without their Workspace yet, no last-active flag,
  or a selected tab that pulls the window back into another Workspace.

## Change

- Bridge: `ApplyPendingSessionMetadata` applies tab metadata before window
  metadata.
- Sidebar host: `DeferWorkspaceSurfaceDuringRestore()` skips
  `ActivateWorkspaceRuntimeTab` and `ReconcileWorkspaceSurface` while
  `SessionRestore::IsRestoring(profile)`, and subscribes once to
  `RegisterOnSessionRestoredCallback`. SessionRestore notifies while it is
  still registered as restoring, so the host posts one
  `ReconcileWorkspaceSurface(current generation, follow_selected_tab=false)`:
  the window's restored Workspace wins, its remembered or last-active tab is
  activated, and the empty state is shown only if that Workspace has no tab.
- After the first notification the host no longer defers (a later "restore
  previous session" into an already open window behaves as today).

## Tests

- No unit harness exists for `BrowserSidebarHostView`, and
  `SessionBridgeTest` becomes ready in `SetUp`, so the pending path cannot be
  exercised there. Suggested once a harness can start the bridge before the
  tree is ready: `SessionBridgeTest.PendingRestoreAppliesTabsBeforeWindow`
  (record `GetWorkspaceForTab` inside `OnActiveWorkspaceChanged`).
- Visible journey (the acceptance, named in 011 as
  `RestoreKeepsSelectedTabAndHidesEmptyState`): the reproduction above, five
  restarts, each ending in the saved Workspace with its selected tab and no
  empty-state overlay; one restart with the saved Workspace empty shows the
  overlay.
