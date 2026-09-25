# 028 – S5: Ahoi callers select the tab's Workspace before the tab

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `ad93779` (patch applies with `git apply --check`). Implementation of
011 S5, written by the crest-hardening lane; not compiled by the lane.

## Change

- `SessionBridge::ActivateTabInItsWorkspace(tab, source, user_gesture)`
  switches the tab's window to the tab's Workspace through
  `SetActiveWorkspaceForWindow` first. It then re-resolves the index, since
  the switch may activate that Workspace's last tab, and activates the tab.
- Command bar "open tab" (`ActivateOpenTab`) and the remote focus command
  (`FocusNormalTabFromRemoteCommand`) use it. Neither leaves the active tab
  hidden for the posted `ReconcileWorkspaceSurface` to repair.
- `ReconcileWorkspaceSurface(follow_selected_tab)` stays unchanged as the
  single adoption path for *external* activations (extension
  `tabs.update`, Chromium internals), which ARCHITECTURE.md "Single writer"
  allows as new facts.

## Tests

- New `SessionBridgeTest.ActivateTabInItsWorkspaceSelectsWorkspaceFirst`:
  right after the call, the active tab and the window's Workspace agree
  without any posted task.
- `ahoi_session_unittests`, `ahoi_command_bar_unittests` (existing
  `ActivateOpenTab` fakes are unaffected, as the delegate interface is
  unchanged).
- Visible: open-tab result in the command bar that lives in a hidden
  Workspace. The sidebar shows that Workspace at once, with no empty-state
  flash and no second switch.
