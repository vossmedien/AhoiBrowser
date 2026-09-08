# Active sync coordination

Updated: 2026-09-08. Coordinator: `01a044d6-1545-7532-8394-6b7df1144bb1`.
Registered goal: joint sync-model integration and candidate-bound acceptance.
Contract: `outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md` plus ADR 0009,
binding browser-setup extension ADR 0010 (`79d2102`), and the domain/privacy
contracts of ADR 0006/7/8 where not superseded.

ADR 0010 is included in the coordinator's scope and acceptance: native user
preferences with an explicit supported/excluded catalogue, real trusted
extension install/enable restoration and positively reviewed extension settings.
Five Ahoi preferences/inventory and the original 13-class/26-example fixture
are not completion or a scope ceiling. One format 3, existing Common+Swift
ownership, exact native-hook handoffs, native consent and secret exclusion
remain binding. No product, policy, golden, build or runtime change by this update.

## Immediate user priority: working app before test expansion

The user explicitly renewed this order: implement/fix -> runnable candidate ->
short real visible E2E -> only necessary focused programmatic tests. The global
`/Users/vossmedien/.codex/AGENTS.md` now makes this mandatory. Do not start more
test-matrix, fixture, coverage or assertion-review work while a controllable
startup/product failure or unfinished integration is the next action.
Optional coordinator test-review requests (including `01a0730e`) are deferred
until working integration/E2E. Preserve existing code/evidence and essential
safety checks; no warning suppression or false pass. Desktop was notified in
`01a07314-fafd-7a91-a437-86af1e577625`, Sync in
`01a07314-fcdd-7831-a259-ca32ec9a6e0e`. No ACK loop requested.

## Latest material readback

- **Corrected local native lifecycle PASS, current installed6ae4070:** Desktop's
  source-bound report `docs/audit-evidence/2026-09-08-native-shared-tabs.md`
  records visible Create/Save/Unsave/Quit/restart/close. The org Page keeps
  ID5e58f845… through temporary1 -> saved0 -> temporary1 -> restart; explicit
  close tombstones only that Page while savedcom remains. The unexplained
  external focus/width change is NOT an uninterrupted no-focus-shift pass.
  Coordinator independently matched installed source/executable to
  `artifacts/install/ahoi-dev-6ae4070-20260908.json` (activation verified,
  binary9870d0a9b99d849c060dad9e42e612a3a7841f98d5e57b730b1cca03599e6b8e).
  Root performed no native UI action; Desktop retains the runtime lease.
- **Actual native CloudKit readiness, 2026-09-08:** installed6ae still lacks
  the CloudKit-container/keychain-group bundle values and embedded Mac profile.
  Source `FromMainBundle` returns nullopt without a configured iCloud container.
  Today's read-only Xcode-cache scan found only two matching Ahoi profiles,
  both iOS/xrOS/visionOS, not OSX; the secondary MobileDevice cache is absent.
  This is not a claim about current Portal availability. The documented
  Development preparation on a copy of the built candidate is the next native
  transport prerequisite, not another wire redesign. Desktop was sent the
  concrete metadata/readiness result; no profile/key/portal mutation occurred.
- **Settings source41de599 is a separate handoff:** the implemented catalogue
  contains24 total settings (five existing Ahoi plus19 Chromium entries),
  native USER/default/reset semantics and matching Swift validation. It is not
  built into6ae/Build17 and not a full ADR0010 or cross-client pass. Mobile apply/
  provider, broader meaningful settings, Extensions and workspace pins remain.
  One specific catalogue coupling question was sent to Sync in
  `01a0807a-ea76-7be0-bcf8-2ce4de4fa33b`: homepage_is_newtabpage is included while
  its dependent homepage URL is excluded. Resolve the semantics before the
  Settings candidate; do not introduce arbitrary URL transfer or a test matrix.
- **Direct user toolbar addition, 2026-09-08:** hard rectangular Reload hover,
  optional address-bar pinning and native Home were assigned to the existing
  Desktop owner in `01a0806a-dc24-7d93-90ee-113e19d3bc92`. User screenshot is
  preserved at `artifacts/computer-use/toolbar-feedback-20260908/user-toolbar-hover.png`;
  the original was not removed. Scope and the short visible acceptance are in
  the coordination prompt. No product file or current candidate was changed
  by this handoff; the active Save/Unsave correction finishes first.
