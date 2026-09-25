# 003 – Deleting a Workspace with its own website sessions (H5, H6)

Status: ready
Owner lane: desktop (session, native isolation)
Base: `f811604`; gate for enabling `AhoiWorkspaceWebsiteSessions` (ADR 0011, order step 1)

## Finding

`SessionBridge::DeleteWorkspace`
(`overlay/chromium/src/ahoi/browser/session/session_bridge_workspace.cc:206-251`)
marks the tree records as deleted. It then reassigns every open runtime tab of
the deleted Workspace to the first remaining Workspace
(`runtime.workspace_id = fallback->id; PersistTabSessionMetadata(tab)`).
Those tabs keep their `WebContents`, which live in the deleted Workspace's fixed
`StoragePartition`. `PersistTabSessionMetadata` derives the binding from the
WebContents (`session_bridge_session.cc:84-86`). So the fallback Workspace now
shows, and after restart restores, pages logged into the deleted Workspace's
accounts. This contradicts `docs/WORKSPACE_SESSIONS.md`, which says an
already-open page's local account must never switch silently.

The entry in `ahoi.session.website_session_bindings` is never removed, because
`session_prefs.cc` has no removal function. No Ahoi code clears or deletes the
partition's data, so its cookies and site storage stay on disk indefinitely,
and restore still accepts the binding (`IsKnownWebsiteSessionBinding`).

The flag is off by default, so installed users are not affected today.

## Required behavior (contract, before the flag may be enabled)

1. Deleting a Workspace whose binding is an isolated partition asks the user.
   It then closes that Workspace's open tabs through the normal
   `TabStripModel` close path with before-unload, as a group. A veto cancels
   the whole deletion and leaves all state unchanged (see handoff 006 for the
   group check).
2. Only tabs whose binding is the default partition may move to the fallback
   Workspace.
3. After the tree commit, remove the binding from
   `ahoi.session.website_session_bindings`. Then clear the partition's data
   through the native `StoragePartition` clear path, and remove its directory
   or schedule its removal at the next launch.
4. Record the deletion intent before step 3 and resume it at startup, so a
   crash cannot leave a half-deleted partition that restore still accepts.
5. Workspaces stored only in the archive or in structure sync keep their tree
   records per the existing retention rules; website data is never retained
   for a deleted Workspace.

## Acceptance cases

- **WS-DEL-01**: Log in to site X in Workspace B (own sessions) and delete B.
  In the fallback Workspace A, X is not logged in as B's account, and none of
  B's pages are open there.
- **WS-DEL-02**: B has a tab with a before-unload handler. Delete B and cancel
  the prompt. B, its tabs and its data are unchanged.
- **WS-DEL-03**: Delete B and restart. The binding pref no longer contains B,
  and B's partition directory is gone or scheduled for removal.
- **WS-DEL-04**: Kill the process between the tree commit and the partition
  cleanup. The next launch finishes the cleanup; restore never reopens a page
  in B's partition.
- **WS-DEL-05**: A shared-level Workspace behaves as today: its tabs move to
  the fallback.
