# Active Desktop checkpoint

Updated: 2026-09-06. Owner: thread `01a04f97-e3ba-70f2-a031-220b214d352d`.

## Contract and ownership

- Active registered goal: implement `outputs/AhoiBrowser-Master-Zielprompt.md`
  through its full Definition of Done. Package 1 is not the whole product.
- Review and rationale: `docs/reviews/2026-09-05-product-and-execution-review.md`.
- Canonical branch: `codex/desktop-core-feature-wave-20260830`. Other owners
  commit here too; inspect current HEAD and stage only owned changes.
- Bookmark owner `01a06d69-1034-7372-b784-0b05a53c87e0` explicitly released the
  shared Chromium checkout/build/install path to this Desktop thread. Read
  `docs/ACTIVE_BOOKMARKS_CHECKPOINT.md`; do not infer a new handoff from idle PIDs.
- Unified Sync implementation is now owned by
  `01a06d69-1034-7372-b784-0b05a53c87e0`: existing common C++ scope plus the
  explicitly transferred Swift/Mobile scope. The former Mobile owner
  `01a044d6-1545-7532-8394-6b7df1144bb1` is the coordinator/read-only reviewer.
  Read `docs/ACTIVE_SYNC_COORDINATION.md` and its goal prompt (48b1ef4).
  Desktop explicitly accepted its retained Native Tree/Session/UI/adapter and
  sole build/install/UI role in `01a07281-9afb-7dc2-8f92-51fbcd2ec0bf`.
  Preserve the unified owner's work; no competing Swift/Common implementation.
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

## Binding Sync decision — one format, fresh isolated acceptance

The explicit 2026-09-05 user decision supersedes earlier complex Sync migration,
old-client support and permanent v2/v3 coexistence requirements: the app is not
live/actively used. Target one active format for ALL relevant permitted entity
types on macOS/iOS, provisionally format 3 including Bookmark and Capability.
The matching-client acceptance uses fresh isolated stores, not an old-client
migration matrix. Existing profiles, the real Arc failed-import journal/backup,
CloudKit data and keys MUST NOT be silently deleted, reset or rewritten.
Consent/category defaults, account/key isolation and crash-safe local commits
remain required; this does not expand the allowed Sync data categories.

Master and native seams are bound to this decision in
`outputs/AhoiBrowser-Master-Zielprompt.md` and
`docs/SHARED_TAB_NATIVE_SEAMS.md`. Common C++/policy/canonical contract remains
with the Sync owner; matching Swift work follows its coordinated ownership.
The simplified canonical format is now published in
`docs/decisions/0009-unified-prelaunch-sync-format.md` and `config/sync-format.json`;
Desktop has read both completely. Format 3 covers all 13 entity classes; this
contract publication is not implementation or acceptance. Common/native code
and default changes still require their concrete coordinated source integration.
No further format-freeze question is needed for the independent 22e2f2b baseline.
Desktop does not introduce Common schema or writer changes.

Desktop explicitly accepts the newly requested Native package A-D in full:
common target-type alias; SessionBridge/native Tree+Store stable temporary-tab
identity and Save/Unsave; full `tab_tree_sync_adapter` projection; and Sidebar
generation-bound complete/deferred capture plus preserving window detach.
Exact owned file groups and native Store seams are in
`docs/SHARED_TAB_NATIVE_SEAMS.md` under "Accepted Desktop-native implementation
package A-D". No disjoint write scope is handed back; corresponding native tests
and existing normal tree UI remain here. Shared Common C++ AND Swift belong to
the unified Sync owner, coordinator read-only. Common leaf/capture types are now
committed and explicitly handed over as5885d01. Native A is now implemented in
source906dac8: two aliases replace the duplicate Session enum/struct, and its
GN target publicly depends only on `//ahoi/browser/sync:shared_tab_types`.
Native policy behavior is unchanged. Shared headers/leaf were read back against
5885d01; no Common WIP file was edited. Scoped diff, GN format and pinned
clang-format with the actual Chromium style passed; no build/test or runtime
claim. This source change is NOT in the frozen/installed4cb622a candidate and
does not trigger another isolated Chromium build or checkout refresh.
Actual Service/backend methods for B-D still require their concrete completed
handoff, not another conceptual format-freeze or leaf-ownership request. Ownership accepted
does not mean implemented or tested. This upcoming package does not alter the
separate baseline candidate/build, UI lease, or portal/key permissions.

