# 054 – ADR 0011 step 2: command bar and routing lists use the shared order

Status: integrated 5be0782 (build 32; unit tests green; visible check pending)
Owner lane: desktop (apply, build, test)
Base: HEAD `1f87598` **with 044 and 048 applied** (`git apply --check` passes
on that stack). By the crest-hardening lane; not compiled.

## Gap

- WS-ISO-04 includes switching by command bar. The command bar's Workspace
  items (`session_bridge_runtime.cc:454-465`) only list the window's own
  Profile, and `SwitchWorkspace` only switches inside it.
- The link-routing target chooser (`TargetOptions`) and the Settings list
  (`LinkRoutingWorkspaces`) concatenate main, then separated Workspaces in
  registry order; ADR 0011 asks for one order there as well.

## Change

- `PublishCommandItems` also publishes the openable separated Workspaces
  (secondary text "Vollständig getrennt") and, in a separated window, the
  loaded main Profile's Workspaces; ids already listed are skipped.
- `SwitchWorkspace` first switches in place; for an id of another Profile
  it calls the new `sidebar::ActivateBrowserWorkspaceById`, which finds
  the id in the shared switcher (048) and activates it (hand-over).
- `TargetOptions` and `LinkRoutingWorkspaces` return the Workspaces in
  `OrderDirectoryWorkspaces` order (044).

Known limits: the command items of other Profiles refresh with this
Profile's next `PublishCommandItems` (any tab or tree change), not on a
registry change; the secondary text is German only, like other Ahoi
command-bar labels in the bridge.

## Tests

Existing `SessionBridgeTest` command-index tests are unaffected (no
separated entries, main Profile is the test Profile). Visible journey:
from the main window, command bar "S" → separated Workspace S is presented
in the same frame; from S, command bar "A" → main window with A; the
routing chooser and Settings → Link routing list A B S C in the switcher
order of 048's journey.
