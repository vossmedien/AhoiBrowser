# Extension menu crash recovery evidence

Date: 2026-08-29
Installed candidate at diagnosis: `/Applications/AhoiBrowser.app`
Chromium baseline: `152.0.7977.65`

## Reproduction and crash signature

The user reported that an attempted AnyChat installation was followed by a
browser crash after the extension-toolbar button was clicked. The one fresh
matching macOS incident is
`AhoiBrowser-2026-08-29-233407.ips`, incident
`9C6FABD7-3DC5-48FD-B451-191125E393DA`.

The crashing main-thread path is:

1. `ExtensionsToolbarButton::ToggleExtensionsMenu()`
2. `ExtensionsMenuCoordinator::Show()`
3. `ExtensionsMenuDelegateDesktop::OpenMainPage()`
4. `ExtensionsMenuViewModel::GetOptionalSection()`
5. `GetMainPageState()` with a null active `WebContents`

The invalid address is `0x0`. This is an Ahoi zero-tab lifecycle bug in the
Chromium extension-menu view model. The crash report does not establish that
AnyChat extension code ran, and there is no second fresh AhoiBrowser crash
report for a separate install-time failure.

## Read-only profile inventory at diagnosis

The existing Ahoi `Default` profile contains:

| Extension | Store ID | Version | Manifest |
| --- | --- | --- | --- |
| 1Password - Password Manager | `aeblfdkhhhdcdjpifhhbdiojplfjncoa` | `8.12.34.34` | MV3 |
| uBlock Origin Lite | `ddkjiahejlhfcafbddmgiahcphecmpfh` | `2026.825.1619` | MV3 |

AnyChat (`khpefodpgnkegiohbolbaaeabnfdegln`) and uBlock Origin Classic
(`cjpalhdlnbpafiamejdnhcphjbkeiagm`) are absent from the extension directory
and extension settings. Therefore:

- the attempted AnyChat installation did not persist;
- uBlock Origin Lite is installed, not uBlock Origin Classic;
- Lite must not be reported as a successful Classic installation.

Only the AhoiBrowser profile was inspected. No Chrome or Arc profile was
modified.

## Recovery contract

Patch `0009-ahoi-empty-surface-extension-menu.patch` keeps generic extension
identity and management available in a true zero-tab Ahoi window while failing
closed for every page-bound action and permission control. It adds a focused
browser regression for that state.

Required acceptance order:

1. Build, sign, and install the exact candidate.
2. Open and reopen the extension menu in a real zero-tab window containing the
   existing extensions; confirm the installed app remains alive.
3. Activate a normal page and confirm the menu updates to page-bound state.
4. Only after visible installed-app acceptance, run the focused browser test and
   ordered patch-composition checks.
5. AnyChat installation itself requires visible source/ID/permission review and
   an explicit user confirmation at the installation action.

Status at creation: implementation present; installed-app and test acceptance
still pending.