225df88 and the test-only 22e2f2b follow-up remain frozen UI/compile baselines,
NOT final acceptance of the new unified format. Do not restart/widen that
baseline for this documentation decision. Continue the current Desktop package;
one native build owner, separate exact-candidate Sync acceptance later.

## Current candidate: 4cb622a installed; startup/fullscreen journey passed

**Newest live action:** same installed4cb, no new build. Short visible Sidebar
flow passed: docked -> floating -> hide -> Cmd+Shift+S restores floating ->
original docked mode. Same URL/active tab, original width264/toggle0 restored;
no imported-tree/folder/workspace edit. THEN exactly five existing, unchanged
Sidebar State/Layout regressions passed, list39741/run95926 both EXIT0,
jobs1/retries0, about1s. No new tests or test binaries. Existing c986 runner and
4cb shared-runtime/source-equivalence limits are explicit in
`artifacts/tests/sidebar-presentation-20260906/README.md`; no whole4cb or Sync
test pass is implied. Nothing is building/running as a test here.

AnyChat Store flow (ID khpefodpgnkegiohbolbaaeabnfdegln, Store version1.0.8)
reaches Chromium's real native permission sheet and cancels without a crash.
The second sheet has NOW ALSO BEEN CANCELLED to permit the independent UI
journey. Store shows Hinzufügen; no modal is left open. The requested explicit
approval for 14 KI-site read/change rights, New Tab replacement and site icons
is still outstanding. Do not infer it from a priority/source-handoff message.
After approval, reopen the normal Store flow and verify its current scope.
Browser remains running on the Store tab; no competing UI slot is granted.
No AnyChat install/functionality pass yet. Exact journey/permissions/next action:
`docs/audit-evidence/2026-09-05-anychat-store-baseline.md`.

Shared fixture c3c3d20 independently checked: live bytes equal frozen SHA256
f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00; 26 unique
examples/all13 explicit IDs, frozen-manifest field maps and four target pairs.
Resource-only preflight, no codec/runtime pass or new build/test expansion.
Native A remains source906dac8; B-D Service/backend APIs remain unapproved WIP.

**Current action:** product build73875 and atomic installer77504 are TERMINAL
EXIT0. Installed4cb622a is source/executable/tree matched to the signed build.
Receipt `artifacts/install/ahoi-dev-4cb622a-20260905T204104Z.json`, SHA256
`fcbc1b682c5d0ef3811aabab567c91680d70e3686f7dd0b6a489dd1c5ce0d6ae`.
Real native CUA: launch/crash-session restore, new window, native fullscreen
entry/exit, Command Bar navigation to example.com, toolbar reveal, regular close
of the own test window, Cmd+Q and clean relaunch/continue all passed. The normal
startup choice, not a new crash-restore warning, appeared after Cmd+Q. Do not
rebuild or continue an old startup/CPU/source blocker. Relevant programmatic
changed navigation/fullscreen regressions have not run/rebuilt for4cb; existing
test executables were not part of that product-only build. The five unchanged
Sidebar regressions above are separate, narrowly source-bound reuse.

The standard Settings import link opened the actual importer. It correctly
displayed "Arc vor dem Import schließen" and no Zen; the three available Firefox
category checkboxes were aligned. Cancelled; NO Firefox/Arc import or failed-
journal recovery ran. This is not a pass for all five Arc checkbox rows or the
real Arc transaction. Native File/Bookmarks menus did not expose an import item.

**Latest continuation, 21:23 UTC:** normal reopen/Continue on SAME4cb exposed the
full Ahoi Settings page; the earlier menu-only capture is not a retained pipe or
product-crash blocker. `getApp` launches in the background. Explicitly Raise the
observed native window BEFORE keyboard navigation: Raise + Cmd+L worked where
background Cmd+L had no effect. Do not repeat the old reset/build loop. A
settings-menu-only AX tree can recur after dialog cancellation/backgrounding;
the successful normal restart/window activation is the known recovery path.