- **First native Save journey found a product UI defect, 2026-09-08:** Desktop's
  visible `c20a759` run saved the Page correctly in the store, but the Sidebar
  retained its temporary presentation. This is a controllable product failure,
  not an external gate or native E2E pass. Desktop committed the narrow existing-
  observer refresh correction `272385f` (one Session file, ten added lines).
  Next is its corrected candidate and repetition of the same Save/Unsave/restart
  journey; no new Sync architecture, broad tests or ownership handoff is needed.
- **New native candidate BUILT + INSTALLED, 2026-09-08:** corrective app-only
  build82463 from clean snapshot `c20a759dd936cfa93d5fedeb4c9dcd52e876bcd7`
  completed EXIT0, including signing and portable-bundle checks. Its Sync and
  TabTree source matches the grouped `dfcc32e` correction; Inbox7a47063 is included.
  Coordinator read the completed build log/receipt, installed plist and actual
  executable SHA256 `7f34223ff06d7b430acab72d6f57d34072efeb3db5b1ff9363d566e1396ef189`.
  Canonical install receipt:
  `artifacts/install/ahoi-dev-c20a759-20260908T092211Z.json`; activation verification
  is true and bundle-tree SHA is
  `d17efb433b0fa822fbde0ad0be476acb49d1bef85f4564870f1ca8be19583010`.
  Previous4cb remains at the receipt's explicit rollback path. No prior Default/
  failed-Arc profile is an acceptance fixture: Desktop's next visible journey
  uses a fresh isolated profile. Native E2E and real Desktop/Mobile CloudKit
  acceptance are still OPEN; no test suite was substituted for the visible app.
- **Compiler corrections are committed, 2026-09-08:** Desktop `5a15614` retains
  all four public sequence DCHECKs and the two private helper context contracts,
  removing only duplicate helper acquisitions; coordinator checked all callers.
  Common `dfcc32e` separately fixes the virtual-default definitions/GN, override
  marker and SyncStore helper guard. Its source ancestry includes both5a15614
  and Inbox7a47063 (both ancestry checks EXIT0), with no ADR0010 expansion in
  this six-file correction. These are source fixes, NOT a successful rebuild.
  Desktop's terminal readback confirms build71760 EXIT1 at08:59:56 UTC.
  Next: its one guarded cached app-only corrective candidate, then visible E2E.
- **Native build71760 is TERMINAL FAILED, 2026-09-08 08:57 UTC:** the canonical
  `desktop-shared-tabs-74ceb15-20260908/build-0843.log` ends with Ninja unable to
  make progress after product compiler errors; wrapper/build PIDs24145/24426 are
  absent. Owner's exact terminal exit-code receipt is still to be read; no new
  build/install/E2E pass exists. Do not resume the old live-PID instructions below.
  Deduplication gives three compiler-error classes across five owned files:
  Native TabTree node mutations/snapshot and Common SyncStore have sequence-
  context annotation conflicts; Common UI-bridge has nonempty inline virtual
  definitions; Common backend has a missing override marker. Native correction
  stays Desktop-owned, Common correction/GN stays Sync-owned. Exact bounded
  assignments: `01a0803e-a008-7080-ad85-5e4692466e96` (Desktop) and
  `01a0803e-9e2e-7c02-92a3-c2496e3487e9` (Common). Next is ONE coherent cached
  app-only correction, including the already delivered Inbox fix7a47063; no
  per-file rebuilds, warning/assertion suppression or extra test-binary gate.
- **Desktop guarded app build is live, 2026-09-08 08:44–08:46 UTC:** overlay
  succeeded and the same frozen `74ceb15` snapshot entered `build-ahoi.sh dev`
  with `AHOI_JOBS=2`, no extra test binaries. Coordinator verified wrapper
  PID24145 / build PID24426 and the advancing preflight log
  `artifacts/build/desktop-shared-tabs-74ceb15-20260908/build-0843.log`.
  This is a running pipeline, not compiler completion or a new installation.
  Capacity had recovered; the previous CPU wait is historical. Common's
  requested canonical Inbox correction `7a47063` was committed AFTER this
  source freeze and is not included. Desktop was notified of that exact
  candidate boundary in `01a08032-6386-7363-a763-03d9d76f1f5f`; include it in
  an owner-controlled incremental candidate before matching Inbox/Sync
  acceptance. Never mutate the running frozen checkout or start a second build.
