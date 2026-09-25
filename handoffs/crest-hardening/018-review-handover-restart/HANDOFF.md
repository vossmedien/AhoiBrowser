# 018 – Review of the 016 fixes (fullscreen and restart hand-over)

Status: ready
Owner lane: desktop
Reviewed: `cfcc187` (source reading; the commit says "not yet built").

- H1 fixed: a fullscreen source leaves fullscreen before the hand-over and the
  presented window enters it.
- H2 fixed with one weakness. `RestoreHandOverAfterStartup` hides the other
  Profile's window after a fixed 2-second delay
  (`isolated_workspace_directory.cc:425`) and only if the presented Profile's
  window is already visible by then. On a slow or busy host (the load average
  was above 100 today), restoring the separated Profile's window can take
  longer, and both windows then stay visible, stacked.

  Fix idea: instead of a fixed delay, observe browser creation
  (`BrowserCollectionObserver` or the equivalent) for the presented Profile,
  with a generous timeout, and hide the others when its window first becomes
  visible.

- **WS-ISO-19**: under CPU load (for example during a build), switch to a
  separated Workspace, quit, and relaunch. Exactly one window is visible.

- 014 residual withdrawn: the owner's note is correct. `ClosePage()` runs
  unload handlers, not before-unload, so a late page cannot veto its close
  (WS-DEL-09 not applicable).
