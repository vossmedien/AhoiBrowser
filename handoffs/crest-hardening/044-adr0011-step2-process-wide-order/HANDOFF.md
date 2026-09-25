# 044 – ADR 0011 step 2: one process-wide Workspace order (data)

Status: integrated 5be0782 (build 32; unit tests green; visible check pending)
Owner lane: desktop (apply, build, test)
Base: HEAD `663398a` (`git apply --check` passes on HEAD and the current
worktree). First of the step 2 handoffs by the crest-hardening lane
(044 order, 048 switcher, 050 Quick Window, 052 conversion); not compiled
by the lane.

## Gap

ADR 0011: "A process-wide Workspace directory lists all Workspaces of all
Ahoi Profiles in one order". Today every surface concatenates: the Profile's
own `ordered_workspaces()` (tree `sort_key`), then separated Workspaces
in registry (creation) order (`GetOpenableIsolatedWorkspaces`, context menu
`:267-333`, link routing `:282/:352`, settings `:61-67`). A separated
Workspace has no position of its own, so it can never sit between two main
Workspaces, and the order differs between surfaces.

## Change

- `IsolatedProfileEntry::sort_key` (optional `sort_key` string in the
  Local State entry; not written when empty, so old builds read new entries
  and new builds read old ones).
- New pure `workspace_directory_order.{h,cc}` in `session_preferences`:
  `OrderDirectoryWorkspaces(main, isolated_entries)` merges by key (main
  first on equal keys; entries being deleted skipped; unkeyed legacy entries
  last in registry order, which is where they appear today) and
  `NextDirectorySortKey` (greatest key + '@', "00000000" when empty: the
  existing scheme of `SessionBridge::CreateWorkspace`).
- `CreateIsolatedWorkspace(..., std::string sort_key, done)`; the sidebar
  dialog passes `NextProcessWideWorkspaceSortKey()`, which reads the main
  Profile's Workspaces through `GetLoadedMainProfile()` (also from a
  separated window).
- `SessionBridge::CreateWorkspace` appends after the merged order, so a
  main Workspace created later also follows existing separated ones.

No surface changes its display in this handoff; 048 switches the switcher,
keyboard cycling, command bar, context menu and routing lists to
`OrderDirectoryWorkspaces`. The keys are local (Local State); syncing them
belongs to step 3.

## Tests

- `WorkspaceDirectoryOrderTest` (4 cases, new file, added to
  `ahoi_session_unittests`).
- `IsolatedProfileRegistryTest.RoundTripsSortKey`.
- Visible: none yet (no display change); covered by 048's journey.