- **Mobile bounded handoff is documented, 2026-09-08:** the owner explicitly
  returned UI in `01a0801b-0086-71f0-97f5-46f712d925ad`. The complete local report
  is `artifacts/e2e/mobile-shared-intents-7b706a7-20260908/README.md`.
  Coordinator read its raw `sync-boundary-tests.log`: four named XCTest
  `SyncBoundaryTests` cases executed with zero failures after the visible
  journey. The separate trailing Swift Testing runner's zero cases are NOT
  the pass. This verifies the shared-source allowlist/ciphertext-shape/current-
  format boundary only, not a cryptographic roundtrip, Unsave, Bookmark Sync,
  Chromium or real CloudKit. No product bytes were changed by those four
  existing test updates. Next is implementation/candidate integration, not
  replaying this bounded acceptance or expanding its test matrix.
- **Bounded Simulator window CLOSED, 2026-09-08:** at 08:20:24 UTC Mobile
  explicitly stopped further UI actions and confirmed shutdown of its own
  test device. Coordinator independently read F8253C50… as Shutdown, with
  BetterConvo and MindBodyCompass still Booted. On that explicit cessation plus
  cleanup evidence, the coordinator ends the reserved window; Desktop's retained
  native UI ownership is no longer held source-only by this test. No extra ACK
  or repeat user approval is required. This does not grant a My-Mac host, new
  build/install or control of another project's Simulator.
  The last UI call at 08:16:40 unexpectedly returned the MindBodyCompass window;
  its two click effects are not proven and are excluded from Ahoi acceptance.
  The affected owner was resolved by PID+CWD and notified in
  `01a0801c-096b-7062-8ac3-0d158d8e1c68`. No foreign process/data was changed by
  the coordinator. Future Simulator UI needs actual window isolation or specific
  coordination with concurrent Simulator projects; a new device alone did not
  isolate the shared native control surface. Mobile's 08:18 saved after-restart
  record matches the three original IDs and saved selection. Preserve these
  bounded Save/restore proofs; Unsave and real cross-device Sync stay open.
- **Build17 Save + restart evidence, 2026-09-08 08:13 UTC:** coordinator viewed
  the owner's `navigation.png` / `saved-tab.png` in
  `artifacts/e2e/mobile-shared-intents-7b706a7-20260908/` and compared the
  before-restart record with the live persisted session after restart. Exactly
  one selected saved row remains, with the same Runtime `391646fb…`, Presence
  `e4a95271…`, Page `18126d0f…` and deterministic Inbox IDs. New app PID67334
  replaces PID48730 in the same isolated Simulator. The owner's native AX result
  at 08:09:13 shows `Inbox · Gespeichert` and that same selected row ID.
  This confirms the bounded local Save/restore journey, not Unsave or CloudKit.
  Unsave remains unproved because native long-press input has not reached its
  context menu. Explicit UI handback is still pending; absence of the first PID
  was a restart, not a runtime-slot release.
  One actual visible layout defect was sent to Mobile in
  `01a08011-0102-7f63-99e0-266c3777cb5c`: even `example.com` is truncated to
  `exa…` by the action row. It belongs to existing UI polish, not a new test gate.
- **Mobile UI is in progress, 2026-09-08 07:58 UTC:** the Mobile owner has
  consumed the explicit user grant and started the isolated Build17 journey.
  Its live report confirms visible start/navigation and Inbox assignment;
  Save/Unsave and restart are the current steps. Some Simulator swipe inputs
  have been unreliable. This is a partial owner-observed journey, not a final
  Mobile or cross-client Sync pass. Do not retain the old unanswered-slot block.
  The coordinator goal was freshly read back as ACTIVE at 08:00 UTC.
  Independent readback at 08:01–08:03 UTC found live app PID48730 in the isolated
  `Ahoi Unified Sync Build17 20260908` Simulator
  `F8253C50-E423-4424-8EE3-5F152C593A31`. Its installed plist reports Build17,
  `7b706a73f98802fe31b206731b7e88c1da6b6c59`, DebugLocal. Both existing owner
  sessions remain live. The coordinator started no app/build/test and left
  the separate BetterConvo and MindBodyCompass Simulators untouched.
