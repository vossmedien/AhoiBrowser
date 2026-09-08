# Active Desktop checkpoint

Updated: 2026-09-08. Owner: thread `01a04f97-e3ba-70f2-a031-220b214d352d`.

## Contract and ownership

- Effective capacity policy is the new global AGENTS.md: assess sustained whole-
  machine CPU/core capacity, memory/swap, GPU and responsiveness. No fixed80%
  single-process blocker or Ahoi project priority remains. Old dated samples
  below are historical. Only own costly work may be adjusted for contention.
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

## Runtime ownership — Build18 returned; native715 isolated UI

The bounded11:52UTC Build18 Simulator grant is CLOSED. Explicit handback
`01a080fd-802c-75e1-8670-42aede65b234` and the coordinator's12:25–12:27UTC
readback return Desktop's native UI; the isolated Mobile simulator is Shutdown.
Do not retain any old Mobile slot. Other projects' Simulators are not in scope.

Desktop then activated the verified compatible4cb Arc-recovery app through
guarded install79760 EXIT0. During initial navigation CUA twice refused input
because the app changed externally; workspace/rows and width also changed.
No recovery transaction was started. The user was asked asynchronously to leave
Ahoi briefly unbedient; do not force input against a changing real profile.
The later14:40 screenshot/questions and fresh14:41 process absence do not prove
who changed the earlier window. That interference was later resolved by a fresh
stable readback; real recovery was attempted and failed for the concrete data
mismatch below, not a continuing UI-slot block. Current native session50070
uses ONLY `/private/tmp/ahoi-toolbar-715afc2.enwD8P`; real Default remains closed.

### Earlier slots — returned, not active

Mobile explicitly returned the My-Mac window
`01a070c7-7b59-78d0-b224-bfab8a57b998` unused in
`01a070ef-f293-7c53-971d-411deac9db53` and again in the conversation. No host,
runner, install, zone, account or key was changed. The last Bookmark slot was
also returned after the native-pipe failure. Both handbacks are accepted;
Desktop app/UI, checkout/build/sign/install are available to this Desktop owner
subject to fresh CPU and live-state checks. Do not retain a stale runtime block
or repeat the grants/acknowledgements. New Mobile My-Mac work needs a fresh
explicit slot once its exact host is ready.

**September8 Simulator slot is now returned:** explicit owner handback
`01a0801b-0086-71f0-97f5-46f712d925ad` and shutdown of the isolated device are
confirmed in `docs/ACTIVE_SYNC_COORDINATION.md`. Desktop's retained native UI
ownership is no longer held source-only by that window. This is not an inference
from PID absence and does not grant control of other projects' Simulators or a
new My-Mac host. Do not retain the old active-Simulator gate.

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
The Service/backend handoff has since arrived in `e2f6711` with its subsequent
async/durability corrections; the current native integration is described below.
Source implementation does not mean compiled or tested. The new package does
not imply a new UI lease or portal/key permission, and the installed baseline
remains separately bound to its original receipt.

225df88 and the test-only 22e2f2b follow-up remain frozen UI/compile baselines,
NOT final acceptance of the new unified format. Do not restart/widen that
baseline for this documentation decision. Continue the current Desktop package;
one native build owner, separate exact-candidate Sync acceptance later.

## Current candidate: e241191 installed; corrected combineda9db378 integration

### Active toolbar/UI continuation — 2026-09-08

**LIVE HANDLE:** cached guarded app-only build7222 is RUNNING, jobs4, since
15:13UTC; corrective overlay52816 is TERMINAL EXIT0 on the same clean snapshot
at `a9db37849bf5a9f437283c7bff173dfced0f195e`. Prior app-only92858 is TERMINAL
EXIT1, not a candidate. Four objects reported the SAME new delegate inline-
virtual style error; only fix846e875 was integrated (out-of-line false default
plus its GN source, no behavior/guard change). Native schema objects and new
WebUI CSS/TS/GRIT actions compiled successfully before that failure. Original
failed log stays under6b6c771; do not rerun it or ask for another source fix.
The exact follow-up is already started, not a pending handoff. Fresh17:13CEST
gate78.5%idle/63%memory/no new swapouts/no compilers;38.3GiB hard-floor margin
unchanged. Earlier17:12CEST
gate70.2%idle/63%memory/no new swapouts/no compilers,38.3GiB above32GiB floor.
Logs: `artifacts/build/desktop-sidebar-store-a9db378-20260908/`.

**Combined package already present:**6b6c771 overlay52765 EXIT0,
checkout delta verified. Exact scoped integration: fe647ee Sidebar tint/star,
ad91502 constrained local schema upgrade,9323d71 recovery spacing/copy on e241;
no Common/Swift WIP or patch36. Only a README conflict was resolved by retaining
the applicable37/38 ledger entries; series correctly remains without36.
Fresh17:03CEST gate76.8%CPUidle/67%memory headroom/zero new swapouts/no compilers
permits AHOI_JOBS=4;38.3GiB uses documented build override above32GiB hard floor.
Watch disk/capacity at heavy phase boundaries. No extra test targets. Logs:
`artifacts/build/desktop-sidebar-store-6b6c771-20260908/{overlay,build}.log`.
Do not restart92858/52765 or edit a running snapshot/out.

