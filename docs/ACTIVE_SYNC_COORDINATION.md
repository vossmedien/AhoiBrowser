# Active sync coordination

Updated: 2026-09-06 (local time). Coordinator: `01a044d6-1545-7532-8394-6b7df1144bb1`.
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
  before mirrors. Still open: Common native capture, Mobile lifecycle/navigation/
  unsave completion and ADR0010 setup adapters. No build/test/runtime pass or
  B-D API handoff is claimed. Old codec/marker/ownership fixes are not reopened.
  Golden assertions remain unexecuted and optional expansion deferred; the
  earlier `3e9552f` DCO follow-up remains preserved.
- On the same installed4cb candidate, AnyChat1.0.8 reached the native permissions
  dialog and the Cancel journey passed. Actual installation remains user-gated
  because of the disclosed AI-site/NTP/favicon permissions. Arc still needs the
  already requested regular close; neither import nor recovery has mutated data.
  The owner resolved background-start menu-only UI by raising the observed
  native window before keyboard input, not by another build/reset. See513ae54
  and58a32ae; no duplicate user question or new runtime handoff here.
- Coordinator observed repeated delayed 92694fe/SQL replies at 20:50–20:52 and
  sent `01a07359-5e4f-7242-a33b-0b407858ba53`: no repeated old handoff checks;
  continue the actual Service/capture/backend and Mobile binding work. ADR0010
  remains fully in scope. This is execution steering, not a new role/freeze or
  a claim that the missing B-D code handoff has arrived.
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
B-D still await the actual service/getter/capture/backend implementation handoff;
these interfaces alone are not working native behavior. Baseline work is separate.
Desktop has consumed Native A in906dac8: exactly the Session target-type aliases
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

## Verified unified-format progress (not acceptance)

- ADR 0009 and `config/sync-format.json` are committed in `1bfae11`; Desktop
  aligned its master/native contracts in `006930f`. The manifest reads as exactly
  13 entity IDs 0–12, all authored/accepted at 3; obsolete input must not turn
  into a deletion or an authoritative empty snapshot. Golden bytes are now
  committed as `c3c3d20` at
  `overlay/chromium/src/ahoi/browser/sync/testdata/sync_wire_v3.json`.
  Coordinator verified SHA256
  `f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00` and 26
  records. C++ `sync_unified_serialization_unittest.cc` now directly reads that
  file and asserts hash, 26 cases/13 types and exact byte roundtrip; Mobile
  `project.yml` includes the canonical resource. Actual Swift codec-test
  assertions and both languages' execution remain for the candidate wave.
  The remote-command example is shape-only, not cryptographic authorization proof.
  Two WIP test-isolation gaps were reported in `01a0730e-2e27-7b02-aeba-0efd288e7660`
  (unknown-only field map; malformed field logical counters); see the appended
  section in the coordinator's Swift integration checklist. No fix/test pass inferred.
- Actual uncommitted C++ implementation is now visible in common model/store/
  profile types and the two new shared-tab leaf headers. Coordinator inspected
  the leaf types, separate presence/logical IDs and default-false native support.
  This is WIP, not a header freeze, compiled result or writer activation.
- Swift implementation has now started too: central `SharedSyncFormat`, exact
  live class/version boundary and Capability `[3]` defaults are visible WIP.
  The coordinator's remaining-path review is
  `docs/reviews/2026-09-05-unified-sync-swift-checklist.md`; it is source-only,
  not a claim that unfinished codecs, snapshot markers or live bindings pass.
- Direct Terminal Computer Use was denied by the tool's safety boundary. No
  workaround or process interruption was attempted. Coordination continues via
  the existing CLI queue and scoped read-only process/log/checkpoint evidence;
  a queued message is still not an immediate steering or acceptance proof.

## Next actions

1. Both owners have accepted their roles. Follow the concrete package sequence
   and native header handoffs; no further role-confirmation loop.
2. Track the next bounded native correction handoff/build without starting
   competing intensive work. Source, build, tests, signing and install
   remain separate gates.
3. Sync owner closes matching C++/Swift, fresh bootstrap and live binding
   gaps, using exact native adapter handoffs where needed; target one active
   model, not perpetual v2/v3 writer coexistence or complex old-data migration.
4. Agree the next coherent source freeze and candidate-bound verification.
   A native Development-capable app/profile/key-bootstrap and a real roundtrip
   are still unproven; no account/portal/Production authority is implied.

Only update this checkpoint on meaningful state changes. Preserve detailed
historical build/test evidence in the existing reports, not repeated ACK logs.
