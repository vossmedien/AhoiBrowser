# 048 – ADR 0011 step 2: one shared switcher across Profiles

Status: integrated 5be0782 (build 32; unit tests green; visible check pending)
Owner lane: desktop (apply, build, test)
Base: HEAD `cdd3e98` **with 044 applied** (`git apply --check` passes on
HEAD+044 and on HEAD+044+040). By the crest-hardening lane; not compiled.

## Gap

WS-ISO-04 asks for switching by dot, keyboard, swipe and command bar
between Workspaces of every level. Today only the Workspace menu reaches
another Profile, as separate blocks (own, then main, then separated in
registry order). Dots, Cmd+1..9, Ctrl+Tab-style cycling and the swipe only
know the window's own Profile, so a separated Workspace is unreachable
from a main window except through the menu, and from a separated window
the only dot is its own.

## Change (sidebar host only)

- `SwitcherWorkspaces()`: the main Profile's Workspaces (own service in a
  main window, `GetLoadedMainProfile()` in a separated one) plus the
  openable separated Workspaces, in `OrderDirectoryWorkspaces` order
  (044), with name, icon, accent, level and `own`.
- `ActivateSwitcherWorkspace`: `own` → `SetActiveWorkspaceForWindow`;
  main from a separated window → `OpenMainWorkspaceByHandOver(id)`;
  separated → `OpenIsolatedWorkspaceByHandOver(dir)`. Both helpers are the
  existing menu hand-over code moved into members, unchanged (fullscreen
  handling of `RunHandOver` included).
- Workspace menu: one list in that order; the check mark stays on this
  window's Workspace; "Haupt-Workspaces öffnen" remains for a separated
  window whose main Profile is not loaded. Cmd+1..9 hints follow the
  switcher position (`context_workspace_positions_`).
- Dots: every switcher entry except the active one; `workspace_index` is
  the switcher position.
- `ActivateWorkspaceAtIndex` (dots, Cmd+1..9) takes the switcher
  position. Keyboard and swipe cycling
  (`ActivateRelativeSwitcherWorkspace`) wrap through the switcher; a
  neighbour in the same Profile keeps the animated in-window transition
  with the matching own delta, another Profile hands over.

## Not in this handoff

- The command bar's Workspace items and the link-routing target lists
  still list per Profile; they follow in 054 (order only, same helper).
- Dots do not yet refresh when only another Profile's list changes (a new
  separated Workspace appears after the next own change or window focus).
  A Local State observer on `ahoi.isolated_profiles` can be added with
  054 if the journey shows it.

## Tests

No unit harness for `BrowserSidebarHostView`. Visible journey (WS-ISO-04):
main Workspaces A, B, separated S created after B, main C created after
S. In the main window, dots, menu and Cmd+1..4 show A B S C. Cmd+3 hands
over to S. In S, the dots show A B C, Ctrl-cycling forward from S reaches
C (hand-over back to main with C selected), and a swipe backward from A
wraps to C with the in-window animation. Fullscreen: Cmd+3 from a
fullscreen main window leaves fullscreen, hands over, and re-enters it.