The actual Arc recovery section is under Arc in the STANDARD import dialog
`chrome://settings/importData`, not in `chrome://settings/ahoi`. Its native
source picker visibly contains a DISABLED "Arc — Arc vor dem Import schließen"
row. Therefore the current user-visible recovery entry also waits for Arc to
close; do not invoke hidden WebUI messages or bypass the disabled row. User was
asked asynchronously to quit Arc normally after saving their work. At21:23:43UTC
Arc42725 was still live; Ahoi67242 had been closed with normal Cmd+Q. No import
or recovery transaction was started, no Arc process was stopped, no new build.

Read-only Default journal: version5, status prepared, manual_recovery_required,
170 affected IDs, 7 planned native members, no completed native receipt. The
referenced manifest is a regular owner-owned0600 file and its raw SHA matches
the journal. This is metadata/manifest evidence, not a complete restore-safety
pass; native recovery must still validate the backup/tree/session boundaries.
Next: after fresh proof that Arc is closed, reopen the SAME installed4cb,
Raise its native window, open the standard import dialog and select Arc, then
perform its explicit verified recovery before a new preview/import. Keep the
real journal and backup intact. Independent work need not await new type/format
handoffs for Native A, which is already source-complete in906dac8.

The earlier FillIt Unity18495 samples are historical; the fresh20:41UTC overall
gate cleared before installation. No foreign process was touched. Desktop still
owns UI/checkout/out; no new slot granted.

Historical c986090 build59738 and atomic install3416 are TERMINAL
EXIT0. Build/install receipts match source, executable and full bundle tree.
Visible CUA launch, session continuation and a second browser window succeeded.
The subsequent green window-button/fullscreen action crashed with SIGABRT:
CustomCornersBackground::Paint checks that its layer is not marked as entirely
opaque. NavigationSurfaceController's fullscreen material wrongly set that
coverage flag. This is a new concrete product failure, not the old startup null
dereference or a tool-only failure. No import/recovery or programmatic test ran.
The owning-controller correction and repeated visible journey are now complete
as described above; this historical red is not the current candidate status.
Detailed safe evidence and crash hashes:
`artifacts/build/desktop-startup-guard-20260905/README.md`.

Own canonical correction88ebbe9 guards GetWidget()/browser_widget() before
IsFullscreen; layout replays the saved appearance after native initialization.
New real-browser regression covers creation before preference/mode repair,
native painter/color/alpha/radius, fullscreen return and normal close. Source
review + patch syntax/pinned-format checks only, no claimed test pass.

The previously built detached snapshot was clean at
`c986090d99c22318aef4d45378208cece6878d27` =92694fe plus ONLY those two owned files.
The same fix is already committed/pushed on main as88ebbe9. This isolates the
UI baseline from newer committed Common interface/golden work and all WIP,
without creating another worktree/development branch. Exact derived source is
retained in canonical Git ref `refs/ahoi/build-candidates/desktop-startup-c986090`
and verified incremental `artifacts/build/desktop-startup-guard-20260905/source.bundle`
(requires published92694fe). Do not delete the provenance bundle/ref during cleanup.
Guarded overlay5741, build59738 and installer3416 all completed EXIT0.
Receipts `artifacts/build/ahoi-dev-build-c986090d99c2.json` and
`artifacts/install/ahoi-dev-c986090-20260905T201925Z.json` are retained unchanged.
Those receipts confirm the earlier c986090 candidate. The older bundles remain
at their installer-recorded rollback paths; no rollback or profile reset ran.