**CURRENT BUILD STATE:** build56659 and overlay32506 are TERMINAL EXIT0 on the
clean existing
snapshot `e24119158e2d15c8bc9f0b22d0fea5e555327e5a`:715 plus ONLY the reviewed
Arc-preserving fixdff21f9 (seven Arc files, no Common WIP/patch36). Both original
and Development715 bundles/receipts were preserved first. Same guarded
app-only build jobs2, no extra test targets. Fresh16:01CEST start gate73.8%idle,
58%memory, no new swapouts/compilers;46.3GiB above32GiB hard floor. Earlier
15:58CEST gate52.9%idle,
58%memory headroom, no new swapouts,46.2GiB above32GiB build floor. Identified
FillIt Unity batch15877 is a separate2-worker job (~4.6% at sample); it is not
touched or treated as an automatic blocker; absent at the fresh compile gate.
Do not resume/restart them. Original receipt SHA
f1348e3e07654c5cd2842bb5f6c50b45e2f8ed526947cccf3cd3ffecbbfb7a1b,
binaryf52478a7897211fea8b6398e45d33d929417041fd7f4b76cf184753032cd193b,
treeaf0c6450d01f2e8970d7309a1a6068563dd558bf3900e8a5b069f8c69dccc82e.
Logs:
`artifacts/build/desktop-arc-preserve-e241191-20260908/`.

**CURRENT NATIVE STATE:** no own app is running. Test runtime50070 and real
Default runtime94861 both ended EXIT0 by normal Cmd+Q; no kill. The latter
showed a bootstrap Inbox because the real Schema2 tree FAILED TO LOAD, not
because a new Inbox was persisted. Native SQLite rejects `ADD COLUMN ... CHECK`
with `no such table: pragma_quick_check`; actual log at the directory above's
matching E2E/runtime.log. Disk tree remains Schema2,2 workspaces/174 nodes,
SHA791e9ae... and journal1768e20f... unchanged. Do not force recovery against
that RAM bootstrap. Pure preserving-plan fixdff21f9 was not enough to resolve
this separate native database-load failure.

e241 Development copy prepare73966/sign/verify22013 EXIT0 and install74357 EXIT0
were completed. BEFORE starting Default, read-only prefs proved global Sync
already TRUE. To avoid activating transport on old real stores during recovery,
the SAME compiled provider-freee241 was installed through34545 EXIT0; no user
Sync preference/key changed. Active installed receipt:
`artifacts/install/ahoi-dev-e241191-provider-free-arc-recovery-20260908.json`,
SHAdd876c0796d026fb6b450fa8534d4058e66256dbed27e5a94c152fc9a4dfe854.
The prepared/verified Development copy remains preserved separately. Original
Default recovery continues provider-free until an intentional isolated Sync run.

**Next coherent source package, all committed:** fe647ee adds quiet saved-section
tint/validated empty-root highlight plus an8px native-bookmark star in existing
favicon slots (one Host observer, indexed native lookup, no state/Sync field).
ad91502 replaces failed Schema2 ADD-CHECK migration with atomic constrained
rename/recreate/copy; FK/CHECK/indices/undo/meta remain, no PRAGMA-off or Raze.
9323d71 shortens recovery copy/button and adds16px notice/action gap plus padding.
Main reviewed these changes; minimal existing regressions are written, unrun.
They are now integrated in running6b6c771, excluding Common/Swift WIP/patch36.
Then actual loaded Default/Arc recovery plus new Sidebar/notice visible journey;
no warning/constraint bypass and no unrelated test-target prerequisite.

