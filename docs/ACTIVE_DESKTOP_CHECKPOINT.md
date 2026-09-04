# Active Desktop checkpoint

Last updated: 2026-09-04

This file is the restart and compaction checkpoint for the active Desktop work.
Do not replace this execution boundary with another feature inventory or a new
planning pass. The complete product backlog remains authoritative in
`outputs/AhoiBrowser-Master-Zielprompt.md`, but it is frozen until the package
below reaches its exit criteria.

## Locked package

Close this package in order:

1. Build the pending Arc Settings accessibility correction and prove that the
   Arc preview and result remain alive when macOS accessibility reads them.
2. Install AnyChat only through Chromium's normal Chrome Web Store flow; cover
   cancel, success, action/Side Panel, `Command+Shift+S`, disable/enable and
   restart. Do not add an Ahoi-specific AnyChat installer.
3. Install uBlock Origin Classic through Ahoi's single explicit action for the
   pinned Official GitHub release, then accept Chromium's separate permission
   prompt. Prove filtering, dashboard and restart readiness.
4. Only after Classic is ready in a later browser process, request and execute
   the separately confirmed removal of uBlock Origin Lite. Never remove Lite
   automatically or after a failed/cancelled Classic installation.
5. Run the focused tests for the exact corrected source only after the visible
   installed journeys are green.
6. Record evidence, update status, commit and push only the owned Desktop files.
   Do not stage, rewrite or claim the separate Mobile work.

## Exact current boundary

- Canonical repository:
  `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`
- Branch: `codex/desktop-core-feature-wave-20260830`
- Installed candidate source: `f1475d6d6937aa832c745a58d18750bea8359ba8`
- Chromium: `152.0.7977.65` at
  `fc4d67f1788019a27e32511137ceccbd2fafdaaa`
- Installed executable SHA-256:
  `a29b95e2cad7ed28295a66e9e95b6265ef126ecaea5dd4ac2bbd1fa46c48a32a`
- Installed bundle-tree SHA-256:
  `603459b30acb07e436b868e8ac7b1035570660db98e1936c00c02bd357de7029`
- Installed candidate evidence:
  `artifacts/build/installed-ahoi-dev-f1475d6d6937-20260904T103443Z.json`
  (SHA-256
  `d14f3f089e17a1f17d6f969625913998445bb05f6501cc0fba7d898c351d8ced`).
  This development receipt binds the installed executable and exact app-tree
  hashes to clean Desktop source `f1475d6`.
- Exact clean build evidence:
  `artifacts/build/ahoi-dev-build-f1475d6d6937.json` (SHA-256
  `3075e930e29249a9fe1be5e156fe397da96b3ee310064729af9644f930c546b7`).
  The combined incremental build completed all requested package targets,
  staged 533 component dylibs and 238 framework resources, signed with the
  stable Apple Development identity, and passed strict deep code-signature and
  installed-app verification.
- The installed app contains the Arc accessibility correction and the later
  inactive-workspace split reconstruction fix from Desktop commit `285990c`.
  Exact-tree uBO attestation hardening is integrated in Desktop commit
  `69b1720`. The native-sheet handoff correction and its regression coverage are
  integrated in Desktop commit `f1475d6`. The final candidate was generated
  from a clean detached authority worktree, installed atomically and verified
  byte-for-byte against its build and installation receipts.
- Existing visible passes on the previous `ab0d97d` candidate: exact installed executable,
  cold/empty start, docked/floating sidebar persistence, zero-tab extension
  menu, zero-tab two-pane split and public HTTPS split.
- Reproduced red journey: Arc preview caused a Settings renderer accessibility
  DCHECK (`ax_object.cc:4814`, prohibited accessible name from a WebUI `dt`).
  The browser process remained alive.
- The Arc correction is committed in `5cb95ff`: replace both Arc count
  definition lists with neutral list items and assert that `dt`, `dd` and
  accessibility role `term` are absent.
