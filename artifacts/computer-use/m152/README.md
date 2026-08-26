# Chromium M152 installed-app compatibility smoke

Captured on 2026-08-26 from the ARM64 Ahoi development bundle installed at
`/Applications/AhoiBrowser.app` after the M152 source port and sidebar collapse
crash fix.

## Observed results

- `chrome://version` visibly reported Chromium `152.0.7977.65`
  (`Developer Build`, ARM64), V8 `15.2.124.18`, and the executable below
  `/Applications/AhoiBrowser.app/Contents/MacOS/AhoiBrowser`.
- Four fresh sidebar collapse/restore cycles completed. Each cycle used the
  native `Sidebar ausblenden` control and restored the sidebar with
  Command-Shift-H while the installed browser remained responsive.
- Native accessibility increment/decrement actions resized the sidebar from
  370 to 420 points and back to 370 points.
- No new Crashpad dump appeared after the fix. The newest pending dumps remain
  the two pre-fix reproductions from 19:02:24 and 19:03:33 local time.
- The command bar and appearance settings were also visibly inspected.

## Captures

| File | SHA-256 | Scope |
| --- | --- | --- |
| `command-bar.png` | `d871f4bb611438b46513a26da2fb032e0c9eb1786fad5b9c5d8c2de88198bd82` | Command-bar rendering |
| `appearance-settings.png` | `1970b52bd35d9fd45f5100103b33ea9e223cf1f84ee9bbe13267c7f5ef46529e` | Appearance settings |
| `sidebar-hidden-fixed.png` | `89a00434fd347a3067ea6483e9531a9098513acd61039455ce988d09ab148dc3` | Post-fix collapsed state |
| `sidebar-restored-fixed.png` | `f81c629bcacf217a3c3542462acdd95c1226e5faa0ef2794112f133d57eee460` | Post-fix restored state |
| `version-page-fixed.png` | `b6cfe5af1892226796dbce5677858891c1b85bfbd8ac47597083c4e98e038d4f` | Post-fix installed version/path |
| `version-page.png` | `499964a3638914d8a9c42313ee7e63a40e817cf487a3bdca999e8345352bce13` | Initial M152 version/path capture before the sidebar fix |

This is scoped development compatibility evidence. It does not replace the
master target's requirement-by-requirement `CU_E2E PASS`, assisted device
coverage, release signing, notarization, updater, performance, or soak gates.