- **Explicit user Simulator-UI handoff, 2026-09-08 07:08 UTC:** the user answered
  yes to the Mobile owner running the short Simulator journey while Desktop
  works on source only. This resolves the previously unanswered UI-slot gate;
  do not request the same approval again or retain an old no-slot statement.
  Sync/Mobile owner uses the built `bba0b86` DebugLocal build16, or a necessary
  same-scope corrected successor with its own source/candidate receipt, in a
  fresh isolated Simulator context after a current capacity check. A concrete
  product correction does not require the same UI approval again. Desktop continues
  code work without native UI actions for this window. No My-Mac/iPad-on-Mac,
  `/Applications` replacement, desktop profile, existing key, portal or Production
  action is included. Mobile explicitly returns the UI window after its bounded
  visible journey and cleanup; source/build ownership otherwise stays unchanged.
- Global and project AGENTS.md were re-read after that approval. The new clauses
  require reusing granted approval for the same scope, finishing authorized
  preparation before approval requests, avoiding inferred skill approval gates,
  and concise result-first communication. Current total-capacity/E2E-first rules
  remain. No further approval gate is added by this coordinator.
- Mobile is now a committed source package `4e64c5f` (41 Mobile files, 8 shared
  Swift files, 1 checkpoint). Its first product build failed at one obsolete
  external-open call; `bba0b86` fixes that route without weakening guards.
  The same incremental DebugLocal arm64 Simulator build then reported
  **BUILD SUCCEEDED**. Coordinator read the actual completion log and product
  plist: source `bba0b86ad2a4b67fe0c6ff0ca763a3ca1e6bfabb`, build16.
  Evidence: `artifacts/build/mobile-unified3-4e64c5f-20260908/`, especially
  `correction-bba0b86.log` / `.xcresult`; the earlier red run remains separate.
  This is a provider-free built candidate, not visible E2E or cross-client Sync
  acceptance. Next is the owner's representative Mobile journey, not a rebuild.
- Native `cc7e7e3` adds a local tree+baseline persistence envelope and atomic
  database replacement while ordinary logical edits retain the receipt.
  Coordinator inspected the storage changes. The UI store is in-memory, so
  the durable asynchronous apply acknowledgment/original-scope checks are a
  remaining integration step, not proven by this storage-only commit.
- Desktop also explicitly accepted native install/enable hooks and the
  StorageFrontend patch for ADR0010. Common/Swift setup data and orchestration
  remain with the Sync owner; neither role is handed back to this coordinator.
- Current global and project `AGENTS.md` were read completely on September 8.
  A read-only local prompt-input check confirmed both files, the new total-machine
  capacity rule, equal treatment of projects and E2E-first sequencing. The old
  single-process 80% gate and Ahoi priority are superseded; source/out/UI leases
  remain unchanged. Both active owners were notified in
  `01a07f8e-f28b-7c41-af80-a57796ad09d0` / `01a07f8e-f49d-7302-9f4a-7bc3c3bbb0cf`.
  Fresh process readback bound Desktop PID74605 and Sync PID75291 to their exact
  resumed thread IDs and the canonical workspace; old process IDs are history.
- Installed plist readback on September 8 is still `4cb622a`. Earlier visible
  results below retain their original artifact/environment scope; no rerun or
  present runtime lease is inferred from them.
- The user confirmed scope additions made directly in both owner sessions.
  Desktop owns workspace-local cookie/session isolation after its current
  package; Sync owns eligible Chrome settings and extension restoration/settings.
  Cookies/passwords/secrets remain local; no raw profile/extension-store copying.
  See the scope addition in the coordination prompt. Existing ownership stands;
  neither addition widens the already frozen startup correction.
