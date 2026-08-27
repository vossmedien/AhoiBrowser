# Chromium M152 installed-app compatibility smoke

Captured on 2026-08-26 and 2026-08-27 from ARM64 Ahoi development bundles
installed at `/Applications/AhoiBrowser.app` after the M152 source port,
sidebar collapse repair, and the interaction-regression follow-up at source
`f332dd7011c1aca566f919e5d6d955dcfcab7f99`.

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

## 2026-08-27 interaction-regression follow-up

Environment: macOS 26.6 (`25G72`), ARM64, isolated fresh profile
`/private/tmp/ahoi-visible-qa.lZvBjt`, installed development bundle at
`/Applications/AhoiBrowser.app`. The installed executable was independently
verified byte-identical to the built executable, with SHA-256
`1453ef09b84104002f798ee1e83d0788939c5c3e6bc08bb0f09b01a39db6b259`.

- `chrome://version` visibly bound Chromium Stable `152.0.7977.65`, the
  executable below `/Applications/AhoiBrowser.app`, and the isolated profile.
- Entering `fpn-dichtstoffe.de` in the normal address bar and in the exact
  Command-T quick-window flow selected and opened FPN, rather than an unrelated
  history result.
- Appearance exposed independent CloudKit synchronization and developer-tools
  switches. Enabling developer tools immediately restored the developer helper;
  `Darstellung -> Entwickler -> Entwicklertools` was visible and opened the
  docked DevTools UI.
- The copy control was exposed as `URL kopieren`, without an accelerator `&`.
- The floating sidebar visibly retained equal outer gutters, closed rounded
  corners, and circular header controls. Revealing the floating address bar put
  it in the top layer over the sidebar without resizing the page viewport.
- No global `Sync ist ausgeschaltet` copy appeared. Synchronization remains an
  explicit Appearance switch and a scoped sidebar control.
- Computer Use attempted the same coordinate drag on two open-tab rows in both
  floating and docked modes. Its single-step drag primitive did not start a
  native AppKit drag in either mode, so that attempt is deliberately not called
  `CU_E2E PASS` or a floating-only product failure. The retry-disabled browser
  test `AhoiFloatingSidebarOwnsItsNativeDragRoute` passed and directly proves
  the native drag source, New Group target, floating address-bar overlap,
  row/gap/content routing, and clean drag termination.

The source-matched focused gate also passed 402/402 tests across 11 unit-test
binaries and 14/14 tests across five browser-test binaries without retries or
skips. The official wrapper then completed the source-matched app build; the
actual incremental Ninja phase linked eight targets rather than rebuilding all
of Chromium.

## Captures

| File | SHA-256 | Scope |
| --- | --- | --- |
| `command-bar.png` | `d871f4bb611438b46513a26da2fb032e0c9eb1786fad5b9c5d8c2de88198bd82` | Command-bar rendering |
| `appearance-settings.png` | `1970b52bd35d9fd45f5100103b33ea9e223cf1f84ee9bbe13267c7f5ef46529e` | Appearance settings |
| `sidebar-hidden-fixed.png` | `89a00434fd347a3067ea6483e9531a9098513acd61039455ce988d09ab148dc3` | Post-fix collapsed state |
| `sidebar-restored-fixed.png` | `f81c629bcacf217a3c3542462acdd95c1226e5faa0ef2794112f133d57eee460` | Post-fix restored state |
| `version-page-fixed.png` | `b6cfe5af1892226796dbce5677858891c1b85bfbd8ac47597083c4e98e038d4f` | Post-fix installed version/path |
| `version-page.png` | `499964a3638914d8a9c42313ee7e63a40e817cf487a3bdca999e8345352bce13` | Initial M152 version/path capture before the sidebar fix |
| `regression-20260827/installed-version.jpeg` | `26962667a23b6a3de357c62c15df9a15c0d8b37587c1de009e873118923c228b` | Installed Chromium version, executable, and isolated profile |
| `regression-20260827/quick-window-exact-url.jpeg` | `33d13d9a2e95e213acab74b0168e2faa40ddd906dae5c80751e48e8e0489ce0d` | Exact Command-T URL resolution for FPN |
| `regression-20260827/appearance-developer-enabled.jpeg` | `31f811a6bc190c2f032dfb3a9c07ea047c82626f985c2a1566d645349c803dcc` | CloudKit and developer settings, developer tools enabled |
| `regression-20260827/devtools-open.jpeg` | `bc09df459dc6d49c4bb7567f075c05eea9d0173ef55701edff71cbce7389e2da` | Docked DevTools opened from the restored Developer menu |
| `regression-20260827/floating-sidebar.jpeg` | `42fa922a4a749719072c9b05bb48025fb1dab38444be2dfb272966c1a6beb07f` | Floating-card gutters, rounded corners, and page overlay |
| `regression-20260827/floating-address-bar-layer.jpeg` | `339a89432da0ed8272d4e30dc4207325365081fc796f9151826cc95a8697393c` | Revealed address bar layered over the floating sidebar |

This is scoped development compatibility evidence. It does not replace the
master target's requirement-by-requirement `CU_E2E PASS`, assisted device
coverage, release signing, notarization, updater, performance, or soak gates.