**715 predecessor evidence:** initial runtime85457 ended EXIT0 through Cmd+Q.
Guarded install92150 is TERMINAL EXIT0 for the separately Development-configured
715afc2 copy. Build98973 and overlay10765 are TERMINAL EXIT0.
The same clean detached snapshot is at
`715afc21a9757ea46a309817691c5c4aaa320c1c`. New source contains ONLY the56px activator, LEFT Pin,
exclusive-sidebar BookmarkBar condition and verified certificate CLI fix on
top of3d59. No patch36, extension orchestration, Common/Swift WIP or new tests.
Fresh heavy-phase gate70.7%CPUidle/52%memory headroom, no active compilers or
new swapouts;49.9GiB uses the documented build override above32GiB hard floor.
Same guarded `build-ahoi.sh dev`, AHOI_JOBS=2, canonical AHOI_WORK_ROOT; no UI
or installed-profile mutation. Logs:
`artifacts/build/desktop-toolbar-left-715afc2-20260908/{overlay,build}.log`.
Original final build receipt SHA73cb67db12cfb3d64644fddc63f7be3b40d0b64b6a7d799676255fd6ba563d3a,
binary4d98e259a439be7eb5319317020ffacebc4f5eda07ce4fa0598035e39b82b69e,
tree74d9aa8f67889f072c2b1621e1866a747c83071fd4a63fbd7e92933d97f22936.
The715 Development copy has preparation41500 EXIT0, signing EXIT0 and full
verification28337 EXIT0, with the same real OSX profile. Verification receipt
`cloudkit/verification.json` SHAbc367cef3da0d0b08950ea6cf4ce345685b6289e31950e80f198611bcdd09031;
copy binary667626cd56c98d0768966b6cecf2be7b0fae2e69d6496164c92acdcca267eb45,
tree99bc79f5e44489d7d4e609849c1cc3f4ebf0a28be463fb8e2e8ea3723fbae95d.
Initial install26297 EXIT2 before staging/activation: its exact4cb rollback
pathname was already occupied by the preserved older identical rollback. That
owned, idle bundle was renamed on the SAME Applications filesystem to
`/Applications/.AhoiBrowser.retained-rollback-4cb622a-d795fa7d-20260908.app`;
source/main-binary55301ccb... read back identical. Nothing deleted or overwritten.
The original failed install.log is retained; corrected retry is install-retry.log.
Installed readback matches715afc2 and signed copy binary667626cd... . Install
receipt `artifacts/install/ahoi-dev-715afc2-cloudkit-20260908.json`,
SHA8969a2f1f884f0d08c806943691b170b62e33905c0bdc99feb26c2882062b767.
No build/install/signing process remains active. Do not restart any of them.
Approved Icondf03d46's full asset/generator paths were compared byte-identical
to715; the delayed handoff does not require another source integration/build.

**Bounded visible715 journey passed:** real Example page -> LEFT Pin on -> Home
opens native NTP; normal bookmark menu creates Example Domain in bookmark-bar
collection, visible ONLY in sidebar on normal page and NTP. Cmd+Q exits0, explicit
same-profile CLI restart preserves Pin on, bookmark and24/24 settings category
selection while global Sync remains OFF. Category then visibly returns0/24.
Bookmark click opens real Example page. Pin off + page click hides navigation;
the broader activator reopens it. Pin is left ON for visible user inspection.
The new sail logo is rendered in About/Help. CUA captures/AX are in this thread;
scope/limits in `docs/audit-evidence/2026-09-08-toolbar-and-arc-recovery.md`.
No full hover matrix, Dock-cache/entire-branding, multi-device or CloudKit pass.
The settings page still labels a non-instantiated provider as build-unavailable
while global Sync is off: this is not evidence of missing Mac configuration or
a failed key lookup. No global enable/bootstrap/key/zone write was attempted.

**Real Arc recovery attempted, now concrete bounded product correction:** normal
Settings -> importData -> Arc -> Search -> Restore was finally operable after a
fresh stable window. Recovery refused; journal/backup unchanged. Read-only
semantic hashing reproduces backup170685df... exactly, currentd8409d52... differs
from expectede1b3928c... . The ONLY changed baseline row is unrelated Page
9b4a0498-3b35-4b51-a48d-8239b51d001a: title/url/modified_at. Substituting its backup
fields solely in a comparison model yields EXACT expected fingerprint; imported
data itself is unchanged. The existing helper returned the seven-file Arc-only
preserving recovery implementation; main reviewed it against the exact hashes.
It retains independent baseline values/new temporary pages in surviving baseline
targets and keeps exact imported-hash, undo, native/current-session and journal
checks. Unknown saved rows/workspaces and references to removed targets fail
closed. Source only; no Common/Session/UI or profile writes and no test run.
The real dialog was aborted and4cb quit via native app menu, not killed. No
recovery/import succeeded. Raw DB after normal persistence:
791e9ae10ed951cb8503ee0a29d863d768bb35969e16553549d4175f64795ff0;
journal1768e20f... unchanged. The completed715 UI uses ONLY an explicitly isolated test
user-data directory: do not call CUA getApp before the explicit CLI launch, or
after Quit, because that can launch real Default automatically. Real Default
stays protected while the preserving Arc correction is implemented.

**Preserved predecessor:** overlay17811 and app-only build62333 are TERMINAL EXIT0.
The clean candidate source is `3d59cf9de8846e47c91db540b90b3067b18c0841`;
do not restart any build. Original receipt is preserved at
`artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/build-receipt.json`,
SHA256 `f5b04cebd2290ad3d7c29baa3ae2a1955d7942d16bd427779965d1e73025f6f8`.
Original provider-free bundle was APFS-cloned to `AhoiBrowser.app` beside that
receipt before715 integration; main-binary hash rechecked identical. Original
binary1f2d82a0a0b122a6dba895c9628c30156b5ff89f92243ffab5fde3ac8d5effdb,
treea113f6991742b68d820b9c0d79de71dcf6a80259fc8be443b6f9f5eb7024c124.
Logs and the separately prepared Development copy are in that same directory.
Build used jobs2 and the documented low-disk override above32GiB hard floor.
Prior79280 is TERMINAL EXIT1, its sole GURL cause fixed byc1fd86f. No extra
test targets, patch36 or foreign Common WIP entered this candidate. Original
failed log remains under `desktop-toolbar-settings-f5a324b-20260908/`.