The six-line fullscreen correction is canonical7de7fac (one product source,
one assertion in each of two existing regressions). The SAME existing clean
detached worktree cherry-picks only this correction onto c986090, yielding
`4cb622a0bffc602051bf72e6e95b6100948f861e`. No new branch/worktree or Common WIP.
Fresh full process gate clear20:24UTC, disk79,777,988KiB. Guarded overlay31405
is TERMINAL EXIT0; log `fullscreen-overlay.log`. After another fresh CPU gate,
product-only `build-ahoi.sh dev`73875 completed EXIT0, `fullscreen-build.log`
in the same directory. Canonical receipt
`artifacts/build/ahoi-dev-build-4cb622a0bffc.json`, SHA256
`2d94480da797419e9ab9a755a73fbfffff2de4fb822399c0b352a6a2e3f9de76`,
copied byte-identically from the same clean snapshot. Signed executable SHA256
`55301ccbda32e32d3ee57420bd10adc3581b96a918047dbbb82815a56134770b`.
No compiler or test is running here. Do not rebuild or require unrelated test
links before installing the runnable fix. Retained exact source also exists in
`refs/ahoi/build-candidates/desktop-fullscreen-4cb622a` and verified incremental
`fullscreen-source.bundle`, requiring published92694fe.
No new sync format/default/header WIP is included or native runtime slot granted.

## Accepted next package: workspace-local website sessions

Latest user addition is now normative in the master goal and
`docs/WORKSPACE_SESSIONS.md`: finish the current browser fixes first, then local
workspace cookies/accounts AND native site-storage isolation. History, passwords
and installed extensions remain global; action pins may be workspace-specific.
Only appropriate non-secret metadata may sync, never website sessions/storage,
local paths or permission grants. Native design/implementation remains Desktop;
Common+Swift/Wire owner received request01a07337-b020-7211-b95b-06878f84178f,
coordinator01a07337-b056-7d73-95a1-9e3f0e6fce63. No reply/field freeze assumed.
The active full-master goal includes this package; requirement recording is not
runtime implementation or acceptance, and does not widen the current snapshot.

## Historical baseline evidence — do not use as a resume plan

The chronology below preserves earlier build/failure evidence. Its dated PIDs,
candidate names and formerly pending actions are NOT current instructions.
Only "Current candidate" above and "Next actions" below define the live handoff.

### Completed 92694fe baseline build and installation

**Previous fixture blocker RESOLVED, 2026-09-05 18:41 UTC:** the user explicitly
handed over `92694fe36539d024af6567646103f1cf246d5364`. Main reviewed its exact
one-file 3+/2- diff: `<array>` plus `constexpr std::array<LegacyRow, 2>`, identical
fixture data/order/assertions. The former three-turn blocked audit is historical;
no further fixture permission, SQL22 handoff or format-freeze request is needed.
The user handoff authorizes resuming this independent baseline correction.

**Current phase, 2026-09-05 19:05 UTC:** the existing clean detached snapshot
`/private/tmp/ahoi-desktop-package1.5g65WO/repo` is selected at
`92694fe36539d024af6567646103f1cf246d5364`, clean. Guarded overlay60701 completed
EXIT0 with full delta verified. Full cached Dev build17302 is TERMINAL EXIT0;
all requested targets compiled, including the previously missing Sync binary.
Portable runtime staged and Apple-Development-signed, 533 dylibs/238 resources
verified. Canonical receipt `artifacts/build/ahoi-dev-build-92694fe36539.json`,
SHA256 `dec35aefe6a4095fa784dcd5f2cf186a8005e9ac53c5f81e4b6ed4163f3772a1`,
was copied byte-identically from the clean snapshot. No tests have run.
Guarded atomic installer19738 is TERMINAL EXIT0. Receipt
`artifacts/install/ahoi-dev-92694fe-20260905T190452Z.json` matches build source,
executable and full bundle tree; SHA256
`502d6ac0bb82153c9e227534a20034785621e1e56ec4f842c3eb7678676d00aa`.
Post-install verification and renameatx_np(RENAME_SWAP) confirmed. Logs:
`artifacts/build/desktop-fixture-92694fe-20260905/`. From22 the only
executable source change is the fixture test. Additional committed config diffs
are the non-consumed `sync-format.json` contract and two registry descriptions;
no matching consumer in this frozen source and no unified writer/default change.
All canonical Common/Swift/entitlement WIP stays outside this snapshot.