- The first combined build exposed an unrelated M152 lean-PDF TypeScript flag
  gap before any candidate was produced. Its reproducible fix is committed in
  `9a0de8b` plus the final strict unused-import guard in `19a71b8`; the full
  Chromium patch reverse-applies cleanly. The next build exposed two unnecessary
  non-null assertions in the new Arc test; their lint-only correction is
  committed in `f630c08`. The next combined build reached 1,552 actions and
  exposed two incomplete-type errors in package-owned test targets. Their
  include-only M152 corrections (`PrefService` and `ToolbarView`) are committed
  in `416d8be`. The next incremental build compiled those targets and exposed
  the removed `Browser::window()` API in the uBO browser test; the public M152
  `Browser::GetWindow()` correction is committed in `005e76c` and compiled
  successfully. The next failure was confined to the split-layout browser test,
  which called protected `View` drag methods. Its public `DragController`
  correction is committed in `223a4f7` and independently checked against the
  M152 API. One Ninja `-k0` compile then compiled and linked seven of eight
  package-owned Ahoi test programs and found no further source errors. The sole
  remaining Arc link failed because its executable used the generic Base test
  runner while pulling Chrome-near Session code. The replacement with
  `//chrome/test:test_support_unit`, independently checked against the working
  Session, Sync and Extension targets, is committed in `2e531aa`. The next
  action is one combined corrective generation/build from this exact candidate;
  do not replace it with another feature pass or speculative source changes.
  That build was observed running through the Chromium browser-test graph on
  2026-09-01, but its PTY/process is no longer present on 2026-09-03 and it left
  no terminal exit. The Ninja log proves incremental output through
  2026-09-01T16:11:34+0200, while `ahoi_arc_import_unittests`, `browser_tests`
  and `interactive_ui_tests` are still absent. Continue the same target list
  incrementally from these outputs after the CPU gate; do not clean, replan, or
  start a different build. The 2026-09-03 continuation preserved 1,546 prior
  actions and then failed reproducibly in Chromium's patched login browser test
  because M152 removed `Browser::profile()`. The exact public
  `Browser::GetProfile()` correction is committed in `bd10a40`; a complete
  Desktop-overlay scan found no further obsolete `Browser` accessors. The
  resumed combined graph completed all 1,363 remaining/incremental actions with
  exit 0, including links for `ahoi_arc_import_unittests`, `browser_tests` and
  `interactive_ui_tests`. Staging verified 533 component dylibs and 238
  framework resources. The exact installed candidate now awaits visible Arc,
  AnyChat and uBO journeys before focused programmatic execution.
- AnyChat ID: `khpefodpgnkegiohbolbaaeabnfdegln`; not installed.
- Pinned uBO Classic ID: `fkgkibajhfbepljeaefdnfnegdcjomkh`;
  not installed.
- uBO Lite ID: `ddkjiahejlhfcafbddmgiahcphecmpfh`; installed, enabled
  and runtime-ready. It must remain present until the separate post-restart
  migration action succeeds.
- The previous installed-candidate process is closed. The uBO first-click and AnyChat
  Web Store Add action both await explicit just-in-time confirmation after the
  corrected candidate is installed. Each later Chromium permission dialog
  requires its own visible review and confirmation.

## Current continuation (2026-09-04)

- Fresh visible-test root: `/private/tmp/ahoi-arc-green.tphjWS`, with profile
  `/private/tmp/ahoi-arc-green.tphjWS/profile`. The exact `f1475d6` candidate is
  installed and still running from that isolated profile. Preserve this failed
  import state until the corrected candidate is ready for the recovery/retry
  journey.
- The standard menu path was visibly confirmed from a zero-tab window as
  `Lesezeichen und Listen` -> `Lesezeichen und Einstellungen importieren...`.
  A zero-tab invocation correctly did not start an import because it had no
  active WebContents.
- Sky Computer Use still fails before app selection with
  `Sky Computer Use native pipe startup failed`. Codex diagnostics identify the
  lower-level failure as `browser-use native pipe peer authorization failed` /
  `failed to read peer code signing identity`. Both `SkyComputerUseClient` and
  `Codex Computer Use.app` independently pass code-signature verification with
  the same Team ID, so the current gate is the running Computer Use transport,
  not the Ahoi candidate. Do not claim Arc, AnyChat, uBO or held-Command-Q
  visibly green until a real window can be driven and inspected.
- A visible fallback journey using macOS Accessibility/System Events plus CDP
  input reached the real installed Settings surface. It is not a formal
  Computer Use pass, but it did exercise the real app and real read-only Arc
  source. Preview was exact: one workspace, 36 folders, 133 pages, three
  horizontal native splits with 2/2/3 members, four top apps, one unsupported
  item, and no unsafe, unreachable, degraded or deduplicated entries.
- After both confirmations the commit wrote a verified 210 MB backup and the
  intended durable tree, then failed closed with `manual_recovery_required`.
  The v5 journal remained at `prepared`, all seven native member IDs were
  present in the target database, and all seven transient runtime tabs were
  rolled back. Arc remained unmodified and Ahoi did not crash. This localizes
  the installed failure to native split reconstruction before its durable
  receipt; the preserved backup/journal paths remain below the isolated profile.