Development copy preparation91993, signing25981 and corrected verification6196
are TERMINAL EXIT0. Initial verifier54382 EXIT2 was a CLI argument-parsing bug
in certificate extraction, not a signing/profile failure. Details and exact
copy hashes: `docs/audit-evidence/2026-09-08-native-cloudkit-development.md`.
The3d59 copy was not installed and is not a transport/bootstrap/roundtrip pass.
Its bundle/receipt remain unchanged;715afc2 is now installed,4cb preserved.

New user UI feedback14:40 was viewed at
`/Users/vossmedien/Desktop/Bildschirmfoto 2026-09-08 um 14.40.48.png`:
widen the address-bar activator and investigate the marked blank round/edge
region near extensions. Activator width is now56 instead of36 in canonical
source only; unchanged height and one existing shared layout token, not a new
hit-testing path. It is NOT in the frozen3d59 candidate and does not trigger a
standalone build before the current visible acceptance. Pin/Home are already
in3d59, not the installed4cb;
do not call that old screenshot a failed3d59 acceptance. The extra bookmark
sync button controls one category within Ahoi Sync, not a second transport;
the user reasonably finds that separation confusing. No automatic consent or
new independent sync system is authorized by the question.

**Confirmed follow-up14:50–14:53:** the user screenshot
`/Users/vossmedien/Desktop/Bildschirmfoto 2026-09-08 um 14.50.57.png` and fresh
native AX expose TWO bookmark toolbars (Sidebar and horizontal top-container).
Native `MaybeShowBookmarkBar` still attaches the second surface behind floating
navigation. Patch0037 now excludes only an actual Ahoi sidebar surface from
that attachment; no data/pref/BookmarkModel change, native non-Ahoi behavior
preserved. User also explicitly requests Pin on the LEFT: updated0034 inserts
it before Back/Forward in visual/accessibility order. These two changes plus
activator56px form one next Toolbar correction, no per-file build. Exact
affected-blob patch checks and pinned formatting passed; NOT compiled/E2E.
The frozen3d59 package has the earlier right-side Pin and duplicate-bar behavior.
Its complete build/signing receipts remain truthful, not current UI acceptance.

The attempted recovery UI resumed after fresh no-running-app plus unchanged
Default tree/journal hashes, but CUA again refused the first mutation because
the user changed the app while taking the new screenshot. No restore or import
ran. Do not retry native input while the user is actively testing; independent
source work continues, no stale Mobile ownership or general approval block.
Integrated source: native6ae/tests, Toolbarc86314b, Common41de599+29c42db+
Home-selector1587497, approved Icondf03d46, explicit native Settingsfc37928 and
Development signing-toolingf4aee9d. No WIP. Overlay91184 TERMINAL EXIT0 and delta
verified; earlierb63/12891 is superseded, not a resume target. Jobs2, no extra
test binaries. Fresh repeat capacity recovered to54% idle/51% memory headroom;
63.3GiB disk uses documentedAHOI_ALLOW_LOW_DISK=1, unchanged32GiB hard floor.
Do not update snapshot/out while a guarded phase runs. After success: signed atomic install,
visible Hover/Pin/Home +explicit Settings category and icon, then focused checks.

**Historical compiler finding,11:55UTC:**79280 was RUNNING, observed335/1441
(generated resource frontier initially3394 then restatted). The only deduplicated
error so far is `ahoi_settings_handler.cc:115`: pinned GURL has `host()`, not
`host_piece()`. Exact native fixc1fd86f is committed/pushed in canonical source,
NOT inserted into the running snapshot. The keep-going build must finish its
remaining independent compilation; then collect any additional diagnostics and
integrate the bounded correction(s) in ONE cached owner-controlled follow-up.
Do not claim79280 green, install its partial output, suppress warnings or start
a parallel compile. Pin and Common setting/search objects compiled successfully;
that is not full build/UI acceptance. Fresh capacity49–62%idle/46%memory,
no new swapouts in the sample; jobs2 remains unchanged.

**Additional source-only work during that build:** native storage observer
handoff851e3bb is committed/pushed as Patch0036 (only upstream
storage_frontend.h/.cc plus ledger/series). Common received exact API in
01a080e9-6683-7291-9296-d26e5b75237b: ObserveSyncSettingsChanges and
GetRemoteSyncApplyObserver carry kNative/kRemoteApply with the individual UI
reply; no global async suppression. Nonempty sync-area changes notify before
the no-JS-listener return, with scoped subscriptions and callback-list lifetime
protection. Positive extension/key/value and original authorization remain
Common's responsibility. Exact patch application/format checked, NOT compiled
or E2E-tested. This is OUTSIDE frozenf5/79280 and MUST NOT be pulled into its
small GURL corrective follow-up; integrate with the matching extension consumer
in a later coherent package, not by checking out canonicalHEAD blindly.

