# Active Desktop checkpoint

Updated: 2026-09-05. Owner: thread `01a04f97-e3ba-70f2-a031-220b214d352d`.

## Contract and ownership

- Active registered goal: implement `outputs/AhoiBrowser-Master-Zielprompt.md`
  through its full Definition of Done. Package 1 is not the whole product.
- Review and rationale: `docs/reviews/2026-09-05-product-and-execution-review.md`.
- Canonical branch: `codex/desktop-core-feature-wave-20260830`. Other owners
  commit here too; inspect current HEAD and stage only owned changes.
- Bookmark owner `01a06d69-1034-7372-b784-0b05a53c87e0` explicitly released the
  shared Chromium checkout/build/install path to this Desktop thread. Read
  `docs/ACTIVE_BOOKMARKS_CHECKPOINT.md`; do not infer a new handoff from idle PIDs.
- Mobile is owned by `01a044d6-1545-7532-8394-6b7df1144bb1`. Preserve its dirty
  files. Shared-bookmark schema/adapter work is coordinated by the bookmark
  owner through `docs/decisions/0006-shared-bookmark-collection.md`.
- The user additionally rejected the normal sidebar folder expand/collapse
  motion. Desktop owns its reproduction/correction after the current import
  blocker. Continuous workspace gesture preview/cancel also remains open.
- The user also reported a dark rectangular step at the sidebar/rounded-toolbar
  seam. The preserved image was viewed at
  `artifacts/computer-use/bookmarks-coordination-20260905/user-sidebar-seam-091918.png`.
  Reproduce and correct bounds/clipping with the owned sidebar UI package; the
  image is a reported defect, not a passing runtime check.

## Exact installed candidate

- `/Applications/AhoiBrowser.app`: source
  `3d413efb5b6f196403e92f51631c346c9c55b2e5`, Chromium `152.0.7977.65`.
- Successful guarded build/sign and atomic install; both commands exited 0.
  Receipts: `artifacts/build/ahoi-dev-build-3d413efb5b6f.json` and
  `artifacts/install/ahoi-dev-3d413ef-20260905T074543Z.json`.
- Executable SHA-256:
  `ab4d0a7664fb8ec871391be1130ee002e79ef8bc4084ff49877d4042e387aa99`.
  Bundle tree SHA-256:
  `a4b830a1fcf57ef76069843cae6b4e2358c1af9ad57afbee847e35ac6a8b9583`.
- Existing clean detached build snapshot:
  `/private/tmp/ahoi-desktop-package1.5g65WO/repo`, now at correction `3d413ef`.
  It shares `.work` through `AHOI_WORK_ROOT`; do not create another snapshot.
- Correction overlay session `61889` and combined build session `83719` both
  exited 0. Successful receipt: `artifacts/build/ahoi-dev-build-3d413efb5b6f.json`;
  log `/private/tmp/ahoi-package1-3d413ef-build.log`. All requested test targets
  compiled/linked. Independent tree-store tests passed 20/20; SessionBridge
  passed 14/15 including the new production-flush regression. One older privacy
  test has a diagnosed fuzzy-query oracle failure; the test-only correction is
  prepared for the next package, not yet rebuilt/rerun. Corrected visible E2E
  remains pending. Details and exact test-binary hashes are in the evidence file.
- Ready bundle tree SHA-256:
  `a4b830a1fcf57ef76069843cae6b4e2358c1af9ad57afbee847e35ac6a8b9583`.
  The foreign Unity CPU gate cleared at 07:45 UTC (0%, no active compilers).
  Guarded atomic installation session `89774` exited 0, log
  `/private/tmp/ahoi-package1-3d413ef-install.log`. Published receipt confirms
  `renameatx_np(RENAME_SWAP)` and post-install verification; installed plist and
  executable hash were read back independently. No foreign process was stopped,
  paused or reprioritized.
- Delayed bookmark handoff messages referring to `8bf309d` and a 1% Unity sample
  are historical. They do not request another build. A fresh 07:42–07:43 UTC
  check still found Unity above 80%. Bookmark owner received the terminal build,
  exact receipt and retained Desktop UI ownership in queue message
  `01a07085-b24d-7ed3-80bc-04fc29f2e53c`.

## Visible acceptance and current defect

Current technical boundary: Computer Use fails before app access with
`Sky Computer Use native pipe startup failed` (app selection, explicit runtime
reset/reselection, then inventory all failed). No successful launch or visible
`3d413ef` journey is claimed. The documented technical-E2E exception permits
independent persistence regressions, without substituting them for Arc acceptance.
UI slot is explicitly handed to the bookmark owner for isolated-profile Bookmark
E2E via queue message `01a0708d-ebf7-7da1-bc02-70b4026b9da6`. Main must not act in
the UI before explicit handback. No default-profile Arc mutation, rebuild,
installation or pin change was delegated; shared build/checkout stays Desktop-owned.
For that Bookmark E2E, create native Chromium bookmarks through the mouse menu,
native Bookmark Manager or a context action. Cmd+D intentionally saves to the
Ahoi tree and does not prove the native bookmark collection.

The delayed `a453dee` API-fix request is already fulfilled by `87a5999`: two
product and three test calls now use `View::GetVisibleBounds()`. The fix is
included in installed `3d413ef`; no repeat build is needed. This confirms API
integration/compilation only, not Bookmark behavioral acceptance. The owner was
notified through queue message `01a070a0-a473-76f3-9325-609cfc8dd745`.
The subsequent semantic refinement is now in test source: all three viewport/
offset assertions call `scroll_view_for_testing()->GetVisibleRect()` directly.
Product anchor checks retain `View::GetVisibleBounds()`. M152's declarations,
`CurrentOffset()` implementation and `GetPreferredSize` default argument were
checked in the actual checkout. This test-only refinement is not rebuilt or
rerun yet and belongs with the next coherent package; do not reinstall `3d413ef`.