The new CPU gate cleared at18:52: Unity84761 ended; the intervening
ConversationCopilot test/app/runner was freshly below20% aggregate, with no
active compiler or other busy build found. No foreign process was touched.
The earlier189% Unity samples are historical, not a retained blocker. Recheck
before compiler start. Disk last82,492,392KiB.
Next logs are under `artifacts/build/desktop-fixture-92694fe-20260905/`.

Historical guarded overlay session `65019` is TERMINAL EXIT 0, full delta verified.
Combined guarded incremental Dev build `7945` is TERMINAL EXIT1, same full
target set below and existing cached outputs. Do not poll that terminal handle
or restart unchanged source. All previous five API diagnostics are resolved;
Sidebar tests compiled/linked, but one fixture array has two newly surfaced
unsafe-buffer diagnostics at bookmark_sync_store_unittest.cc:435/436. Exact
report: `artifacts/build/desktop-test-api-22e2f2b-20260905/README.md`.
Its bounded request `01a072c4-5524-7270-9d9f-526ba9906062` is now fulfilled by92694fe.
Do not disable warnings, weaken assertions or integrate unrelated format-3 WIP.
The compiler-start
CPU gate was freshly clear at 17:59:12 UTC; disk 83,947,400 KiB.
Only the four agreed test files differ in the Desktop build surface; newer
format-3 work is excluded. Canonical logs:
`artifacts/build/desktop-test-api-22e2f2b-20260905/{overlay.log,build.log}`
(not a successful build receipt yet).
Historical guarded overlay session `54118` at 225df88 is TERMINAL EXIT 0.
Combined Dev correction build `30212` at 225df88 is TERMINAL EXIT 1, using the existing
shared `.work/chromium/src/out/AhoiDev` outputs. Do not resume it or rerun the
unchanged source. Product code and the two large browser test binaries compiled/
linked; five diagnostics remain in four test files. Canonical report/logs:
`artifacts/build/desktop-correction-225df88-20260905/{overlay.log,build.log}`.
`artifacts/build/desktop-correction-225df88-20260905/README.md` records all causes,
hashes, dependency restoration and unchanged installed source. Desktop's
independent-height-clock test fix is committed in `5794d37`. Bookmark owns the
bounded three SQL test-file correction requested in
`01a07264-6e02-7fc2-85b9-cfb263b9a18e` and terminal feedback
`01a07277-73a1-7ba1-b399-77d852964363`. Its exact three-test-file correction is
now committed as `22e2f2b7a3f5b0832cc7eff3d23819a6041aa737`, 13+/8-, and main
reviewed against the actual M152 SQL/cstring APIs. The sender has now explicitly
handed over that exact commit in the conversation; this handoff is accepted,
with no remaining test-source wait. No need to recreate or apply a proposal. The next
candidate should use that exact source (including 5794d37), not later Sync WIP.
The same complete target set below is requested, including `ahoi_sync_unittests`
and `ahoi_sidebar_tree_unittests`, with `AHOI_NINJA_KEEP_GOING=1`. No tests have
run, and no successful new build/sign/install receipt exists yet. Installed
bundle remains `3d413ef`; common v3 writers remain off.

CPU start gate CLEARED before overlay: old Blender93670/Shopify23566 ended;
Unity25581 fell to 0.5% at 17:51 UTC. A briefly intervening ConversationCopilot
xcodebuild36882/compiler tree was then observed terminal/absent with aggregate
CPU0, followed by a fresh all-project check without a busy build. No foreign
process was stopped, paused or reprioritized; neither the older high Unity
samples nor the returned runtime slots remain blockers. Recheck before compiler
start. Disk remains above the incremental build floor (last 83,166,276 KiB).
Pipeline notifications: `01a072b5-26ab-7f40-9c78-15a74e288773` (Sync),
`01a072b5-26e4-72c1-b01d-e0edf8329a69` (coordinator),
`01a072b5-271d-7a71-917e-f1353d68eab6` (FillIt). No runtime/UI slot was granted.