**Arc preparation, after real source close:** Arc is now absent in the live
process readback and Default DB/journal hashes still match recorded originals.
To recover that pre-mirroring journal safely while the shared build snapshot
stays immutable, a sparse temporary4cb worktree with ONLY scripts/tools/config
was prepared at `/private/tmp/ahoi-arc-recovery-tools.Q65woe/repo`. It is clean
and must NEVER build/use/refresh shared Chromium/out. The original4cb rollback
was APFS-cloned to `/private/tmp/ahoi-arc-recovery-tools.Q65woe/AhoiBrowser.app`;
full existing verify-built-app check47969 EXIT0 (533libraries/238resources,
source4cb, executable55301ccb...). Guarded install79760 subsequently EXIT0;
receipt `artifacts/install/ahoi-dev-4cb622a-arc-recovery-20260908.json`,
SHA189c8823b711655a194c11a6a0bf1e820bcbf2144f42a0852340f9e6662d78da.
Installed plist was read back as4cb. Keep6ae and4cb rollback/evidence; complete
the real recovery before new normal-tab mirroring touches Default. Recovery
itself has NOT run; initial UI interference is recorded above. Preflight log:
`artifacts/build/arc-recovery-4cb622a-20260908/preflight.log`.
Coordinator informed in01a080ed-2950-7202-807b-e2f21f6d5435; no new ACK required.

New Apple authorization has produced a concrete native Mac Development profile,
not merely a plan:8f149b92-89cc-4d34-a0db-1b305d4e545c in the standard Xcode
profile cache, OSX/1Mac/Team248AJ5BN47/exactAhoiContainer/developmentAPNs. Tiny
provision-only Xcode build10142 EXIT0, never launched/installed. No old
profiles/keys deleted. Real profile validation found and fixed an allowlist-vs-
signed-claim bug in the existing verifier (f4aee9d); exact app claims and the
separate Production verifier remain strict. Details/next candidate-copy gate:
`docs/audit-evidence/2026-09-08-native-cloudkit-development.md`. No CloudKit/key
bootstrap/roundtrip pass yet; Common owner has exact profile/handoff. No new
type/role/API wait, and old Mobile07:08 slots remain returned.

#### Toolbar baseline and source inventory (before the current build)

The delayed e2f6711 handoff is already integrated in6ae. No API/role wait or old
build restart follows from that message. Global/project AGENTS reread live.
Own runtime74127 is now TERMINAL EXIT0 after normal Quit from the installed6ae
executable with the same isolated `/private/tmp/ahoi-native-tabs-c20a759.Z10Mne`
profile. No Default
profile/Arc recovery action or foreign process mutation. CUA uses the full app
path; never query a bound app after Quit because that can relaunch Default.

A controlled Create/Save/Loslösen repeat retained the org focus-check URL and
width293 throughout. The native menu remained present after5s idle; after
Loslösen another5s produced no focus/width change. The original intermittent
observation is retained but is not currently reproducible; no speculative
focus/layout code change or repeated review loop is justified by it. The
ordinary "new group with this tab" dialog then saved that same Page ID
da4571dc-300f-46e7-816d-b0b64d4d99fd into test folderUI Probe; no duplicate or
focus change. Folder close/open used distinct native icons without a caret;
two early snapshots are not full animation-quality acceptance. CUA reported a
user change before Quit; a fresh unchanged AX readback preceded the successful
normal Quit. Original Default DB/journal hashes still match; earlier anomaly's
cause is not inferred retroactively from that user-change signal.

User toolbar request is explicit in
`outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md` (direct handoff
01a0806a-dc24-7d93-90ee-113e19d3bc92) and now included in the master contract.
The user screenshot was viewed; Reload's rectangular hover was also reproduced
on this exact6ae via the real toolbar. Pin and compact native Home are required.

Source package in this owner only (not compiled/installed):

- `ui/shell/navigation_pin_button.{h,cc}` + its GN leaf: a native ToolbarButton
  using the existing `ahoi.navigation.floating_auto_hide_enabled` preference,
  live checked state and managed-pref enforcement. No second state/store.
- Patch0034 (new, appended to series) wires it only into Ahoi's native toolbar,
  exposes Chromium's existing Home control by changing only its registration
  default (explicit user/policy values remain authoritative), and adds de/en-GB
  labels. Matching Sync catalogue already contains both settings; owner informed.
- Reload's outer clipping host gains50% radius matching its inner circular
  control. Read-only helper/source/generated-CSS checks did NOT establish a
  unique rendering cause; this is explicitly a bounded fix candidate pending
  visible verification, not a claimed fix or a reason to disable WebUI flags.

Canonical overlay/patch contains the work. Scratch copies at
`/private/tmp/ahoi-toolbar-patch.i9L6at/{before,after}` are only patch-authoring
material; the current integration/build above supersedes this pre-build state.
Scoped XML/GRIT-ID, GN-format and exact seven-file patch application checks
passed. No compiler or test was run for this new toolbar package. Finish the
connected UI work, freeze only owned committed files into the same snapshot,
then one guarded app-only build/install and visible Hover/Pin/Home journey.

