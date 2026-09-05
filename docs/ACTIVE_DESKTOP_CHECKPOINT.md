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

## Runtime ownership — both temporary slots returned

Mobile explicitly returned the My-Mac window
`01a070c7-7b59-78d0-b224-bfab8a57b998` unused in
`01a070ef-f293-7c53-971d-411deac9db53` and again in the conversation. No host,
runner, install, zone, account or key was changed. The last Bookmark slot was
also returned after the native-pipe failure. Both handbacks are accepted;
Desktop app/UI, checkout/build/sign/install are available to this Desktop owner
subject to fresh CPU and live-state checks. Do not retain a stale runtime block
or repeat the grants/acknowledgements. New Mobile My-Mac work needs a fresh
explicit slot once its exact host is ready.

## Owned combined correction package — not yet built

**Current phase:** `ef0f965` contains the reviewed motion/clipping/canonical
merge package; `96b5a2f` freezes the additional Arc Service/recovery source.
Desktop now awaits the Bookmark owner's explicit committed source-freeze and
final file list. No shared checkout refresh, build, test or install has
started for this wave. `ahoi_sync_unittests` is reserved in the one combined
build. Do not integrate the common Bookmark Sync WIP before its handoff.

The additional Arc code waits for real non-initial native navigation commits
before taking a SessionService receipt; it does not wait for full page loading.
A scoped SessionBridge guard defers only automatic title/URL mirroring for
imported split members; explicit user edits still invalidate the transaction.
The full Service browser fixture now has ten cases, including held-open HTTP
responses, concurrent user edits, and explicit recovery through genuine backup,
journal, SQLite persistence and native SessionService readback. No security or
receipt mocks. Source review found and corrected the temporary-tab workspace
reassignment risk: recovery also refuses tabs in newly added workspaces that
would disappear, including unbound temporary tabs. Tests are NOT RUN.

The Settings recovery action (patch `0032` strings plus overlay handler/UI)
requires an explicit click. It verifies an unchanged failed tree, original
backup/journal, and absence of affected live/durable tabs before restoring.
It keeps the backup and performs no automatic discovery/import retry. The real
Default-profile failed journal/backup have not been changed. The installed app
was quit normally with Cmd+Q after the terminal failure; no force-kill.

Desktop is preparing the user's normal workspace-folder motion corrections in
`sidebar_tree_view.{h,cc}`, `_projection.cc`, `_navigation.cc` and the focused
`_interaction_unittest.cc`. These are canonical overlay edits only; the shared
checkout and installed `3d413ef` are unchanged. The temporary Mobile runtime
window has ended; recheck CPU before the next intensive phase.

Prepared source captures the current interpolated height before resetting a
reversed animation, computes split clips from the current materialized group
bounds via the existing BoundsAnimator observer, stops both motion paths for
Reduced Motion/native drag, and reveals a selected row at its current on-screen
position instead of its future endpoint. No additional animator/timer was added.
One final reveal after both animations finish keeps a selected row visible;
real ancestor ScrollView callbacks cancel it on intervening scrolling, including
away-and-back scrolling. Changed selection/reset also supersedes it; focus is
never re-requested per frame and queued work is weakly bound.
Five focused intermediate-frame/reversal/clip/reduced-motion/scroll tests are
written and the helper returned source ownership. Main reviewed their fixtures
and pinned animation APIs; independent production-diff review found no concrete
remaining source blocker. The final review caught the layer-scroll notification
gap and the actual scroll-callback seam now covers it. This is not compilation,
execution or visible acceptance.
Master and registry now agree on the expanded `TREE-13` requirement; all 412
unique IDs are preserved and its status remains `NOT_RUN`.

Symmetric child-row folding is now in source: nonempty folder splices notify
presentation intent, entering rows unfold from zero height under the folder,
and closing retains only materialized rows until the existing BoundsAnimator
finishes. Exit rows reject events/focus/AX/drag and reuse their UUIDs on reversal;
cleanup is weakly deferred and native drag sources remain parented through drag
completion. Moving visible rows are retained even if their target leaves the
viewport. New regressions cover intermediate bounds, reverse-before-cleanup,
noninteractive exits and Reduced Motion. Native acceptance is still OPEN.

The same package includes patch `0031` and the appearance/navigation changes:
truthful rounded-layer opacity/output clips, explicit caller-owned sidebar
corners, and the native CustomCornersBackground retained by a typed callback.
No cover pixels or extra animation architecture. Unit/native regression source
is prepared; the screenshot's exact cause remains unproven until visible E2E.

It also includes the canonical Arc merge and native receipt focus corrections
from the fresh runtime failure described below. Include all related targets in
one build plus `ahoi_sync_unittests`; do not integrate the bookmark owner's
uncommitted common Sync WIP. Use the existing clean snapshot after owned commit.

The subsequently renewed, one-attempt Mobile My-Mac slot was also explicitly
returned. Session `84564` / PID `88055` ended Exit 65 before a test body: missing
destination variant selected native macOS for an iOS host. This is not a native
Desktop, CloudKit or provisioning failure. Evidence is Mobile commit `e8f9975`
and `docs/audit-evidence/2026-09-05-mobile-mymac-cloudkit/README.md`. No current
Mobile runtime reservation remains; no automatic retry is authorized.

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

Computer Use briefly RECOVERED: installed `3d413ef` was opened, session continued,
and real Arc preview/import executed. The transaction ended in manual recovery,
not success. Backup/manifest now exist, tree is 2 workspaces/174 nodes/155 nested,
0 FK violations. Journal v5 is `prepared/manual_recovery_required`, 170 affected
IDs and 7 planned native members, no completed native receipt. Those planned
fields do not prove runtime reconstruction started. No second import or
recovery mutation occurred. Preserve the real Default-profile journal and backup.
Diagnosis and next safe recovery boundary:
`docs/audit-evidence/2026-09-05-arc-canonical-recovery.md`.

The concrete cause is append-ordered merged snapshot versus canonically exported
store vectors, causing false post-write and rollback mismatches. Canonical export
via the existing validator fixes the cause without weakening equality. The
independently incorrect global active-window receipt check is also corrected,
retaining target-pane and ownership checks. Both have new regression source.
After dismissing the terminal error dialog, CUA again returns only window titles
without controls/screenshots, even after reset; no further visible pass is claimed.
The bookmark owner returned the isolated-profile UI slot explicitly in
`01a0709c-556c-70c1-b59b-17fb3d9cdc30` after the same native-pipe error in its own
fresh/reset session. It launched no app and changed no profile. Desktop accepted
the handback in `01a070b8-90f0-7cd1-9a05-108a24700fb1`, which also cancels the
redundant re-offer prompted by a delayed coordination message. That bookmark
slot is closed, and the separate Mobile runtime window was returned unused.
Build/checkout/UI ownership remains Desktop; do not repeat old tests.
A subsequent main-thread `cua.getState()` still failed before app access.
The owner's existing `3d413ef` Shelf suite passed 11/11 under the technical-E2E
exception. Main checked log, JSON status counts, summary hash and executable
hash; this is not a visible Bookmark pass. Evidence is in
`artifacts/tests/bookmarks-3d413ef-20260905/` and the package evidence report.
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
   UI handback is accepted. Once Computer Use is working again, repeat
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
  signing/key/bootstrap gates, seven requested focused suites and missing shared
  Workspace/SavedPage transcripts:
  `docs/audit-evidence/2026-09-05-mac-sync-readiness.md`. A deps file is not a test
  executable; do not claim C++ or cross-language execution before that handoff.
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