- The exact 36-folder/133-page/2+2+3 regression initially failed because
  `ReconstructArcSplits()` activated the backgrounded import window. Split
  creation itself succeeded. Runtime success and verification are now defined
  by the active imported tab inside the target Ahoi window, independent of OS
  app/window activation; the import neither steals focus nor fails merely
  because the user changes windows during the long commit. The focused browser
  regression is green 1/1 and the pure focus-contract unit test is green 1/1,
  both with retries disabled. The real installed recovery/retry remains the
  next acceptance gate for the corrected candidate.
- One real browser regression is green:
  `ArcSplitRuntimeBrowserTest.ReconstructsTwoTwoThreeAcrossInactiveWorkspace`
  (1/1). Evidence lives below
  `artifacts/e2e/0.0.1-dev/IMPORT-ARC-12/diagnostics/programmatic/`.
- The first full Arc unit run found five real failures among 100 tests: three
  positive durable split-receipt cases returned conflict, the name-based merge
  path failed to report one deduplicated workspace, and a backup test consulted
  live Arc/open-file state and returned `kSourceInUse`. The run is red and was
  not accepted; its log and JSON summary use the
  `arc-import-unit-20260904T094000Z` stem in the same evidence directory.
- The split-receipt failure classifier exposed the exact defect: an
  unspecified function-argument evaluation order could move decoded metadata
  before its UUID was copied into the member map. The corrected implementation
  preserves the UUID first. It also records the missing name-merge
  deduplication statistic and gives backup tests a hermetic injected source-use
  check while production retains both fail-closed live checks.
- Focused receipt tests are green 3/3 with retries disabled. The complete Arc
  unit target is green 100/100, also with retries disabled. The focused Arc
  browser regression remains green 1/1. Machine-readable results and logs are
  retained below
  `artifacts/e2e/0.0.1-dev/IMPORT-ARC-12/diagnostics/programmatic/`.
- The current Settings WebUI sources are green for the Arc surface 4/4 and the
  Ahoi page 7/7. Their tests now await asynchronous checkbox propagation and
  model the actual fail-closed remote-control prerequisite instead of assuming
  that a load-time seed overrides the authoritative backend state.
- The Ahoi Settings repository contract is green 11/11 after replacing stale
  refactor symbols with the current runtime and durable split verifiers and the
  current `ArcImportService::Commit` boundary. `git diff --check` is clean.
- The deterministic overlay parity refresh completed after these corrections.
  Clean Desktop source `f1475d6` was then built, signed, atomically installed
  and hash-verified. The installation receipt records same-volume staging,
  `renameatx_np(RENAME_SWAP)`, quiescent processes, rollback protection and
  successful post-install verification. Sky Computer Use again failed at
  native-pipe startup before app selection, so this candidate still has no
  acceptable visible Arc, AnyChat, uBO or held-Command-Q result.
- The exact installed `e01e77b` candidate now has a green uBO technical
  preflight at
  `artifacts/e2e/ubo-1.74.0-release-attestation-e01e77b.json`.
  It verifies Official GitHub release 1.74.0, upstream commit `6dd2d95`,
  package SHA-256 `b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e`,
  Manifest V2 and matching declared/derived ID
  `fkgkibajhfbepljeaefdnfnegdcjomkh`. This is only the required technical
  preflight; it does not claim a browser installation or permission grant.
- The first focused uBO browser run exposed a real native-sheet lifecycle bug:
  `UboInstallDialog` relied on delegate destruction to hand off to Chromium's
  permission prompt, while macOS tears the sheet down asynchronously. The
  contents hierarchy now owns the delegate and an idempotent window-closing
  callback performs the handoff only after the native sheet closes. The three
  formerly red journeys are green 3/3, the complete uBO browser target is green
  5/5, and the extension-policy/attestation unit target is green 43/43, all
  with retries disabled. Evidence is retained below
  `artifacts/e2e/0.0.1-dev/UBO-13/diagnostics/programmatic/`.
- The uBO lifecycle correction and its regression coverage are committed in
  `f1475d6`, and the exact combined candidate is installed. The next product
  action is the locked Arc, AnyChat and uBO visible journeys as soon as Computer
  Use recovers. Because that external UI gate is presently real, exact-candidate
  programmatic reruns may proceed but remain diagnostic and cannot substitute
  for visible acceptance.
