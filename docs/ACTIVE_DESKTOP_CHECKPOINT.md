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
- Installed candidate source: `69b17203e172a65fc1be9d12c58977cc7c40a43f`
- Chromium: `152.0.7977.65` at
  `fc4d67f1788019a27e32511137ceccbd2fafdaaa`
- Installed executable SHA-256:
  `d91dcd086bba982586b957f48813e9979c0495fc57d79f5161d1e38501f13ec4`
- Installed bundle-tree SHA-256:
  `9684e1b3771711925f50e8645f105e20dcddc354a886679209ddc785fdbe7c88`
- Installed candidate evidence:
  `artifacts/build/installed-ahoi-dev-69b17203e172-20260903T094641Z.json`.
  Its receipt SHA-256 is
  `0aaf599257e3309f406c96010e26ba50226bb7f8ac80c5e50f4b4dadc83551f1`.
  Build provenance is schema 2 and binds both the legacy bundle hash and the
  exact app-tree hash to clean Desktop source `69b1720`.
- The installed app contains the Arc accessibility correction and the later
  inactive-workspace split reconstruction fix from Desktop commit `285990c`.
  Exact-tree uBO attestation hardening is integrated in Desktop commit
  `69b1720`. The incremental compile/link completed, the app was staged, signed
  and verified, then provenance was restamped from a clean detached authority
  worktree without recompiling byte-identical outputs.
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
  `/private/tmp/ahoi-arc-green.tphjWS/profile`. The installed candidate remains
  running and stable; its window is currently closed, so a new real tab/window
  is required before invoking the standard Chromium import command.
- The standard menu path was visibly confirmed from a zero-tab window as
  `Lesezeichen und Listen` -> `Lesezeichen und Einstellungen importieren...`.
  A zero-tab invocation correctly did not start an import because it had no
  active WebContents.
- Sky Computer Use currently fails before app selection with
  `Sky Computer Use native pipe startup failed`. A read-only macOS Accessibility
  probe succeeds but reports no current Ahoi window. Do not claim the installed
  Arc journey green until a visible window can be driven and inspected.
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
  The next executable boundary is an owned Desktop checkpoint commit, then one
  incremental candidate build. The installed-app
  Arc, AnyChat and uBO journeys must be repeated on that exact new candidate
  before the focused suites are rerun for acceptance.
- The exact installed `69b1720` candidate now has a green uBO technical
  preflight at
  `artifacts/e2e/ubo-1.74.0-release-attestation-69b1720.json` (receipt SHA-256
  `603c1ffcaa885067d738b17117702cdd1c184b550fd2be69ab1c43127295eb93`).
  It verifies Official GitHub release 1.74.0, upstream commit `6dd2d95`,
  package SHA-256 `b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e`,
  Manifest V2 and matching declared/derived ID
  `fkgkibajhfbepljeaefdnfnegdcjomkh`. This is only the required technical
  preflight; it does not claim a browser installation or permission grant.
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
- `overlay/chromium/src/ahoi/browser/importer/arc/BUILD.gn`
- `overlay/chromium/src/ahoi/browser/ui/shell/floating_browser_view_browsertest.cc`
- `overlay/chromium/src/ahoi/browser/ui/split_drop/split_layout_menu_browsertest.cc`
- `tests/repository/test_ahoi_settings_page_contract.py`
- `patches/chromium/0004-ahoi-lean-profile-compose-guards.patch`
- `patches/chromium/0011-ahoi-command-scroll-and-auth-policy-hardening.patch`
- `patches/chromium/README.md`

`git diff --check` was clean for these edits and commit `218b4d0` contains only
the twelve listed Desktop paths. The corrected app has been produced, signed,
atomically installed and bundle-verified; visible product journeys and focused
programmatic execution remain pending, so the package is not yet a pass.

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
