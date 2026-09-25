# ADR 0011: Optional fully isolated Workspaces backed by their own profile

Status: accepted 2026-09-25 (user request: Workspaces optionally separated as
completely as profiles). Extends ADR 0002 and `WORKSPACE_SESSIONS.md`; does not
replace them. Analysis:
[crest-hardening-2026-09-25-workspace-isolation](../reviews/crest-hardening-2026-09-25-workspace-isolation.md).

## Decision

Every Workspace has one isolation level, chosen when it is created:

| Level | Name in UI | Technical boundary | Shared with other Workspaces |
| --- | --- | --- | --- |
| `shared` | Gemeinsam (default) | the normal Profile (ADR 0002) | everything |
| `website-sessions` | Eigene Website-Sitzungen | fixed `StoragePartition` in the normal Profile (`WORKSPACE_SESSIONS.md`, package 1b) | history, passwords, extensions, settings |
| `isolated` | Vollständig getrennt | its own Chromium `Profile` | nothing except the app, its updater and the macOS account |

`isolated` uses Chromium's native Profile separation. Cookies, site storage,
history, passwords/autofill, extensions and their storage and grants,
permissions, settings, bookmarks, download history and HTTP-auth credentials
are separate because upstream separates them. Ahoi builds no parallel stores.

## Consequences

- One isolated Workspace maps to one Profile. It is created through
  `ProfileManager` without Google sign-in or the profile picker. Deleting the
  Workspace, after explicit confirmation, schedules deletion of the Profile
  through Chromium's profile deletion path. The deletion intent is persisted
  and resumed after a crash.
- Ahoi's profile-keyed services (tree, Workspaces, session, command,
  sync, HTTP auth, uBO, importer, resource policy) run per Profile. A
  process-wide Workspace directory lists all Workspaces of all Ahoi Profiles
  in one order for the sidebar, the switcher, the command bar, link routing and
  Quick Window. Switching to a Workspace of another Profile presents that
  Profile's window with the same frame and hides the previous one, without web
  reflow.
- `WebContents` never changes Profile. Moving a tab, split or subtree across a
  level boundary reopens its URL in the target and states that sessions do not
  move. A split never mixes Profiles.
- Converting an existing Workspace to `isolated` is an explicit move that
  uses the portable Workspace export/import. Structure moves; logins,
  passwords and site data do not.
- Profiles load on first use and unload when no window of theirs is open.
  Their memory cost is measured under `docs/PERFORMANCE_METHODOLOGY.md`.
- Sync stays opt-in per Profile. Each Profile uses its own CloudKit namespace
  within the same account. Website data, passwords and permission grants are
  never synced. Mobile preserves isolated Workspaces' metadata and presents
  them with a separate `WKWebsiteDataStore`. Its exact shape belongs to the
  Sync and Mobile owners.
- The Arc importer maps an Arc profile to an `isolated` Workspace when the user
  chooses to.
- The UI states the level of every non-shared Workspace in its menu and
  creation dialog. `isolated` is a data boundary inside the browser, not a
  security boundary against software running as the same macOS user.

## Order

1. Level `website-sessions` finishes package 1b, including the deletion defect
   (open tabs of a deleted isolated-session Workspace must close with
   before-unload, and the binding and partition data must be removed).
2. `isolated` step 1: create, open, delete, and separate world per Profile.
3. `isolated` step 2: shared switcher, window hand-over, routing, Quick Window,
   import/export.
4. `isolated` step 3: per-Profile sync namespace and Mobile presentation.

## Open for the user

Whether level `website-sessions` still needs its own per-Workspace permission and
`chrome.cookies` scoping patches, or documents those as shared because
`isolated` provides full separation.
