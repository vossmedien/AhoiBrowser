# 082 – Desktop: command bar "In Workspace verschieben …" (ADR 0012, section 2)

Status: integrated by owner in `cc142964`
Owner lane: desktop (apply, build, test)
Base: HEAD `e138e11` (`git apply --check` passes).
Implements `CMD-MOVE-01`. No new store API: the command reuses the sidebar's
"Move to" paths with the Workspace root as destination.

## Change (`082-command-bar-move-to-workspace.patch`)

- **Items.** `internal::BuildMoveToWorkspaceCommands`
  (`command_execution_adapter.cc`) makes one `kBrowserCommand` item per
  Workspace of this Profile. The id is `move-to-workspace.<uuid>`, the title
  "In Workspace verschieben: <Name>" (German locale) or "Move to Workspace:
  <Name>", and the keywords are both command phrases plus the Workspace name,
  so typing the name after the command finds it. The priority is 180. Items with an
  invalid id or an empty name are skipped.
- **Publishing.** `CommandBarController::PublishBrowserCommands` appends the
  items from `SessionBridge::tab_tree_store()->GetWorkspaces()`. That skips
  off-the-record windows, and covers only the same Profile, like the context menu.
  `OnCommandIndexChanged(kWorkspace)`, which is SessionBridge's Workspace
  list, republishes, so new, renamed or deleted Workspaces show up. There is no
  loop: only `kWorkspace` triggers.
- **Execution.** In `CommandExecutionAdapter`, the prefix is checked before the
  shortcut and allowlist branches. A malformed UUID is refused
  (`GetMoveToWorkspaceTarget`) and never reaches `chrome::ExecuteCommand`. The
  new delegate calls `CanMoveToWorkspace`/`MoveToWorkspace` default to
  `false`. The Chromium delegate forwards to
  `sidebar::(Can)MoveBrowserSidebarSelectionToWorkspace`.
- **Sidebar.** New `BrowserSidebarHostView::MoveSelectionToWorkspace(id,
  dry_run)` in `browser_sidebar_host_move_command.cc` moves a source into the
  target's root, in this order:
  1. The selected folder in the tree.
  2. Otherwise the active tab's saved page (`FindTreeNodeIdForTab`), with its
     split group (`GetMoveGroupNodeIds`), through `PerformGroupedDrop(kMove)`.
  3. Otherwise the active temporary tab, through `SaveTemporaryTabAtDrop`, as
     in the context menu.

  If the active tab moves, the window follows into the target (handoff 011 S1),
  with source `kKeyboard` as for `SwitchWorkspace`. The window's own Workspace
  and unknown ids are refused, which makes that item disabled.
- `BUILD.gn`: the new sidebar file, and `//base:i18n` for the locale check in the
  controller.

## Checked by this lane

- Every changed `.cc` is syntax-checked with the AhoiDev flags of its target:
  - `command_execution_adapter.cc`, `command_execution_adapter_unittest.cc`
  - `command_bar_controller.cc`, `command_execution_adapter_chromium.cc`
  - `browser_sidebar_host_move_command.cc`, `browser_sidebar_host.cc`
- The line budget holds: `browser_sidebar_host_view.h` is at 799 of 800 lines.
- Not built or run.

## Tests for the owner

- `ahoi_command_bar_unittests`: new
  `CommandExecutionAdapterTest.MoveToWorkspaceItemsRouteToTheDelegate` covers the
  items, both languages, the index accepting them, execution through the
  delegate, refusal, and a malformed id.
- Visible (`CMD-MOVE-01`):
  1. Select a folder with children and run "In Workspace verschieben: <B>".
     The folder and its children are at B's root, and ⌘Z undoes the move.
  2. With the active page in a split, the whole split moves and the window
     follows.
  3. Typing a Workspace name lists the move item next to the switch item.

## Open points

- A lingering folder selection wins over the active tab. The sidebar
  selection is only set by explicit actions (click, reveal, search). If the
  owner prefers the active tab when the tree is not focused, the host can check
  the tree view's focus instead.
- Other Profiles' (`Vollständig getrennt`) Workspaces are not offered, the same
  as "Move to". `WS-ISO-05` reopening belongs to its own step.