Fresh `cua.getState()` now succeeds and reports Ahoi closed; this is connection
inventory, not native-window or E2E acceptance. Arc is reported running. The
user was asked asynchronously to quit Arc normally when convenient for the
later real import; no Arc app/data action was taken. Recovery of the preserved
Ahoi transaction still comes first once a corrected candidate is installed.

The built source 225df88 includes Desktop corrections `ef0f965`, `96b5a2f`, `6bd3b70`,
the native target-policy test preparation `dc01cb5` and its explicit string-copy
fix `1ea90da`, plus the explicitly handed-over Bookmark package `c28ec4a`.
The common compiler-only fix `3035529` and the separate 25-file effective-consent
freeze `225df88` are now integrated. No product-source-freeze wait remains, no WIP was
integrated, and no intermediate 303-only build was started. The old three-file
proposal in `artifacts/build/desktop-combined-dc01cb5-20260905/` is historical
evidence of the already-landed 303 correction; NEVER apply it again.

Consent correction manifest and limits:
`docs/audit-evidence/2026-09-05-bookmark-consent-generation-fix.md`; explicit
handoff `01a07231-8c67-79e2-b983-2e38c5a2e2f8`. Original authority is rechecked
at journal commit, native projection/ACK and after the provider-to-pump task
hop; reapproval does not revive older replies. The nine added regression cases
are source-only, NOT passes. The previously observed race is source-fixed but
runtime/cross-account acceptance remains open. No network-leak claim or
wire/schema/policy-default change is implied.

Historical combined build `90068` / source `dc01cb5` is TERMINAL EXIT 1,
confirmed 13:57:30 UTC; do not resume or reinstall from its partial outputs.
Its four primary compiler causes are all addressed by 1ea90da + 3035529 in the
current snapshot. Failure report, full log and hashes are preserved in
`artifacts/build/desktop-combined-dc01cb5-20260905/README.md`. Both temporary
dependency workarounds were independently verified restored after that failure.

Fresh CPU gate immediately before the correction build found no competing
build/compiler process tree over 80%. The prior FillIt bakes and Shopify Next
build are terminal; high OS PerfPowerServicesSignpostReader CPU is not a foreign
build. Pre-build free space was 89,299,708 KiB (85.2 GiB), above the incremental
64-GiB recommendation and below the 120-GiB roll floor. The installed Ahoi app
is closed; no foreign process was stopped or reprioritized. Pin stays .65.
Bookmark/Mobile build notifications:
`01a0725e-88d3-7e72-97a3-460cbfe3acc4` /
`01a0725e-890b-7500-b3a9-cb76a9eb7f07`. No new runtime slot was granted.

Continuous workspace gesture preview/cancel is still open: a bounded helper
failed at model capacity and changed none of its seven reserved navigation
files. No gesture correction is part of 225df88; do not count source exploration
as implementation or acceptance.

A fresh visible baseline attempt exposed sustained native CPU and UI timeouts.
The bounded follow-up now adds patch `0033`: unchanged computed sidebar margins
no longer invalidate layout from inside every layout pass. Material reapply
also guards unchanged radius, fast-corner mode and opacity setters that otherwise
request more compositor work. Three native invalidation/geometry tests and one
real-compositor no-op regression are written, not run. Source proves those
redundant invalidations, not the complete runtime hang cause. Include this
follow-up in the same package; do not start a separate one-guard build.

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

The current snapshot includes the user's normal workspace-folder corrections in
`sidebar_tree_view.{h,cc}`, `_projection.cc`, `_navigation.cc` and the focused
`_interaction_unittest.cc`. They are tracked overlay changes, now integrated in
the shared checkout but not the installed `3d413ef`. The temporary Mobile
runtime window has ended; recheck CPU before each new intensive phase.

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
from the runtime failure described below. All related targets plus
`ahoi_sync_unittests` are in the current combined build. Only the explicitly
committed freezes are integrated; any subsequent owner WIP stays outside it.