- The exact-candidate diagnostic matrix is now green 173/173. The initial
  split-layout run contributed 8/10 because two tests modeled Chromium M152
  incorrectly: `AddToNewSplit()` already includes the active tab, and the real
  `Widget::RunDragDropLoop()` invokes `OnWillStartDragForView()` before asking
  the drag controller to publish its payload. The test-only correction mirrors
  those two public lifecycle contracts. Both formerly red cases then passed
  2/2 and the complete focused split-layout target passed 10/10, all with
  retries disabled. Evidence is retained as
  `split-layout-two-regressions-workingtree-20260904T110135Z.json` and
  `split-layout-full-workingtree-20260904T110156Z.json` below
  `artifacts/e2e/0.0.1-dev/DESKTOP-SHELL/diagnostics/programmatic/`. No browser
  product source changed, so this test correction does not require another app
  build or installation and does not alter the installed `f1475d6` candidate.
- Chromium's macOS default is still active for `browser.confirm_to_quit`: a
  quick Command-Q does not exit; the keys must be held for about 1.5 seconds.
  The visible restart journey must exercise the held shortcut rather than a
  synthetic tap.

## Integrated owned source changes

- `overlay/chromium/src/chrome/browser/resources/settings/people_page/ahoi_arc_import_section.css`
- `overlay/chromium/src/chrome/browser/resources/settings/people_page/ahoi_arc_import_section.html.ts`
- `overlay/chromium/src/chrome/test/data/webui/settings/ahoi_arc_import_section_test.ts`
- `overlay/chromium/src/ahoi/browser/extensions/ubo_service_unittest.cc`
- `overlay/chromium/src/ahoi/browser/extensions/ubo_service_browsertest.cc`
- `overlay/chromium/src/ahoi/browser/ui/extensions/ubo_install_dialog.cc`
- `overlay/chromium/src/ahoi/browser/ui/extensions/ubo_install_dialog.h`
- `overlay/chromium/src/ahoi/browser/importer/arc/BUILD.gn`
- `overlay/chromium/src/ahoi/browser/importer/arc/arc_split_runtime.cc`
- `overlay/chromium/src/ahoi/browser/importer/arc/arc_split_runtime.h`
- `overlay/chromium/src/ahoi/browser/importer/arc/arc_split_runtime_unittest.cc`
- `overlay/chromium/src/ahoi/browser/importer/arc/arc_split_runtime_browsertest.cc`
- `overlay/chromium/src/ahoi/browser/ui/shell/floating_browser_view_browsertest.cc`
- `overlay/chromium/src/ahoi/browser/ui/split_drop/split_layout_menu_browsertest.cc`
- `tests/repository/test_ahoi_settings_page_contract.py`
- `patches/chromium/0004-ahoi-lean-profile-compose-guards.patch`
- `patches/chromium/0011-ahoi-command-scroll-and-auth-policy-hardening.patch`
- `patches/chromium/README.md`

The previously integrated implementation changes are clean through Desktop
commit `f1475d6`. The focus-independent Arc runtime correction, its exact-shape
regressions, the split-layout test correction and this checkpoint are the
current owned Desktop delta. Build one combined candidate from their owned
commit before retrying the visible Arc, AnyChat and uBO journeys; the package is
not yet a pass.

## Exit criteria

This package is not closed until all of the following are true for one exact
installed candidate:

- Arc preview, confirmed import/result, second no-op run and accessibility tree
  read complete without a renderer or browser crash.
- AnyChat uses only the normal Web Store path and survives the full lifecycle
  above without a crash or residual failed-install state.
- uBO Classic has the pinned identity and source, works after restart, and Lite
  is removed only by the separate deliberate action.
- The affected visible journeys are repeated before focused programmatic tests.
- Build, signature, installed executable, profile inventory, crash inventory,
  test results and redacted screenshots are bound to the same source SHA.
- Owned Desktop changes are reviewed, committed and pushed without Mobile
  files.

Only after this exit may the next master-prompt package start.

## Frozen follow-up UI contract

The next larger UI/performance package must include the now-explicit `WS-09`
workspace transition contract: directional horizontal slide only inside the
sidebar; never slide the website. When the workspace switch changes the active
tab/WebContents, only the WebContents area gets a short cross-fade. An unchanged
WebContents gets no effect. Interruption, forward/reverse direction, split focus
and Reduced Motion are visible acceptance cases. This follow-up remains recorded
but must not interrupt the locked Arc/AnyChat/uBO package above.