**Current continuation, 2026-09-08:** global/project AGENTS reread again after
the user's latest update. Reuse same-scope approvals; finish authorized
preparation before necessary questions; no inferred gates from optional skill
guidance. Installed candidate is `6ae40701d93727f8915668a99a3aaf22aef16de0`;
overlay63476/build91795/install29501 all TERMINAL EXIT0. Native272385f corrected
the originalc20 Save-row failure. Visible repeat: neworg tab -> Cmd+D immediate
saved row -> native Loslösen -> normal Cmd+Q -> restart/Continue with activeorg
and no duplicates -> explicit Cmd+W removes onlyorg. Read-only DB confirms the
same global ID across1->0->1 temporary flags, tomb0 through Quit, tomb1 only on
explicit close; savedcom stays live. This is local lifecycle evidence, NOT a
cross-client/CloudKit/release or uninterrupted no-focus-change pass. One expired
menu handle and unexplained concurrent focus/width/infobar changes are retained
in `docs/audit-evidence/2026-09-08-native-shared-tabs.md`, not hidden by retries.

Build receipt `artifacts/build/desktop-shared-tabs-6ae4070-20260908/build-receipt.json`,
SHA334a51d3a9a79b5b18ff1954e5869d8ed7c4b2c68af6353c179f5d9eaff8cf62.
Install receipt `artifacts/install/ahoi-dev-6ae4070-20260908.json`,
SHA2814734670336c7f2c77cf27031211a2e7c77fe1693820e0ee5ee1f8d82d8195.
Binary9870d0a9b99d849c060dad9e42e612a3a7841f98d5e57b730b1cca03599e6b8e,
tree5bb026f6f612e7b43248b84b46d89322120bb60888659466f5511bac04da00f7.
Receipt-bound4cb/c20 rollbacks remain; source6ae is protected at
`refs/ahoi/build-candidates/desktop-native-tabs-6ae4070`.

Only AFTER that visible journey, existing Save regression was strengthened for
global identity/Unsave notification and updated to reject obsolete URL-only
rebinding: test-onlye215186, committed/pushed, clean detached snapshoted84ec4.
Product bytes/source relative6ae are unchanged. Test-only overlay81032 and
two-target build2870 are TERMINAL EXIT0, with no app restamp/sign/install.
Explicit nonempty lists preceded jobs1/retries0 runs: Session75097 EXIT0,8/8;
Tree EXIT1,3/4. The failed receipt test's SQLite trigger was disabled on the
production connection, so no error was injected. Its assertions stayed strict.
Test-onlyf97b661 changes that fixture to an enforced CHECK; same clean detached
snapshot nowa9d6ad7. Overlay19904/build85696 both TERMINAL EXIT0. Fresh listed
four receipt tests now4/4 SUCCESS/EXIT0, with actual CHECK error observation and
exact rollback; Session remains8/8 on unchanged source. No app reinstallation
or repeated E2E for these test-only edits. Logs/summaries:
`artifacts/tests/native-tabs-6ae4070-20260908/`. The original failure is retained.
This native local-lifecycle slice is closed; no build/test process remains and
none of these handles should be resumed. Full master/remote UI/Sync remains open.

Workspace-isolation preparation is captured in the existing
`docs/WORKSPACE_SESSIONS.md`: main verified the helper's concrete fixed-partition,
noopener, restore/sessionStorage, permission and extension-cookie routing
findings. This adds no implementation/pass or new Common schema requirement.

Earlier owned runtime97371 and restart52424 are TERMINAL EXIT0. Isolated profile:
`/private/tmp/ahoi-native-tabs-c20a759.Z10Mne`. Those sessions granted no new UI
slot; follow-up74127 above is now also terminal. Do not query a bound CUA app
after Quit: getAXState can relaunch its
normal-profile chooser. Verify process exit read-only and relaunch explicitly
with test-profile arguments before reattaching. Real Default DB/journal hashes
still match originals; no whole-profile byte-identity claim.

### Integrated native source inventory and earlier build history

The following source-only statements describe their dated pre-build stages;
current installed/runtime evidence is above, not reset by this history.

Native B-D source now connects the committed Common package `e2f6711`, async
completion `5e74472`, durable-export/support separation `37bc558` and cancelled
apply handling `40358d1`; there is NO remaining general header/role/freeze wait.
Exact native source package: `74ceb158ba5abac611afef943c24e66340ab13e9`,
committed/pushed with DCO, 41 owned files. No new tests were added in this wave.

- Native SQLite schema3 (not SyncStore6) adds temporary/target fields throughout
  reads, writes, snapshots, Undo and duplication. Existing native schemas1/2
  receive only additive defaults; existing data is not erased.
- Normal tabs reserve global Page IDs in native session metadata before deferred
  SQLite creation; Presence IDs remain separate and survive window movement.
  Explicit tree activation/session metadata, NEVER URL deduplication, binds a
  saved page. Save/Unsave and drag-save retain the existing Page ID; placement
  plus saved-state changes share one Store transaction/Undo.
- Explicit temporary closes tombstone their Page on the safe UI task boundary;
  window detach/whole-window close/Quit preserve shared pages. Retired remote
  bindings remain local until new navigation/save and retain that state through
  a local session-metadata extension; ordinary/old Arc metadata bytes stay v1.