The following scoped visible results and original import failure are from
the preceding installed `0a13e22` candidate, not fresh `3d413ef` acceptance:

- PASS (scoped): native App-menu Import opens the real dialog from a zero-tab
  normal window. The existing restored user window was preserved.
- PASS (scoped): a normal Cmd+Q quit exited the installed process before the
  correction install. Computer Use timed out while querying the now-closed
  app; the recorded main PID was independently confirmed absent.
- PASS (scoped): checkbox boxes align with their first text line. All three
  genuine choices work by mouse and Space. Missing profile/sidebar selection
  disables Import; optional split deselection does not. The two duplicate
  consent boxes are replaced by a translated mandatory-backup notice and the
  single deliberate Import action.
- Arc preview: 1 workspace, 36 folders, 133 pages, 3 splits, 0 degraded,
  1 excluded, 0 deduplicated. The first commit correctly refused running Arc.
  Arc was then closed; the exact main process and all helpers were gone.
- FAIL: the fresh real import then returned `backupError`, confirmed by a
  read-only expression in the current dialog's DevTools. No retry since.
  No `Default/Ahoi/ArcImportJournal.json` or `Arc Import Backups` directory was
  created. The generic error text is not a completed import or rollback proof.
- Root cause: `TabTreeStore::ReplaceWithSnapshot` bulk-deletes a self-referencing
  tree whose `parent_id` uses immediate `ON DELETE RESTRICT`. The real
  authoritative `Default/Ahoi Tab Tree` contains 5 nodes, one parent-before-child
  link, and zero FK violations. This is not the legacy `Ahoi/TabTree.sqlite`.
  An in-memory synthetic SQL reproduction fails with FK error 19. The production
  failed persistence flush maps to `backupError` before backup creation.
- Correction now in owned source: detach old parent links inside the existing
  atomic replacement transaction, leaving foreign keys enabled and restoring
  all state on rollback. Persistence failures log only their class, not private
  paths or saved-page data. Regressions cover repeat nested writes, changed
  workspaces, tombstones/undo, SQL-error rollback and the actual flush bool plus
  durable second-write readback. These tests are written, not run.

## Next actions — do not restart the review

1. The reviewed four-file fix and regressions are committed in `3d413ef`; its
   clean snapshot is selected. Recheck all-project CPU ownership and free space
   before each intensive phase.
2. Overlay and combined guarded build are ALREADY COMPLETE for `3d413ef`; do
   not rerun them for a delayed coordination message. Built targets include
   `ahoi_tab_tree_unittests`, `ahoi_session_unittests`,
   `ahoi_startup_policy_unittests`, `ahoi_arc_import_unittests`,
   `ahoi_arc_import_browsertests`, `ahoi_sidebar_tree_unittests`,
   `ahoi_extension_policy_unittests`, `ahoi_extension_ui_unittests`,
   `ahoi_ubo_browsertests`, `browser_tests`, `interactive_ui_tests`.
3. `3d413ef` is ALREADY INSTALLED and verified; do not repeat installation.
   After explicit UI handback and a working Computer Use connection, repeat
   the real Arc journey: successful backup/import, 2/2/3-pane splits,
   folder state, restart and identical second no-op import. Preserve existing
   source/profile and old recovery evidence. A red visible flow blocks its
   programmatic acceptance; fix its actual cause, not assertions.
4. Complete normal-folder motion, workspace slide/fade, zero-tab split and
   Cmd+Q acceptance. Coordinate bookmark E2E UI ownership explicitly.
5. AnyChat: ordinary Store install/cancel, action/Side Panel, shortcut,
   disable/enable and restart. Classic: official pinned GitHub one-click,
   Chromium permissions, actual filtering, dashboard and restart. Lite remains
   until Classic passes and the deliberate migration action is performed.
6. Run focused programmatic checks after each corrected visible journey;
   record exact candidate/results, commit/push owned work and advance the master
   packages. Sync/lean/privacy/performance and all other contracts remain in scope.
   Include the stricter credential-index test oracle in the next coherent
   package; no extra build was started for this test-only source change.

## Evidence and independent boundaries

- Mac Sync readiness was read back from installed `3d413ef`: it remains
  provider-free, without CloudKit runtime keys, entitlement or provisioning
  profile. No separate verified CloudKit Mac candidate exists in this wave.
  `ahoi_sync_unittests` is a planned addition to the next combined package,
  not part of the completed target list above. Exact evidence and outstanding
  signing/key/bootstrap gates: `docs/audit-evidence/2026-09-05-mac-sync-readiness.md`.
- Current import diagnosis: `docs/audit-evidence/2026-09-05-desktop-package1.md`.
  Older chronology: `docs/audit-evidence/2026-09-04-desktop-checkpoint-history.md`.
  Historical 173-test runs and old recovery attempts are not current passes.
- A short idle sample showed compositor/property-tree work during the observed
  high browser CPU. It does not yet establish a cause or performance regression;
  do not make speculative compositor changes. Diagnostic sample is ephemeral at
  `/private/tmp/ahoi-import-diagnostic.Arh1kv/ahoi-idle.sample.txt`.
- The bookmark owner's `.83` Stable roll remains separate. The hard 120-GiB
  checkout floor must be checked exactly and never overridden. Do not switch
  the Chromium pin during this package's correction/acceptance.
