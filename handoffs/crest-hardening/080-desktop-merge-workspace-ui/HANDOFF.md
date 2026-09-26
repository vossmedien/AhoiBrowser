# 080 – Desktop: "Zusammenführen mit …" through the session bridge and sidebar (ADR 0012, part 2)

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `8231b0a` with 078 applied (`git apply` of 078, then 080, passes;
082 also applies on top).
Implements ADR 0012 section 1 on desktop (`WS-MERGE-01`–`05`), with the store
primitive from 078.

## Change (`080-desktop-merge-workspace-ui.patch`)

**Session bridge** (new `session_bridge_workspace_merge.cc`, declarations in
`session_bridge.h`):

- `SessionBridge::MergeWorkspace(source, target, into_folder, done)`.
- When both Workspaces use the default web context (`SharesWebContext`):
  - It commits at once with undo.
  - Bound tabs follow their moved nodes through the existing
    `OnTabTreeChanged` path; an unbound tab of the source is moved explicitly.
- When either Workspace has its own website sessions:
  - Every open page of the source is asked as one before-unload group.
    It reuses `workspace_deletion_close_`, so there is one question at a time.
  - A veto returns `kCancelled` and changes nothing.
  - After agreement the merge commits without undo, and the pages' temporary
    nodes are tombstoned in the same transaction (`closing_temporary_ids`).
  - The pages then close. A page opened while the question was showing closes
    too (handoff 010 R3).
  - The source's binding is retired, and its partition data is cleared after
    3 s, exactly as on deletion.
- Either way:
  - Windows that showed the source switch to the target, not to the first
    Workspace.
  - Link-routing rules and an explicit default route that named the source
    point at the target.
  - `RefreshWorkspaceSnapshot()` runs.
- `OnTabTreeChanged` calls `RefreshWorkspacesAfterUndo()` first for `kUndone`,
  so a Workspace revived by an undo (078) is listed before its tabs follow
  their nodes back.
- The sort key after the target's last root follows the temporary-page
  convention (`last + "@"`, `session_bridge_runtime.cc:288`).

**Sidebar**:

- Workspace menu: a submenu "Zusammenführen mit" / "Merge into" lists the other
  Workspaces of this Profile (new `kMergeWorkspace`, items
  `kMergeWorkspaceCommandBase` 500–599, below the archive policies).
  - A window of a fully separated Workspace has no targets, so the submenu is
    not shown there. Merging across profiles is a later step (ADR 0012,
    `WS-MERGE-04`).
- Dispatch (`browser_sidebar_host_command_dispatch.cc`) and enabled state
  (`browser_sidebar_host_menu_state.cc`) handle the range. The target goes into
  `workspace_dialog_.merge_target_id`.
- Dialog (`PendingWorkspaceAction::kMerge`, body in the new
  `browser_sidebar_host_workspace_merge.cc`):
  - Title "Workspaces zusammenführen", button "Zusammenführen".
  - The text says what happens. With a shared context, open tabs keep running
    and the merge can be undone. With own sessions, the tabs close, the
    sessions are deleted, and there is no undo.
  - A checkbox "Als Ordner „A“ ablegen" is checked by default.
  - Failures other than `kCancelled` go to `OnMutationFailed`.
- `browser_sidebar_host_view.h` stays within the 800-line budget: 799 with 080,
  800 with 080 and 082.

**Tests**: new `session_bridge_workspace_merge_unittest.cc` in
`ahoi_session_unittests`:

- A shared merge keeps the live tab, moves the window and routing to the
  target, and undo brings the source and the tab back.
- An own-sessions source without open tabs is merged without undo, and its
  binding is retired.
- Merging into itself or into an unknown target is refused.

The path with open pages in their own partition needs real renderers, like
the deletion path, which has no unit test either. It belongs to the visible
journey.

Checked by this lane: every changed `.cc` (bridge, observers, unit test and
the five sidebar files) is syntax-checked with the AhoiDev flags of its
target. No errors. Not built or run.

## Open for the owner

- The structure state (split and archive entries) is not rewritten here. The
  nodes keep their IDs and move with the tree. Please check in `WS-MERGE-01`
  that an archive entry and a split of the source still restore after the
  merge (structure refresh on the `kMoved` notification).
- The command id range 500–599 was free at `8231b0a`. Check that no newer
  menu command uses it.

## Tests for the owner

- `ahoi_tab_tree_unittests` (078) and `ahoi_session_unittests` (this handoff).
- Visible, on the installed candidate:
  - `WS-MERGE-01`: folder, split, open tab and archive entry.
  - `WS-MERGE-02`: "Als Ordner" unchecked.
  - `WS-MERGE-03`: ⌘Z in the sidebar.
  - `WS-MERGE-04`: source with its own website sessions and an open login tab.
    Check before-unload, no cookie in the target, and that the dialog text
    matches.
  - `WS-MERGE-05`: kill the browser right after the click; after a restart,
    everything or nothing is merged.