- Startup correction `c986090d99c22318aef4d45378208cece6878d27` is built and
  installed. Coordinator read the completion/install logs and receipt
  `artifacts/install/ahoi-dev-c986090-20260905T201925Z.json`, then independently
  matched the installed plist and executable SHA256
  `dbd2c25bb8e22af6cd2424db7fa967972fa19f178031fde8c269eac2925c9cc5`.
  This supersedes installed926 below. Desktop then visibly opened the session
  and a second window, but native fullscreen hit a distinct CHECK in
  `CustomCornersBackground::Paint:277`; the overall journey remains RED.
  Its bounded correction is committed as `7de7fac`; clean derived source
  `4cb622a` excludes Common/Swift WIP. Overlay31405 and product-only build73875
  completed successfully. Coordinator then verified the installed source and
  receipt `artifacts/install/ahoi-dev-4cb622a-20260905T204104Z.json`, including
  successful activation and executable hash
  `55301ccbda32e32d3ee57420bd10adc3581b96a918047dbbb82815a56134770b`.
  Desktop's 20:45/20:50 public E2E readback passed start, second window,
  fullscreen in/out, navigation, close, Cmd+Q and restart. This is the owner's
  visible result, separate from the coordinator's receipt readback. Arc import
  has not executed (Arc remains running); the settings-menu UI connection is
  still being checked. No full-browser or unified-Sync pass is implied.
  No runtime lease was handed over. Live details are in
  `docs/ACTIVE_DESKTOP_CHECKPOINT.md` and `desktop-startup-guard-20260905` logs.
- Sync continuation is `docs/UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md`
  (`7518c2f`): Mobile Page/Presence capture, explicit closing and intent/projection
  separation are now WIP code. The old URL-filter/absence-delete publisher is
  removed; restart duplicate rows have a source correction using binding readback
  before mirrors. Common native capture has since been handed over in `e2f6711`
  below. Mobile lifecycle/navigation/unsave completion, Native B-D implementation
  and ADR0010 setup adapters remain open. No build/test/runtime pass is claimed.
  Old codec/marker/ownership fixes are not reopened.
  Golden assertions remain unexecuted and optional expansion deferred; the
  earlier `3e9552f` DCO follow-up remains preserved.
- On the same installed `4cb622a` candidate, AnyChat 1.0.8 reached the native permissions
  dialog and the Cancel journey passed. Actual installation remains user-gated
  because of the disclosed AI-site/NTP/favicon permissions. Arc still needs the
  already requested regular close; neither import nor recovery has mutated data.
  Its second permission sheet was also cancelled; no modal remains open and
  approval is still pending. The owner resolved background-start menu-only UI
  by raising the observed native window before keyboard input, not by another
  build/reset. See `513ae54` and `58a32ae`; no duplicate user question or new
  runtime handoff here.
- The bounded Sidebar docked/floating/hide/restore journey passed on `4cb622a`,
  followed by five unchanged State/Layout regressions (`7822f3a`). Coordinator
  read all five SUCCESS results and verified the selected source/header/test
  files have no `c986090..4cb622a` differences. Reused runner/shared-runtime
  limits are explicit in `artifacts/tests/sidebar-presentation-20260906/README.md`;
  this is not a whole-candidate or unified-Sync pass. No test rerun here.
- Common C++ is now a committed, pushed source handoff:
  `e2f67111fcb02f08eabe44b6fbac52f0afb3a57b` (September 8). Coordinator checked
  its manifest: 50 Common files, 3 configs, 2 docs; no Native/Swift files.
  Service/getter/Observer/capture and receipt-backed backend/journal code are
  present. Exact callable seams and original-authorization requirements are in
  the current `docs/UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md`.
  The missing Common-code wait is RESOLVED; Desktop now implements B-D,
  including atomic native tree+receipt persistence, against this concrete source.
  Receipt metadata is local, not a new wire field or migration. Source handoff
  is not native implementation, compilation, E2E or unified-Sync acceptance.