- The same pure target policy is below Session/UI in tab_tree, with the existing
  Session alias entry point retained. Adapter projection carries all target
  fields, preserves local-only native targets and sends no local URL/code/path
  through URL or automatic captions. Nonportable peers remain inert.
- Async native Apply now overrides the released Common method. It checks the
  original authority at disk commit and RAM publication. Export attests ONLY the
  complete current RAM tree matching a successful disk commit, not a stale
  persisted revision. Local writes re-notify Common after actual disk ACK.
- Full snapshot replacement notifies existing loaded-tree observers by stable
  node IDs; no parallel tree/animation architecture. Passive runtime URL/title
  events cannot overwrite a newer remote destination.
- Sidebar hosts register meaningful capture changes and answer the exact issued
  generation with complete/deferred data. No filtered HTTP-only vector or
  missing-window delete fallback. Shared IDs suppress duplicate device rows;
  explicit open/adoption reuses the same Page. Temporary origins use real
  provenance and existing native icon slots; Sync remains reachable with no
  remote rows. Dormant-row placement needs visible review; filtering projected
  shared temporary rows by origin is still an implementation follow-up.
- Arc fingerprints stay byte-identical for old/default rows and include real
  new page state only when present. Existing recovery copies verified DB/WAL to
  a ScopedTempDir before migration; originals/journal remain untouched.

A bounded helper implemented only the handed-over Store SQL/mutation files;
main integrated/read them. All changes are SOURCE-only: no compiler/test/E2E
pass. Existing SQLite/flush regressions remain prepared; no new test matrix.
The new source needs an exact app-first candidate and short visible
create/save/unsave/restart/shared-row journey before focused regressions.

Next build preparation: reuse the clean owned detached snapshot
`/private/tmp/ahoi-desktop-package1.5g65WO/repo`, preserve the existing4cb source
ref/receipts, and advance only to the committed coherent package (no WIP).
Use guarded overlay + `AHOI_JOBS=2 ./scripts/build-ahoi.sh dev` without extra
test targets; `AHOI_NINJA_KEEP_GOING=1` may collect product compiler errors.
Fresh 10:21–10:23 local samples found 12 cores, 56–67% idle, no active compiler,
53% memory-pressure free/reclaimable and 80.9GiB disk available. Historical swap
is about24.5GiB but did not grow in the sample; recheck capacity before starting
and keep concurrency bounded, without an Ahoi-specific priority.

**Actual continuation 08:37 UTC:** existing detached snapshot has advanced cleanly
to74ceb15;4cb source ref/receipts and installed bundle remain intact. Shared
Chromium checkout/out has NOT been refreshed, no overlay/build process started.
The start gate changed materially: repeat samples reached82–92% overall CPU use,
31GiB RAM used with11–14GiB compressed. FillIt guest/third players, multiple
Simulator journeys, CUA and WindowServer are active; no foreign job was touched.
That sample deferred the start; the subsequent gate result below supersedes it.

**Current build result, 08:59:56 UTC:** overlay41438 is TERMINAL EXIT0 and its
checkout delta was verified. Log remains
`artifacts/build/desktop-shared-tabs-74ceb15-20260908/overlay-0841.log`.
The following capacity sample had53–61% idle/49% memory headroom. The one
guarded app-only build `71760` is TERMINAL EXIT1 from the same clean74ceb
snapshot, canonical AHOI_WORK_ROOT, AHOI_JOBS=2, AHOI_NINJA_KEEP_GOING=1, command
`./scripts/build-ahoi.sh dev`. No added test targets. Log:
`artifacts/build/desktop-shared-tabs-74ceb15-20260908/build-0843.log` (actual
start08:44:54; filename is only a label). Host/toolchain/disk/Sparkle/hooks/GN
passed; Ninja's167-step frontier collected compiler errors and stopped. No
stage/sign/install or successful candidate receipt followed. Do NOT resume71760
or rerun unchanged74ceb.