The subsequently renewed, one-attempt Mobile My-Mac slot was also explicitly
returned. Session `84564` / PID `88055` ended Exit 65 before a test body: missing
destination variant selected native macOS for an iOS host. This is not a native
Desktop, CloudKit or provisioning failure. Evidence is Mobile commit `e8f9975`
and `docs/audit-evidence/2026-09-05-mobile-mymac-cloudkit/README.md`. No current
Mobile runtime reservation remains; no automatic retry is authorized.

### Historical 92694fe/3d413ef candidate details

- At that earlier stage, `/Applications/AhoiBrowser.app` had source
  `92694fe36539d024af6567646103f1cf246d5364`, Chromium `152.0.7977.65`,
  build/install verified but startup RED; do not describe it as accepted/daily-ready.
- Historical receipts: `artifacts/build/ahoi-dev-build-92694fe36539.json` and
  `artifacts/install/ahoi-dev-92694fe-20260905T190452Z.json`.
- Historical executable SHA256:
  `717827e792a4882665334dc834ec54680696ae8b2cc93a88399b176741b945ca`;
  historical bundle tree:
  `083286c952f1b6c542c8177ca9c5cf1c8c9f7edc092b11ac5b8525c753620b6f`.
- Rollback3d remains at the install receipt's exact backup path. No rollback
  or profile/journal reset was performed. The following3d receipts are HISTORY:
- Successful guarded build/sign and atomic install; both commands exited 0.
  Receipts: `artifacts/build/ahoi-dev-build-3d413efb5b6f.json` and
  `artifacts/install/ahoi-dev-3d413ef-20260905T074543Z.json`.
- Executable SHA-256:
  `ab4d0a7664fb8ec871391be1130ee002e79ef8bc4084ff49877d4042e387aa99`.
  Bundle tree SHA-256:
  `a4b830a1fcf57ef76069843cae6b4e2358c1af9ad57afbee847e35ac6a8b9583`.
- The same detached build snapshot was then selected at the isolated correction:
  `/private/tmp/ahoi-desktop-package1.5g65WO/repo`, at that time
  `c986090`; overlay60701/build17302/install19738 all completed for92694fe.
  The startup-fix integration and later4cb correction have since completed.
  This is not a pending CPU/build action. The snapshot shares `.work` through
  `AHOI_WORK_ROOT`; do not create another snapshot.
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

### Historical 3d413ef visible failure and recovery evidence

**Subsequent shutdown readback:** a fresh CUA selection returned the old app's
open command bar with Settings suggestions. One Cmd+Q returned `App quit`;
`ps` independently confirmed PID `37773` absent. No force-kill or journal edit.
The Default journal remains v5 `prepared/manual_recovery_required`. This
supersedes the running-app state below and proves one regular quit on `3d413ef`,
not acceptance of the unbuilt fixes. Do not launch it again to retry the failed
import. Arc itself was seen running in the later process inventory; recheck the
source-closed gate before any future import or Arc Service test execution.

**Latest live readback (13:41–13:50 CEST):** Computer Use inventory briefly
recovered and Desktop opened installed `3d413ef` with **Fortsetzen**. AX and a
real screenshot showed the NTP/sidebar, docked at width `302`; the toolbar was
hidden on NTP, so the seam was not yet reproduced. Cmd+L/internal Settings
navigation+readback timed out at 120 seconds, then a read-only AX request timed
out at 10 seconds and reset only this thread's JS kernel. Completion of the
navigation is unknown. Native app PID `37773` is still running, sampled near
95–105% CPU; do not retain the earlier "app quit" state or force-kill it.
One read-only sample ended Exit 0 and showed only five main-thread observations
in compositor/property-tree work, not a proven source cause. Journal remains
`prepared/manual_recovery_required`; no new import or recovery was attempted.
Details/hash: `artifacts/diagnostics/desktop-3d413ef-ui-hang-20260905/README.md`.

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

1. Keep the installed, visibly exercised `4cb622a` and the same clean detached
   snapshot/output. Builds17302/59738/73875 and installers19738/3416/77504 are
   terminal. Do not resume/restart them or reinstall older926/c986 for delayed
   messages. Startup, navigation, fullscreen, quit/restart and the bounded
   Sidebar-mode journey already have their evidence above.