- Coordinator observed repeated delayed 92694fe/SQL replies at 20:50–20:52 and
  sent `01a07359-5e4f-7242-a33b-0b407858ba53`: no repeated old handoff checks;
  continue the actual Service/capture/backend and Mobile binding work. ADR0010
  remains fully in scope. That was earlier execution steering; the later actual
  Common handoff is `e2f6711`, not another role/freeze question.
- The separate stale active Desktop "Next actions" finding from
  `01a0737f-d71e-76b3-86ed-7d68cc9ff160` is FIXED in `5d9f8c1`, verified by the
  coordinator. Its eight active steps now bind `4cb622a`; old926/c986/PIDs are
  explicitly historical. Do not reopen this finding or replay those builds.
- A fresh read-only installed-profile inventory decoded all 9 profiles under
  `/Users/vossmedien/Library/Developer/Xcode/UserData/Provisioning Profiles`.
  The two Ahoi matches (`bbf658ff-10e4-4768-8cac-de5787ab5e78.mobileprovision`,
  Development; `6deb44ce-7b3e-4db1-8b72-482a8daf6ee1.mobileprovision`, Store)
  carry team248AJ5BN47 and the intended CloudKit container, but list only
  iOS/xrOS/visionOS, not native macOS. The second standard MobileDevice profile
  directory is absent. This does NOT prove absence in the portal/other paths;
  a compatible native Mac profile remains unproven. No profile/key/portal/runtime
  change. Metadata-only handoffs: Desktop `01a0735f-6ae7-77d2-b537-28bf9bcb64e7`,
  Sync `01a0735f-6b71-7b02-a11d-54a9084a5564`. Do not stop source integration
  for this later candidate gate or substitute the iOS profile for a Mac profile.

## Assignment and current acknowledgments

The user explicitly approved one unified Sync implementation owner while the
other existing Terminal agent continues its Desktop package, on the same branch.
Additional direct user messages in the Sync session at 16:35/16:42 UTC request
one format across relevant data and explicitly avoid complex migration because
the app is not actively used. Fresh isolated acceptance stores are authorized;
existing profiles/CloudKit data are not to be deleted. This clarification
supersedes migration/legacy-reader work in the initial coordination wording.

- Sync: `01a06d69-1034-7372-b784-0b05a53c87e0`, observed Terminal `ttys011`.
  Existing common C++ scope plus Swift `spikes/cloudkit` / `apps/AhoiMobile`
  transferred cleanly through source `f25eea5`; explicitly accepted in
  `ae740b5192ac7e64e149bc58aa64de306690e2a8`. ADR 0009 and the Sync checkpoint
  now assign both languages to this one owner. No further ownership ACK or
  separate Mobile implementer is needed. The agreed sequence is common format/
  fixtures and fresh C++ store, matching Swift, native/Mobile binding, then a
  coordinated candidate/E2E/focused-test wave. Initial handoff message was
  `01a07275-a813-73b2-aabb-40399075fa5d`; delayed notices using the old split
  do not reopen it. The unified contract remains `1bfae11`.
- Desktop: `01a04f97-e3ba-70f2-a031-220b214d352d`, observed `ttys002`.
  Browser package, native Tree/Session/UI and adapter, Chromium checkout/out,
  build/sign/install/native runtime retained. Explicit role acceptance received
  after reading committed `48b1ef4`: Desktop will not edit the unified common
  C++ or transferred Swift scopes without an exact handoff. Initial assignment
  message: `01a07276-1cbe-7942-a71c-ba76f2ddc30e`. No further role ACK needed.
- Coordinator: read-only product review/coordination, this checkpoint/prompt;
  no parallel product edits in the transferred scopes. Native integration
  changes require exact Desktop/Sync file or interface handoffs.

Queued messages are not acknowledgments. Terminal/PID observations are not leases.

## Concrete native integration boundary

