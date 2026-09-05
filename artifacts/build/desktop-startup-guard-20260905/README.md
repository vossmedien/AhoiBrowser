# Startup/fullscreen failures and exact isolated corrections

## Current result: c986090 starts, fullscreen is red

Guarded c986090 build59738 and installer3416 both exited0. Build receipt
`artifacts/build/ahoi-dev-build-c986090d99c2.json`, SHA256
`0ffac1eb62d5289eab473e9a95ce1f6154bb249cf37d85171febc6e9a03e7010`.
Install receipt `artifacts/install/ahoi-dev-c986090-20260905T201925Z.json`, SHA256
`922b7e32190f21007697ce96e3580c58d200e103960281a0e1f462b9893d4ffb`.
The filename is only an immutable identifier; actual build/install timing is
in the receipts, not inferred from that filename. Executable SHA256
`dbd2c25bb8e22af6cd2424db7fa967972fa19f178031fde8c269eac2925c9cc5`, tree SHA256
`b41ad737b6c603e08f45b973d600061cc7a940eb2eb2b720fe20067328d1ae1e` match both
receipts. RENAME_SWAP, process quiescence and post-install verification true.
Prior926 is retained at the install receipt's exact rollback path.

Native CUA launched the installed app, displayed the startup choice, then
continued the existing session. The real NTP/sidebar/toolbar rendered. Cmd+N
opened a second rendered browser window. Pressing its native green window /
fullscreen button terminated the process. ReportCrash finished a new report:
`artifacts/diagnostics/desktop-c986090-fullscreen-20260905/AhoiBrowser-2026-09-05-222110.ips`
(local raw evidence, not published), SHA256
`e4c2bea1c53df8567871ff22a9494608e22da0c59900314419ee7887b67524fd`.
Capture20:18:25UTC, launch20:17:43UTC. EXC_CRASH/SIGABRT at
CustomCornersBackground::Paint, custom_corners_background.cc:277:
`CHECK(!view->layer()->fills_bounds_opaquely())`. The stack also records the
native zoom-widget accessibility press. This is not the prior ctor null access.

Source cause: fullscreen sets the navigation surface to opaque/radius0 and the
generic material helper consequently advertises full opaque coverage. The real
ToolbarView retains Chromium's cutout-capable CustomCornersBackground, which
requires non-opaque layer coverage even for opaque material. Canonical7de7fac
corrects only that owner-side flag and adds one assertion to each of the two
existing regressions. The CHECK/painter and upstream fullscreen layout are not
weakened. No new test matrix. No programmatic tests or Arc UI mutation ran.

Exact next clean snapshot4cb622a = c986090 plus ONLY those three owned files,
in the same existing disposable worktree. Product build first, then atomic
installation and this exact visible launch/new-window/fullscreen journey again.
New workspace-session requirements and Unified Sync WIP remain outside it.

Guarded overlay31405 and product-only build73875 subsequently exited0. The
source-bound build receipt `artifacts/build/ahoi-dev-build-4cb622a0bffc.json` was
copied byte-identically from the clean snapshot, SHA256
`2d94480da797419e9ab9a755a73fbfffff2de4fb822399c0b352a6a2e3f9de76`.
Signed executable SHA256
`55301ccbda32e32d3ee57420bd10adc3581b96a918047dbbb82815a56134770b`.
This build contains the product, not newly rebuilt test executables. Its initial
installation was held at the fresh CPU gate (FillIt Unity18495 at118.7–119.6%).
No foreign process stop and no4cb runtime pass are implied.

Publication note: the signed follow-up carrying this note attests the author's
DCO for the AI-assisted owned changes in7de7fac,390bb2c and296571f. Those already
published commits are not rewritten. This does not claim that their original
per-commit DCO-trailer check passed; subsequent commits use `git commit -s`.

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