2. Arc: after the user closes Arc normally, verify the source-closed condition,
   Raise Ahoi's native window and open `chrome://settings/importData`. Select
   Arc, explicitly recover the preserved failed transaction through its guarded
   backup UI, THEN create a new preview/import. Preserve journal/backup; no
   hidden WebUI call, direct DB reset or forced Arc shutdown. Continue the real
   split/folder/restart/no-op journey on this same candidate.
3. AnyChat: await the already requested website/New Tab/favicon permission
   decision. The native sheet is currently cancelled. After approval reopen
   the ordinary Store flow, verify its current scope, install and visibly test
   action/Side Panel, shortcut and restart. No custom installer or inferred
   approval from general priority/source messages.
4. Classic: existing pinned Official GitHub one-click flow, Chromium consent,
   actual filtering/dashboard and restart. Lite stays until Classic is accepted
   and its separate deliberate migration action is authorized/performed.
5. Finish remaining package1 UI obligations: normal-folder motion, workspace
   slide/fade, reported seam, zero-tab/split stability and Bookmark core flow.
   Do not mutate the failed imported tree to set up tests before its recovery.
   Reuse the completed startup/Sidebar evidence; do not replay whole matrices.
6. Native A is source-complete in906dac8 outside the frozen candidate. B-D use
   only the genuinely completed Common Service/backend handoff, not current
   WIP or a legacy vector fallback. Common C++/Swift and their unified-format
   work remain with the Sync owner; see `docs/SHARED_TAB_NATIVE_SEAMS.md`.
7. After the current browser package, implement workspace-local website sessions
   under `docs/WORKSPACE_SESSIONS.md`, preserving global History/passwords/
   extensions and the no-cookie-sync boundary. Continue every remaining master
   package, including Sync, lean/privacy/performance and release obligations.
8. Only a new executable correction/coherent integration warrants another build.
   Recheck actual CPU/process/disk/ownership gates, use the existing guarded
   product-first `./scripts/build-ahoi.sh dev`, install its exact signed result,
   and exercise the changed visible flow BEFORE minimal necessary regressions.
   Preserve existing tests/assertions; no blanket test-target prerequisite or
   testing expansion while a controllable app failure needs fixing.

## Evidence and independent boundaries

- ADR 0008 / `09cae9f` was mapped to Native seams; its mixed-version/bootstrap
  requirements are superseded by the new single-format/fresh-store decision
  above. The isolated
  `session/shared_tab_target_policy` plus six tests uses the canonical target
  fixture, but is linked only into Session tests: no runtime caller, Native-DB
  migration, capability announcement or writer activation. The common Service/
  capture/status invariants from `08af63c` remain recorded in the updated
  `docs/SHARED_TAB_NATIVE_SEAMS.md`. Exact simplified format/default/header work
  belongs to the Sync owner; do not re-open already settled identity/capture
  questions. That handoff is not a prerequisite to the accepted test-only
  correction of the Bookmark-v2 baseline. No Common WIP was edited.
  An unavailable format/authority must preserve/defer capture, never manufacture
  Presence tombstones from a filtered snapshot. Detailed next-package boundary:
  `docs/reviews/2026-09-05-native-shared-tabs-seams.md`.
- Mobile source `313e351` / DebugLocal 15 is independently accepted locally,
  not as native cross-client sync. Main read its clean matching-source receipt
  and terminal logs: visible Bookmark journey 1/1, Core 70 passed + 2 entitlement
  skips, shared Swift 36 passed and repository 2 passed. Its default writers
  remain v2. Report: `docs/audit-evidence/2026-09-05-mobile-bookmark-wire2/README.md`.
  No Mobile runtime reservation remains; further Swift WIP stays within the
  coordinated Sync implementation scope. This old wire-v2 result is historical
  local evidence, not acceptance of the new all-entity format. The actual
  C++/Swift roundtrip, native transport/key bootstrap and
  shared-normal-tab runtime are separate open gates.
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