Desktop explicitly accepted all native packages A-D in `59a0378`, detailed in
`docs/SHARED_TAB_NATIVE_SEAMS.md`: target-type alias, SessionBridge/TabTree
identity and persistence, adapter projection, sidebar capture/close. No native
files are delegated back. Common test files/GN need separate exact coordination.
Native A is now unblocked by the explicitly delivered `5885d01` five-file
common code handoff: shared target/capture/state types, default-false UI bridge
and isolated GN `shared_tab_types` leaf. Coordinator inspected the full manifest,
headers and dependencies; no writer/model/store changes. Desktop has the exact
code/dependency handoff for its alias; no further A format/ownership ACK needed.
B-D now have the actual Common Service/backend handoff `e2f6711`. Native
implementation remains Desktop-owned and is not yet runtime proof. In particular,
complete tree+receipt export and authorized atomic apply/persistence must be wired;
their fail-closed defaults are not a working native adapter. Baseline work stays separate.
Desktop has consumed Native A in `906dac8`: exactly the Session target-type aliases
and their GN leaf dependency, documented in e4df043. This is source-only and is
not included in the unchanged installed4cb candidate; do not request A again.

## Historical evidence (superseded by the latest readback above)

- Branch: `codex/desktop-core-feature-wave-20260830`.
- Historical `30212` / `225df88` ended EXIT 1. Its five API diagnostics were
  resolved by `5794d37` + `22e2f2b`; that exact source handoff is accepted.
- Overlay `65019` ended EXIT 0; `7945` / `22e2f2b` ended EXIT 1 (`752723f`).
  Remaining diagnostics were only raw `kLegacyRows[i]` access in one fixture;
  Sidebar tests linked, not an overall-build/test pass. Full logs/reports:
  `artifacts/build/desktop-test-api-22e2f2b-20260905/`. Never poll/restart 7945.
- **Source blocker now resolved:** `92694fe36539d024af6567646103f1cf246d5364`
  is separately committed/pushed. Coordinator verified its complete one-file
  3+/2- diff: `<array>` and `std::array<LegacyRow, 2>`; payloads and assertions
  unchanged, no warning suppression or unified-format WIP. Desktop received
  concrete continuation message `01a072e3-ba51-7293-a596-0ec6a2938f52` after its
  blocked report. The existing cached snapshot/build should continue only with
  that bounded fix and a fresh CPU gate. Desktop subsequently accepted the
  resolved blocker in `82c4074`, selected the clean `92694fe` snapshot and
  started overlay `60701` after the 18:52 UTC gate cleared. Coordinator then
  directly observed its live guarded Dev wrapper (PIDs 94538/94819) doing
  overlay verification, logging to
  `artifacts/build/desktop-fixture-92694fe-20260905/build.log`.
  Work has resumed; its later result is recorded below.
- **Build verified:** Desktop confirms `17302` TERMINAL EXIT 0 on clean
  `92694fe`, after overlay `60701` EXIT 0. Coordinator read the completion/signing
  log and `LINK ./ahoi_sync_unittests`, then verified the canonical receipt
  `artifacts/build/ahoi-dev-build-92694fe36539.json`: source `92694fe`, clean,
  overlay applied, SHA256
  `dec35aefe6a4095fa784dcd5f2cf186a8005e9ac53c5f81e4b6ed4163f3772a1`.
  This is compiled baseline evidence, not executed tests or unified-format proof.
- **Installation verified:** the guarded install log reports completion and
  `artifacts/install/ahoi-dev-92694fe-20260905T190452Z.json` verifies candidate,
  same-volume copy and installed activation (`renameatx_np(RENAME_SWAP)`).
  Coordinator read the installed plist source `92694fe` and independently
  hashed its executable to
  `717827e792a4882665334dc834ec54680696ae8b2cc93a88399b176741b945ca`, matching
  the receipt. Receipt SHA256:
  `502d6ac0bb82153c9e227534a20034785621e1e56ec4f842c3eb7678676d00aa`.
  `releaseEvidenceEligible` is false. No coordinator app launch/UI action.
- **Visible acceptance red:** Desktop's 19:17 UTC public readback reports that
  the new app also exits after a targeted Finder start and produced two new
  crash reports. Desktop is analyzing them without repeated launches. Exact
  diagnosis/evidence handoff is pending; build/install success is not E2E success.
  Its 19:20 UTC update localizes the crash to a toolbar callback asking for
  fullscreen state before BrowserView has a Widget. Desktop owns the lifecycle
  correction and its independent attachment/reapply review; no duplicate
  coordinator implementation or app launch.
