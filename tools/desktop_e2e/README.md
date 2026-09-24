# Desktop E2E helpers (PID-scoped)

`axtool.swift` drives one exact AhoiBrowser process through the macOS
Accessibility API (`AXUIElementCreateApplication(pid)`) and posts keyboard/mouse
events only to that PID (`CGEvent.postToPid`). System Events cannot tell an
isolated test clone from the installed `/Applications/AhoiBrowser.app` because
both are named `AhoiBrowser`; never use name-based System Events input here.

`empty-workspace-navigation.sh <App.app> <outdir>` launches the given bundle
with a disposable `--user-data-dir`, `--remote-debugging-port=9344` and
`AhoiWorkspaceWebsiteSessions`, creates two empty Workspaces through the real
Workspace context menu and dialog, opens a page with ⌘T, then uses ⌘L from the
second, tabless Workspace. `verdict.json` passes only when no existing tab's URL
changed and exactly one new tab holds the typed URL. Tab state comes from
Chromium DevTools `/json`; UI state from the Accessibility tree. It needs the
Accessibility permission for the host terminal. It does not capture images
(Screen Recording is an owner-gated permission).

Baseline: on signed `79a7752` (before ordered patch `0054`) the journey fails
with `hiddenTabsNavigated` = the Leer-Test tab, matching the recorded manual
negative.