Exact causes: two Native private helpers redundantly acquired an already-required
sequence context; fixed canonically in `5a15614`, preserving public DCHECKs and
the helper's VALID_CONTEXT_REQUIRED contract. Common failures are three nonempty
inline virtual defaults in profile_sync_ui_bridge.h, missing override/final at
profile_sync_backend.h:52, and the same duplicated context guard in
sync_store.cc:292/365. Sync owner received exact compiler handoffs
`01a0803d-a9fd-7472-9afd-4232e697e71a` and
`01a0803e-462d-77e2-8fa9-e1e08ed87712`; no Common file was edited here.
The bounded Common compiler fix has arrived as `dfcc32e` and was read in full;
default-method semantics/authorization remain unchanged. Same owned detached
snapshot now clean at `c20a759dd936cfa93d5fedeb4c9dcd52e876bcd7`, comprising74ceb
plus5a15614,7a47063,dfcc32e only. No catalogue WIP was included. Corrective overlay
`68595` is TERMINAL EXIT0; log
`artifacts/build/desktop-shared-tabs-c20a759-20260908/overlay.log`.
The guarded app-only corrective build82463 is TERMINAL EXIT0, started
09:10:22 UTC, receipt builtAt09:19:48 UTC, jobs2/keepgoing1. Build and immutable
copied receipt are under
`artifacts/build/desktop-shared-tabs-c20a759-20260908/`.
Build receipt SHA2567d67a8bbe97204c08535283792719ca2d7a0560666574f60279beba6e93205ce.
Atomic install99703 is TERMINAL EXIT0, receipt
`artifacts/install/ahoi-dev-c20a759-20260908T092211Z.json` (filename only a label),
SHA2563a1499158e861a556424b3a7df11805f82445ccf9f9b2c47801d7c160174a3cd.
Installed binary7f34223ff06d7b430acab72d6f57d34072efeb3db5b1ff9363d566e1396ef189
and tree d17efb433b0fa822fbde0ad0be476acb49d1bef85f4564870f1ca8be19583010
match the signed build. The previous4cb bundle remains in the exact rollback
path recorded by the installer. Source c20 is protected by
`refs/ahoi/build-candidates/desktop-native-tabs-c20a759`. No build/test/CloudKit
acceptance beyond these explicit boundaries; see the RED visible journey above.

Do not launch the new normal-tab mirroring on the real failed-import Default
profile before its4cb Arc recovery. Initial new-format acceptance uses an
isolated fresh profile. The requested Inbox Bottom-clock correction is already
delivered as `7a47063`, read and included with native `5a15614` in the same detached
correction snapshot (`c20a759`). It is not a remaining source request.
Settings/extension-restoration and later workspace
website-session isolation remain in the master scope, not completed here.

### Earlier4cb runtime evidence — historical, not instructions

The paragraphs below describe earlier4cb sessions. Statements about a running
app, cancelled sheets or unreleased APIs are historical, superseded by the
current continuation above. They are not live resume steps.

**Latest completed native UI journey (4cb history):** short visible Sidebar
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

The coordinator's explicit expanded-scope confirmation and binding ADR0010
(79d2102) are now read and reflected in the master/README/workspace contract.
Native Chromium user settings, actual trusted extension setup restoration and
positively reviewed extension-setting values are in scope; old inventory-only
SYNC-16 wording is superseded. Raw stores/secrets/permissions/paths remain local.
Common+Swift/policy/catalogue stay Sync-owned; new native hooks need exact file
handoffs. Portable workspace IDs/pins are separate from live local website
contexts. No browser rebuild, runtime grant or AnyChat permission approval follows
from this scope confirmation. Native isolation itself is still unimplemented.

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

1. Resume live cached app-only7222 on cleana9db378. Overlay52816 EXIT0 and prior
   92858 EXIT1 are closed;846e875 fixes its one inline-virtual cause. No further
   source/ownership wait or restart. Keep the real Default launch provider-free.
   All e241 handles are terminal, not restart targets. The coherent
   fe647ee/ad91502/9323d71 Sidebar+Store+Recovery-copy package is integrated, no
   Common WIP/patch36 or further source handoff wait. e241's actual load
   failure needs the constrained migration fix, not another unchanged run.
   The bounded visible715
   Pin/Home/restart/settings/bookmark-only-sidebar journey is complete above;
   do not restart builds or replay the whole flow without an affected change.
   Runtime50070 is terminal; preserve that test profile/receipt. After the
   combined correction, repeat the real loaded-Default Arc journey and the
   newly requested saved Drop tint/Bookmark badge/notice spacing,
   then only its necessary regression. No unrelated test-binary prerequisite.
   Keep original and Development-copy receipts distinct. Bootstrap/roundtrip
   remains separate and open. Prior6ae's local lifecycle/focused12-case evidence
   remains valid for its boundary, not proof of multi-device sync.
2. Real Default remains protected and closed, installed e241 is provider-free.
   Do not reinstate old4cb or claim e241 loaded the real tree: it did not. The
   constrained Schema2 upgrade must succeed first; then the already-reviewed
   preserving recovery can retain the unrelated navigation while undoing the
   unchanged import. Global Sync is already true in Default, so never add Cloud
   configuration to this recovery session or toggle its preference as a shortcut.
   After Arc is normally closed,
   use the compatible4cb baseline's guarded importer recovery before allowing
   new normal-tab mirroring to touch that failed-import profile. Open the real
   `chrome://settings/importData` flow, recover the preserved transaction, THEN
   preview/import again. No hidden WebUI call, direct DB reset or forced Arc
   shutdown. Do not confuse the fresh-profile c20 journey with Arc acceptance.
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
6. Native A-D source is integrated in the preserved6ae4070 and new3d59 candidate, including
   the released async completion and durable-current export. After the visible
   local journey, resolve any actual product defect and the remaining shared-
   temporary-row UX, then coordinate the matching-client roundtrip. Do not
   restart the API review or treat local E2E as multi-device Sync acceptance.
   No general header/role/freeze wait, WIP integration or legacy vector fallback.
   Common C++/Swift remain with the Sync owner; see
   `docs/SHARED_TAB_NATIVE_SEAMS.md`.
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
