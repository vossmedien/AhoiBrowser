# 052 – ADR 0011 step 2: convert a Workspace into a fully separated one

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `eb9277f` (contains 044–054 via `5be0782`; `git apply --check`
passes on HEAD and the current worktree). By the crest-hardening lane; not
compiled. Largest step 2 piece (~550 lines); review before applying.

## Gap

WS-ISO-09 and ADR 0011 ("Converting an existing Workspace to `isolated` is
an explicit move that uses the portable Workspace export/import"). No
conversion exists; the creation dialog says the level cannot change later.

## Flow

1. Workspace menu (main windows, ≥2 Workspaces): "In vollständig getrennten
   Workspace umwandeln …" → dialog stating what moves (folders, order,
   splits, homes, archive) and what does not (logins, passwords, history,
   site data), that open tabs close here and temporary tabs reopen there,
   and that Cancel changes nothing.
2. `SessionBridge::ConvertWorkspaceToIsolated(W)`: all open pages of W are
   asked as one before-unload group (`GroupPageClose`); a veto →
   `kCancelled`, nothing changed.
3. Export: `SelectPortableWorkspaceStructure(tree, structure, {W},
   temporary=false, archives=true)`. The imported Workspace record is
   aligned to the new Profile's seed (`sort_key` "0", default archive
   policy) so `AnalyzePortableWorkspaceDestination` sees it as identical;
   the real policy is applied after the import, and W's process-wide
   position becomes the registry `sort_key` (044).
4. `ConvertToIsolatedWorkspace` registers the Profile with **the same
   Workspace id W** in the new state `kConverting` (not openable, not
   listed, not routed; main's W is still the target) and creates it; the
   pending structure waits in memory by directory.
5. The new Profile's bridge (bootstrap sees `kConverting`) takes it,
   `CommitPortableWorkspaceImport` (retried while `kUnavailable`, 40 ×
   250 ms), applies the archive policy, sets `kActive`, reopens the
   temporary pages' URLs in its window, and reports success.
6. Only then the main bridge deletes W (`CommitWorkspaceDeletion`) and
   closes the agreed pages. Routing rules keep working because the id is
   unchanged.

Failures: creation or import failure → the new Profile deletes itself, W
stays (`kFailed` shown as a mutation error). Restart during the
conversion → the in-memory payload is gone; the sweep (not loaded) or the
Profile's own bridge (loaded) deletes the `converting` Profile, W stays.

## Known limits (stated, not hidden)

- Saved pages that were open are closed and stay as saved rows in the new
  Profile (not reopened), to avoid duplicate temporary rows.
- A crash after the new Profile became active but before W's deletion
  leaves both (same id; the switcher shows the main one). Very narrow
  window; a persisted "source pending deletion" marker would close it.
- The new Profile opens its own window like "create", not a hand-over.
- Older builds skip registry entries in state 3 (`Decode` bound), which is
  the safe direction.

## Tests

- `IsolatedProfileRegistryTest.RoundTripsConvertingState`;
  `WorkspaceDirectoryOrderTest.UnkeyedFollowAndDeletingIsSkipped` now also
  skips `kConverting`.
- No unit harness spans two Profiles. Visible journey (WS-ISO-09): shared
  Workspace with folders, a split, a home URL, one archive entry, one open
  saved page and one open temporary page with a before-unload form.
  (a) Convert → "Stay" on the form: nothing changes. (b) Convert → "Leave":
  a new separated window shows the same tree, split, home and archive; the
  temporary page is reopened (logged out); the main window lost W and its
  tabs; W's routing rule opens in the separated Workspace; the switcher
  shows it at W's old position. (c) Quit during step 5 (slow import):
  after restart W is intact and no half Profile remains.
