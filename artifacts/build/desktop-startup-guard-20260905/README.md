# Installed 92694fe startup failure and isolated correction source

## Verified build/install, failed visible startup

Full guarded build17302 / source92694fe exited0. Portable runtime and stable
Apple Development signature were verified. Canonical build receipt:
`artifacts/build/ahoi-dev-build-92694fe36539.json`, SHA256
`dec35aefe6a4095fa784dcd5f2cf186a8005e9ac53c5f81e4b6ed4163f3772a1`.
Atomic install19738 exited0, receipt:
`artifacts/install/ahoi-dev-92694fe-20260905T190452Z.json`, SHA256
`502d6ac0bb82153c9e227534a20034785621e1e56ec4f842c3eb7678676d00aa`.
Installed source, executable and complete bundle tree match that signed build:

- source: `92694fe36539d024af6567646103f1cf246d5364`
- executable: `717827e792a4882665334dc834ec54680696ae8b2cc93a88399b176741b945ca`
- tree: `083286c952f1b6c542c8177ca9c5cf1c8c9f7edc092b11ac5b8525c753620b6f`
- activation: renameatx_np(RENAME_SWAP), post-install verification true.
- prior3d bundle retained at the rollback path recorded by the installer.

The first native CUA launch timed out and the process was absent afterward.
Finder native control worked. After resetting only the CUA JS session to reload
its API documentation, an own Finder window was opened at /Applications and
the observed exact AhoiBrowser.app item opened through its native Open action.
That launch also terminated. Two new .ips reports confirm the same actual
startup crash, not merely a CUA-pipe failure. No further launch is attempted on
926 and no programmatic test suite or import/recovery UI action was executed.
Profile initialization may already have occurred; no whole-profile no-write
claim is made. The Arc journal/backup must remain preserved.

Both reports: EXC_BAD_ACCESS/SIGSEGV, address0x160, main browser thread:
BrowserView constructor -> NavigationSurfaceController constructor's initial
appearance callback -> BrowserView::IsFullscreen -> Widget::IsFullscreen.
The weak BrowserView is live, but its widget is not attached/owned yet. This is
the Desktop patch0031 callback, not a Sync, provisioning or Chromium-pin finding.

Raw reports are retained locally under ignored
`artifacts/diagnostics/desktop-92694fe-startup-20260905/`, not published as profile
data. SHA256:

- AhoiBrowser-2026-09-05-211017.ips:
  `ff51e85a61c56f0b753657809d5edbfebdefeab65b8b74480eebfaa341f6b3c6`
- AhoiBrowser-2026-09-05-211538.ips:
  `cdb75a1a958c8972f383a1d90c3a9d8597dc2a0f60fa9aa72f174cff0d0ebd98`

## Correction and exact isolated source

Canonical main-branch fix `88ebbe9` guards both GetWidget() and browser_widget()
before IsFullscreen(), without changing appearance, clipping or fullscreen
policy. Independent narrow review and main source readback confirmed material
is stored before the early callback and replayed after native layout/painter
initialization; the extra owner-pointer guard also covers teardown ordering.

New real-browser regression creates a second browser, verifies native material,
color/alpha/radius before any preference/mode repair, enters/exits fullscreen,
checks restoration and closes normally. This is test SOURCE, not a pass.

Newer Common interface/golden commits are outside this UI baseline. The existing
clean detached build worktree therefore cherry-picks ONLY the owned two-file
fix on926; no second worktree or active development branch was created:

- candidate source: `c986090d99c22318aef4d45378208cece6878d27`
- base prerequisite: `92694fe36539d024af6567646103f1cf246d5364`
- same code change already committed/pushed on main as88ebbe9.
- retained canonical-repository ref:
  `refs/ahoi/build-candidates/desktop-startup-c986090`
- `source.bundle` contains that exact derived commit and requires926; git bundle
  verify passed. SHA256:
  `5da4ec4a935886ee8cba5ea83ce53189622c454e6c193cd4847dae2e03f94779`

The bundle preserves exact candidate provenance after disposable-worktree cleanup;
it is not a second patch stack or a release. All uncommitted Common/Swift/
entitlement work remains untouched. Next: guarded cached build/install of this
candidate after a fresh CPU gate, then repeat visible startup first. No tests
are a substitute for that red startup journey.
