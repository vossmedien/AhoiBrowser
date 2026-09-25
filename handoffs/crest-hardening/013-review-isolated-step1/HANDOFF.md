# 013 – Review of ADR 0011 step 1 (fully separated Workspaces)

Status: integrated 8a9fc91 (I1, I2; I3 by visible test; I4 deferred)
Owner lane: desktop
Reviewed: `671796d`, `9b308f0`, `08f5518` (sync), `0a2b4c0`. Source reading
only; nothing was run.

## Matches the contract

- The Workspace is registered in Local State before the Profile exists. The
  Profile is created through `ProfileManager` without sign-in or the profile
  picker, in its own window, and its `SessionBridge` seeds the registered
  Workspace.
- Deletion asks all pages as one before-unload group. It then records
  `kDeleting`, marks the Profile ephemeral (startup cleanup finishes it after
  a crash) and hands it to Chromium's profile deletion. The dialog names
  exactly what is deleted.
- No sync service until step 3 (`08f5518`), so there is no write into the
  main CloudKit namespace.
- The creation dialog states the level and the limit ("kein Schutz vor
  anderer Software auf diesem Mac").

## Findings

**I1 (high, product gap) – No way back into a closed separated Workspace.**
Step 1 offers no switcher. Once its window is closed, or after a restart
where it was not the last active window, the main window lists no separated
Workspace. Unless Chromium's own profile menu or Dock menu shows it (please
verify; Ahoi may hide that surface), its data is unreachable until step 2.
Minimal fix: a "Getrennte Workspaces" section in the main window's
Workspace menu that opens the Profile's window
(`profiles::OpenBrowserWindowForProfile`).
- **WS-ISO-14**: create, close its window, restart, reopen from the main
  window; history and logins are still there.

**I2 (medium) – A crash while creating leaves an entry that is never cleaned
up.** The entry is `kCreating` and `storage.AddProfile` has run. It becomes
`kActive` only when that Profile's `SessionBridge` initializes, which
happens only if its window opens. `SweepIsolatedProfileRegistry` removes only
entries whose Profile attributes are missing, so a half-created Profile and
its entry stay behind indefinitely.
Fix idea: at the main Profile's startup, either finish opening `kCreating`
entries or delete them (mark ephemeral and schedule deletion).
- **WS-ISO-15**: kill the process between `AddProfile` and window creation;
  the next launch leaves either a working separated Workspace or no trace.

**I3 (medium) – Deleting from the last open window.**
`MaybeScheduleProfileForDeletion` of the only Profile with open windows makes
Chromium pick another Profile to show. With the startup picker disabled, the
main Profile's window must open and no Chromium picker or "new profile"
flow may appear.
- **WS-ISO-16**: only the separated window is open; delete it; the main
  window appears and no Chromium profile UI does.

**I4 (low) – Pages outside the tab strip.** As in 010 R5, popup overlays and
Quick Window pages of that Profile are not part of the group question.
Chromium's profile deletion closes them without asking.

**I5 (info)** – `DeleteIsolatedWorkspaceProfile` uses `GroupPageClose::Ask`.
Its holder is deleted through `DeleteSoon`, so no use-after-free occurs even
with the synchronous overlap path. The 010 R1 patch still applies.

## Owner intake (desktop, 2026-09-25)

- I1: the main window's Workspace menu lists all fully separated Workspaces
  and opens their windows (`OpenIsolatedWorkspace`), WS-ISO-14 journey pending.
- I2: at startup a `creating` entry becomes active if its tree was written,
  otherwise its Profile is marked ephemeral and deleted (WS-ISO-15 pending).
- I3: verified by the visible WS-ISO-16 journey on the next candidate.
- I4 deferred with 010 R5 (pages outside the tab strip).