- Minor convention follow-up: the read 92694fe commit body lacked a DCO trailer.
  Sync was asked for a traceable attestation without silently rewriting published
  history (`01a072e3-bcd5-7b03-9dfb-3e114c1f26b7`). This is separate from whether
  the source compiles. The partial example queue prefix in that message is not
  evidence; the exact Desktop continuation ID is the one recorded above.
- No current CPU state is inferred from historical Blender/Unity PIDs. Each
  intensive start requires a fresh gate. No coordinator build or runtime lease.
- Installed Desktop is now `92694fe`, independently read back above. It remains
  the older-format UI/compile baseline, not a unified native/Mobile sync pass.
- Swift `3964bcb` is frozen-contract preparation; `895daf9` and `f25eea5`
  correct retained local provenance. Seven provenance test methods are written,
  not executed. No Mobile build/test/host or My-Mac lease is owned here.
- Earlier `313e351` / Build15 Bookmark acceptance remains its own evidence;
  it neither covers the new source nor needs an unchanged repetition.

## Delivered unified-format source and remaining proof

- ADR 0009 and `config/sync-format.json` are committed in `1bfae11`; Desktop
  aligned its master/native contracts in `006930f`. The manifest reads as exactly
  13 entity IDs 0–12, all authored/accepted at 3; obsolete input must not turn
  into a deletion or an authoritative empty snapshot. Golden bytes are now
  committed as `c3c3d20` at
  `overlay/chromium/src/ahoi/browser/sync/testdata/sync_wire_v3.json`.
  Coordinator verified SHA256
  `f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00` and 26
  records. C++ `sync_unified_serialization_unittest.cc` directly reads that
  file and asserts hash, 26 cases/13 types and exact byte roundtrip; Mobile
  `project.yml` includes the canonical resource. These prepared assertions
  are not a C++/Swift execution pass; the four boundary tests above do not
  execute the all-entity codec roundtrip.
  The remote-command example is shape-only, not cryptographic authorization proof.
  The early optional test-isolation review `01a0730e` is historical source input,
  not another prerequisite before the working candidate/E2E sequence above.
- Common C++ is no longer an uncommitted leaf/header proposal: `e2f6711` is
  committed and handed over, with durable async/export corrections `5e74472`,
  `37bc558` and cancellation handling `40358d1`. Desktop reported native capture
  and projection connected at 08:04/08:21 UTC on September 8. Its exact native
  freeze/build/runtime proof still belongs to Desktop; no new compiled native
  candidate is inferred from those source reports.
- Shared Swift/Mobile is no longer merely initial WIP: `7b706a7` compiled as
  Build17 and has the bounded local navigation/Save/restart evidence above.
  Remaining live cross-client/Unsave and ADR0010/workspace setup work stays with
  the same owner. Do not reload the September 5 checklist as today's task list.
- Direct Terminal Computer Use was denied by the tool's safety boundary. No
  workaround or process interruption was attempted. Coordination continues via
  the existing CLI queue and scoped read-only process/log/checkpoint evidence;
  a queued message is still not an immediate steering or acceptance proof.

## Next actions

1. Both owners have accepted their roles. Follow the concrete package sequence
   and native header handoffs; no further role-confirmation loop.
2. Desktop finishes the narrow native Sidebar-refresh correction `272385f` and
   repeats the affected visible journey. The Common compile fixes and Inbox are
   already integrated in built/installed c20a759; no missing-API/role wait or
   replay of the old compiler batch remains. Sync continues
   the remaining Mobile/live integration and ADR0010 setup adapters; the local
   Build17 journey and four focused checks are complete within their stated
   scope. Coordinate heavy phases by actual total capacity, not old CPU rules.
3. Close remaining fresh bootstrap and cross-client live binding
   gaps, using exact native adapter handoffs where needed; target one active
   model, not perpetual v2/v3 writer coexistence or complex old-data migration.
4. Agree the next coherent source freeze and candidate-bound verification.
   A native Development-capable app/profile/key-bootstrap and a real roundtrip
   are still unproven; no account/portal/Production authority is implied.

Only update this checkpoint on meaningful state changes. Preserve detailed
historical build/test evidence in the existing reports, not repeated ACK logs.
