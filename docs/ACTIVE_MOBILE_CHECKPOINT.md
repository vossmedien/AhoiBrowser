# Active Mobile checkpoint

Updated: 2026-09-19. The bounded Mobile worker
`/root/simulator_cloudkit_20260919` has returned sourcef21d089 to coordinator
`/root`. No active Mobile build/UI run or new general Common ownership is implied.

## Current work — continue here

Latest exact Mobile result is clean DCO source
`f21d08929e407c6b17cb999c1db944e6b82ac8db`,
`CloudKitDevelopment`0.1(38), Xcode27/SDK27, existing fe842 scope and the
already signed-in C645/iOS26.5. A one-job product-only build compiled exactly
four product targets with no tests/harness; candidate receipt and installed
source/build/scope readback passed. The normal manual Settings journey reached
`Status: Bereit`, `Verschlüsselung bereit` and visible provider result
`Synchronisiert`; `Jetzt synchronisieren` was enabled. One normal manual Sync
action returned without visible error and retained `Synchronisiert`. This is a
real Mobile CloudKitDevelopment first-use/bootstrap and provider-status pass.
The optional RemoteCommand signing identity remained separately revoked or
unavailable and did not block payload Sync. No fixture, preference/key/account,
database or scope injection/reset was used. Cleanup returned Sync visibly OFF,
disabled manual Sync, terminated Ahoi and left C645 Shutdown. Exact receipt,
screenshot and report are in
[`artifacts/e2e/mobile-cloudkitdevelopment38-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment38-xcode27-20260919/README.md).
This does not yet prove a Mac/mobile roundtrip, CloudKit Production, push
delivery or physical-iPhone behavior.

Newest exact Mobile runtime result is clean source
`3a4f77830809cc6696bf57cad3e4eaeea39b6bff` with Mobile product source
`d098e06`, `CloudKitDevelopment`0.1(37), Xcode27/SDK27 and isolated scope
`fe842784-4865-4272-8bda-4bcf81a64a84`. Build/receipt/install/config readback
passed. A normal manual fresh opt-in reached terminal `Sync-Einrichtung nicht
verfügbar` / safe value `unknown`, not Ready. A bounded standard-debugger pass
on that exact installed candidate identified the actual factory error as
`CompanionPayloadKeyStoreError.authorizationRevoked` (same safe NSError domain,
numeric code10) thrown at `AppEntry.swift:345`, before optional RemoteCommand
signer setup. At the combined guard, runtime authorization was still true and
the verified bootstrap claim existed; only `canonicalKeySHA256(activeVersion)`
was nil. The concrete boundary is therefore an expected canonical payload key
that is not readable after lifecycle Ready, currently mislabeled by the guard
as authorization revocation and by UI as unknown. No key bytes/digest,
record/account identifiers, NSError description/userInfo or credentials were
retained. Debugger detached, Sync returned visibly OFF, Ahoi terminated and
C645 is Shutdown. Exact receipt, screenshot and sanitized cause are in
[`artifacts/e2e/mobile-cloudkitdevelopment37-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment37-xcode27-20260919/README.md).
This does not implicate the optional RemoteCommand signer, prove a CloudKit API
failure, require a physical iPhone or permit rewriting scopes969/37b/fe842.
Cloud acceptance remains OPEN; no further source/build loop is authorized by
the prior `unknown` label alone.

Newest actual candidate is clean descendant source
`d9faeaa1649acfe5f37a67d52a268811cd3fa39d`,
`CloudKitDevelopment`0.1(36), Xcode27/SDK27 and the new prepared Development
scope `37b55dda-30d6-4664-a91c-c91cab92d14b`. Its Mobile product source is
byte-identical to bc12536; Build35/old scope969 remains fully archived and
untouched. Build36 and receipt verification passed, and installed Info.plist
readback proved source/build plus the exact new Zone, Subscription and Keychain
account. The normal manual fresh opt-in started from a separate empty local
namespace and remained visibly setup-pending beyond2m56s without a typed error
or Ready. A bounded process sample observed the saved-record delegate event and
exact claim decode; a later sample showed neither Bootstrap nor Security/Keychain
work. This proves the event boundary, not the cause of the remaining wait and
not CloudKit acceptance. Sync then returned visibly OFF, Ahoi terminated and
C645 is Shutdown; neither scope received manual key/receipt/prefs/DB/Cloud
mutation. Candidate, receipt and sanitized phase evidence are in
[`artifacts/e2e/mobile-cloudkitdevelopment36-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment36-xcode27-20260919/manual-pending-phase.txt).
Follow-up source `d098e06` is DCO-committed/pushed but **NOT BUILT**: bootstrap
zone/read/claim now all use the same direct private CKDatabase, with an atomic
single-record `.ifServerRecordUnchanged` save, exact per-item/conflict handling,
account continuity on success and error paths, exact saved version/digest proof,
and persist-before-created. The main provider remains CKSyncEngine. A new clean
first-claim scope is required to exercise that source without rewriting either
preserved failed scope; no reset is implied.

Latest exact Mobile result is clean DCO source
`bc1253621c3bf9cbb73ba197ad1bbe7b2c718c8c`,
`CloudKitDevelopment`0.1(35), Xcode27/SDK27 and the unchanged signed-in
C645/iOS26.5 plus Structure scope969. Build and candidate receipt verification
passed. The exact candidate was then installed and launched normally without
the known30s XCUI deadline. Normal Sync OFF→ON reached the terminal visible
status `Sync-Wiederherstellung erforderlich` with safe evidence
`bootstrap-recovery:bootstrapOwnershipUnverified`: Build34 had already saved the
remote first-device claim, but its local accepted receipt/ownership is not
verifiable. Build35 correctly refuses to adopt, replace or promote it. This is
an honest fail-closed recovery boundary, not Ready, Sync or CloudKit acceptance.
No preference, key, receipt, account, database or CloudKit record was manually
changed. Sync returned visibly to OFF, Ahoi terminated and C645 is Shutdown.
Exact receipt, screenshot and report are in
[`artifacts/e2e/mobile-cloudkitdevelopment35-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment35-xcode27-20260919/README.md).
The immediately preceding Build34/red-harness plus bounded manual continuation
proved a saved claim event reached receipt persistence, then blocked in Security
XPC (`SecItemCopyMatching`/`SecItemUpdate`) while still inside the CKSyncEngine
delegate callback. That sample proves the boundary, not causal deadlock. Source
bc12536 moves receipt persistence after `sendChanges` and a fresh continuity
proof while preserving persist-before-`.created`; the existing scope cannot
prove that correction on a clean first claim without an explicit recovery or
new isolated-scope decision. Cloud acceptance therefore remains OPEN.

Latest exact Mobile result is now source
`719024631c05fa95d68fb21ce2b8e74f35e84eac`, `CloudKitDevelopment`0.1(30),
Xcode27/SDK27, C645/iOS26.5 and the unchanged Structure scope. Build and receipt
verification passed; the same normal Settings journey passed1/1 in34.998s.
After the user completed native Apple Account sign-in, the typed safe result is
`bootstrap-recovery:accountChanged`: Settings visibly shows
`Sync-Wiederherstellung erforderlich`, keys remain unavailable, manual Sync is
disabled and the existing explicit account-recovery/consent decision remains
pending. No local snapshot upload/discard confirmation was clicked and no
CloudKit roundtrip is claimed. The same candidate also proves the disabled
`Jetzt synchronisieren` action is now visibly gray/dim while preserving the
original single enablement condition. Exact receipt, screenshot and result are
in
[`artifacts/e2e/mobile-cloudkitdevelopment30-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment30-xcode27-20260919/README.md).
The preceding Bootstrap error seam now retains real CK9/CK10 access codes and
keeps local authorization/shutdown separate; neither was the final Build30
cause. Sync ended OFF, Ahoi was terminated and C645 is Shutdown. Peek-specific
visible acceptance remains NOT_RUN due the bounded CUA long-press input limit,
not a product failure.

The exact follow-up source is DCO-committed/pushed as
`32b5751261cfe0c4d847f3d6092fc55c96f25462`, **NOT BUILT**. Build30 proved
that the Bootstrap transport treated CKSyncEngine's first post-login `signIn`
event as an account switch. The corrected actor binds the CloudKit user-record
identity in memory only: the first `signIn` and matching fresh-engine replay are
accepted, while a different `signIn`, `signOut`, `switchAccounts` or pre/post
operation identity mismatch remains `accountChanged`. Identity continuity is
checked before and after remote inspection, zone save and claim creation; no
identity is logged/persisted, remote inspection still precedes every claim, and
no recovery upload/reset or consent decision is automatic. The matching Native
implementation already verifies account identity before/after its scan and
needs no C++ change from this finding. Next bounded gate is one normal candidate
and the same activation journey on the signed-in C645, not a new test matrix.

Latest September19 Mobile result: source
`42a1f835a3350ed35b758e92576f60061b6add39` is DCO-committed/pushed and the
receipt-bound `CloudKitDevelopment`0.1(29) Simulator candidate built EXIT0 with
Xcode27/SDK27, one job and the same isolated Structure scope. The same normal
C645 Settings journey passed1/1 in30.247s. It now reports the actual bounded
cause as `iCloud-Zugriff erforderlich`, leaves keys off and manual Sync disabled,
sets `configurationMissing=false`, and exposes only the safe evidence value
`cloudkit-account-or-permission`; final UI opt-out passed. The classifier keeps
static configuration, other CloudKit codes, Keychain OSStatus, concrete
bootstrap/waiting reasons and unknown fallback separate without retaining raw
NSError userInfo, account, record or key data. Exact candidate, receipt,
screenshots and result are in
[`artifacts/e2e/mobile-cloudkitdevelopment29-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment29-xcode27-20260919/README.md).
A separate read-only native Settings check found the generic Apple Account row
showing the system sign-in prompt: **no Apple Account is signed in on C645**.
No identity/screenshot was retained and no setting/sign-in was changed. This is
the actual user gate on this Simulator, but the retained transport code remains
correctly broader: `accountUnavailable` combines CloudKit `notAuthenticated`
and `permissionFailure` and can also guard revoked authorization/shutdown, so no
exact CKError code is invented. No CloudKit roundtrip/cross-device or Peek UI
acceptance is inferred. Sync ended OFF and the Ahoi app was terminated. A
bounded CUA attempt on public `example.com` could not generate iOS long-press:
the available right-click/short-drag inputs performed ordinary link navigation,
which was restored, so no Peek pass or product failure is inferred and the same
input was not repeated. On the user's explicit approval C645 is now intentionally
**Booted** at the native generic Apple Account sign-in method chooser. No method
was selected, no credential/identity was read or entered, and no screenshot was
retained; the Simulator window is handed to the user for their own sign-in.

Current September19 Simulator result: exact clean Peek source
`4ff983594a473db5295844953bbfcc532c12159f` is now the receipt-bound
`CloudKitDevelopment`0.1(28) arm64 Simulator app, built with Xcode27.0/27A266a
and SDK27.0/24A430 against the prepared Structure scope
`96950f6b-50e0-4e2c-9a94-852dc5099446`. The app/tree/runner receipt verified;
the Simulator Development entitlements contain the exact CloudKit container,
environment and two intended Keychain groups. The normal C645 Settings journey
passed 1/1 in31.506s: real Sync opt-in visibly settled at `Nur lokal` plus
`Verschlüsselungs-Wiederherstellung erforderlich`, missing configuration true,
manual Sync disabled and no operation error; the same UI then restored Sync OFF.
No fixture, SyncProjection, defaults/key/SQL or account injection was used.
Evidence, screenshots, exact receipt and retained red harness-path first attempt
are in
[`artifacts/e2e/mobile-cloudkitdevelopment28-xcode27-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment28-xcode27-20260919/README.md).
This is an honest runtime-activation boundary, not CloudKit success. Static
configuration is present, but the current error seam collapses thrown
CloudKit/Keychain failures and nonthrowing bootstrap recovery into the same
local/recovery presentation without retaining a safe NSError domain/code or
recovery reason. Therefore no exact `CKAccountStatus`, Keychain `-34018` or
specific bootstrap cause is claimed; no server roundtrip/cross-device Sync is
claimed, and Peek-specific visible close/adopt acceptance remains NOT_RUN.
The app was terminated and C645 returned to Shutdown.

Earlier September19 Simulator attempt: exact clean source `2c57c8d` was configured
as `CloudKitDevelopment`0.1(27), arm64 iOS Simulator, with the existing
Structure scope `96950f6b-50e0-4e2c-9a94-852dc5099446`. Xcode26.5/17F42 and
SDK26.5/23F73 compiled the App/Core source, but no runnable candidate was
produced: after the known two stale Core-test files were excluded from the
targeted UI harness, `actool` failed at Simulator asset thinning because the
active CoreSimulator device-type inventory mixed the Xcode26.5 service with an
Xcode27 runtime-data download. Both device-bound and generic-destination red
runs are retained in
[`artifacts/e2e/mobile-cloudkitdevelopment27-simulator-20260919/`](../artifacts/e2e/mobile-cloudkitdevelopment27-simulator-20260919/README.md).
No app was installed or launched, `CKContainer.accountStatus()` was not reached,
the Ahoi Simulator remained Shutdown, and CloudKit/Keychain/device acceptance
stays OPEN. Do not repeat the same `actool` run until the runtime inventory has
actually changed; do not stop the foreign download or restart global Simulator
services.

The native Mobile Link Peek follow-up is DCO-committed/pushed as
`4ff983594a473db5295844953bbfcc532c12159f`, implemented on top of the
existing long-press link coordinator and WebPage policy. The preview is an
explicit action, shows destination and source origins, keeps the initiating
browsing mode/workspace/WebKit data store, returns to the unchanged source page,
and adopts the already loaded WebPage into a normal/private tab only after the
user confirms. Preview alone creates no tab, history, session, Sync or restore
entry; external schemes, permissions, popups and downloads retain the existing
fail-closed page policy. Source/static/project-generation checks are complete;
the exact clean source now has a real **AhoiMobileCore BUILD PASS**: Xcode26.5,
arm64 iPhone Simulator, one job, Swift/localization compile plus framework link,
EXIT0. The graph contained only `AhoiMobileCore` and its local
`AhoiCloudKitSpike` dependency; preserved product/log/result are in
[`artifacts/build/mobile-peek-core-4ff9835-20260919/`](../artifacts/build/mobile-peek-core-4ff9835-20260919/README.md).
That narrow Core result itself built no app/assets/tests or runtime. The later
Build28 result above now adds app build and the normal Settings E2E, but does not
turn the unrun Peek-specific UI or CloudKit roundtrip into acceptance.
The link sheet only stages the request; its real `onDismiss` presents the
preview, without a guessed delay. Preview navigation/HTTP failures have a
visible error/retry path. Root read presentation, adoption and datastore/cleanup
boundaries; this does not substitute for the still-pending visible journey.

The corrected Device24/e2faf54 baseline is installed on Servusla and has a
normal product-launch/local-namespace readback, with Sync OFF. It is fa53e31
plus only the backportable AppStorage fix7e19476, matching Nativec8d9161; it
does not contain the later Structure/Private-Lock source. No visible UI or
CloudKit pass is inferred. Exact current candidate/results are in
[the unified checkpoint](UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md) and
[the launch receipt](../artifacts/build/mobile-development-e2faf54-20260912/product-start-receipt-20260912.json).

The current local UX candidate is exact clean source
`6446b534b3269befaf36a07fb80bde6e0251e745`, provider-free DebugLocal
0.1 (25), product-only arm64 iOS Simulator. Its four-target build completed
`EXIT 0`; the app, build log/result/exit, compact source bundle and verified
source/Info.plist/app-tree/signature receipt are in
[`artifacts/build/mobile-debuglocal-6446b53-20260913/`](../artifacts/build/mobile-debuglocal-6446b53-20260913/README.md).
At build creation no Simulator was booted and no app was installed or launched;
the subsequent focused local journey is recorded below. The separate Structure-
Development scope committed in `2fd0321` was not applied; it belongs only to the
later entitled schema7/structureRevision1 candidate and does not change the
protected Device24/bba baseline.

Root also completed the separate physical-iPhone candidate on that same clean
source: CloudKitDevelopment0.1(26), generic iOS/arm64, build88339 EXIT0. It binds
the prepared96950f6b-50e0-4e2c-9a94-852dc5099446 Structure scope. A separate copy
was signed with the existing valid iOS profile and Apple Development identity;
sign/strict-deep verification92531 EXIT0, exact entitlements/profile/certificate
and unchanged Info.plist checked. Its signed tree is
c11ed4f723c888a10b7e4876361c6c8d0fb3bf227b031700ba0262eb44e75414.
Original/signed apps, log/XCResult and exact receipt are in
[`artifacts/build/mobile-structure-development-6446b53-20260913/`](../artifacts/build/mobile-structure-development-6446b53-20260913/README.md).
This candidate is NOT installed or launched; no payload key or CloudKit call
occurred. Its matching Native Structure app is still unbuilt. Preserve both
Device24/bba and local-UX25; no Mobile build or test is currently running.

The exact DebugLocal25 candidate now passed one normal, non-fixtured visible
Settings/Sync journey on the Ahoi-owned iPhone 17 Pro / iOS 26.5 Simulator
`C645C09E-B284-434B-BC72-508E481ADC02`: **1/1**, zero failures/skips,
45.996 seconds. Normal `arguments: []` launch, candidate identity, Sync opt-in,
`Nur lokal`, disabled Sync keys/action, process relaunch persistence and final
opt-out were visibly exercised. Four XCTest screenshots were exported and
inspected. The original off-screen-switch failures and the narrow frame-bound
runner correction are retained with the successful result in
[`artifacts/e2e/mobile-debuglocal25-normal-sync-20260913/`](../artifacts/e2e/mobile-debuglocal25-normal-sync-20260913/README.md).
The Ahoi simulator returned to Shutdown; BetterConvo `751D…` remained booted
and untouched. This is not CloudKit proof: public `simctl` inventory exposes no
safe iCloud-account readiness result, no Simulator-compatible entitled
CloudKitDevelopment candidate ran, and the signed Build26 app is iphoneos-only.

The optional private-session device-authentication lock is now implemented as
a separate source block. It is included in Build25, but not visibly accepted.
It reuses the existing window shield, installed synchronously from the owning
UIKit scene lifecycle, including above presented sheets. Its window
accessibility list contains only the shield while protected; keyboard browser
actions are guarded and Return can request authentication. System
`.deviceOwnerAuthentication` allows native
biometry/passcode. Cancel/failure stays locked; actual background/disconnect
invalidates the original auth epoch, and an inactive success waits for its
same scene to become active. Private-session generation prevents a late result
unlocking a replacement session. Locking itself never clears tabs or WebKit
storage. The opt-in/defaults are local-only and use the runtime's scoped store;
disabling a locked populated session also requires authentication.
Closing the final private session also dismisses retained private UI drafts;
the native shield stays until that owned sheet dismissal completes.

Sources: `MobilePrivateSessionLock.swift`, the existing `MobilePrivateSceneShield.swift`
and `MobilePrivateLockSettingsSection.swift`. German/English labels and the
Face ID usage description are wired through the generated project. Three
focused state/race tests are authored, not run. Swift syntax, localization/plist,
project-reference checks and the normal product build have run for this block.
The next candidate-bound acceptance needs a representative visible
private-lock/return/cancel/background journey with retained private-session
state, then those focused tests. Actual
snapshot/VoiceOver/keyboard/multi-scene behavior remains unproved.

Apple contracts used: [scene snapshot preparation](https://developer.apple.com/documentation/uikit/preparing-your-ui-to-run-in-the-background)
and [device-owner authentication](https://developer.apple.com/documentation/localauthentication/lapolicy/deviceownerauthentication).
The bounded Home/Reader/Markdown action package is now complete in source. A
saved normal page keeps an explicit Home target independently of current
navigation and can update it locally with Sync OFF; returning uses the selected
saved page only. Reader extracts bounded visible article text from the active
WebKit main document in an isolated content world and presents it without
navigating or replacing that page. Normal and Markdown copy reject non-Web URLs
and remove embedded URL credentials; explicit private copies are device-local
to prevent Universal Clipboard propagation. The normal Browser Actions sheet
shows loading, availability, failure and Home-position states with German and
English strings and stable accessibility identifiers.

Exact source: `CompanionSavedPageHome.swift`, `MobilePageActions.swift`,
`MobileBrowserActionsSheet.swift` and `Resources/Localizable.xcstrings`; the
generated Xcode project registers both new Swift files. It is included in the
successful Build25 candidate but remains **NOT E2E**. The build did not touch
Device24, a Simulator runtime, CloudKit, native Chromium or shared wire scopes.
The next short visible journey on that exact candidate is: open a saved normal
page, navigate deeper, return Home, set the deeper URL as the new Home,
open/close Reader on a suitable article, confirm the unavailable response on a
non-article, and inspect normal plus Markdown clipboard output. Native Peek and
task help remain binding unfinished work; Mobile Split UI remains outside the
required scope.

### Earlier September8 current-work handoff — historical evidence

**The bounded Build17 Simulator journey is finished within the available UI
scope: navigation, Save and restart are evidenced; Unsave remains unproved.**
The own test Simulator is shut down and the coordinator has closed that UI window.
Desktop is no longer held source-only by this test. Do not restart this journey
from the old approval below. Continue the remaining implementation handoff;
any new Simulator UI must resolve the observed cross-project window targeting.

The authoritative implementation handoff and next actions are in
[UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md](UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md).
Ownership and cross-session handoffs are in
[ACTIVE_SYNC_COORDINATION.md](ACTIVE_SYNC_COORDINATION.md).
Do not resume tasks from the historical archive below.

- ADR 0009 requires ONE current format 3 and fresh isolated acceptance stores,
  not complex migration, legacy-reader obligations or permanent mixed writers.
  Existing profiles/data/keys remain protected. ADR 0010's full browser-setup
  restoration and the workspace metadata follow-up remain required; the original
  13 classes are not the final scope ceiling.
- Common source `e2f6711` plus async durable-completion correction `5e74472`
  is handed to Desktop for Native B-D. No generic header/role wait remains.
  Desktop retains Native/Chromium build/install/UI ownership; no My-Mac grant
  is implied by this document or by an absent process.
- Mobile source `4e64c5f` plus external-open correction `bba0b86` produced
  DebugLocal 0.1 (16), arm64 iOS Simulator, product-only build11655 EXIT0.
  The initial compile failure is retained separately. Exact candidate/log/
  signature limits are in
  [the build report](../artifacts/build/mobile-unified3-4e64c5f-20260908/README.md)
  (`9978b2e`). This is not visible E2E, CloudKit or a native cross-client pass.
- The necessary pending-intent correction `7b706a7` is now the built/signed
  DebugLocal 0.1 (17) candidate, evidence `94b1a3f` and
  [its report](../artifacts/build/mobile-shared-intents-7b706a7-20260908/README.md).
  Build16 is preserved. Build17 now has a bounded local navigation/Save/restart
  proof, not complete UI or cross-client Sync acceptance. Evidence is in
  `artifacts/e2e/mobile-shared-intents-7b706a7-20260908/`; the current coordinator
  readback links the exact limits and before/after IDs.
- The user explicitly granted the short Simulator-UI window on September 8,
  07:08 UTC: Mobile runs its visible journey on `bba0b86` build16 or a necessary
  same-scope corrected successor with its exact candidate receipt, while Desktop
  remains source-only. Reuse this approval; no repeat question for that correction.
  See the current authorization in
  `ACTIVE_SYNC_COORDINATION.md`; do not request it again. No My-Mac/native
  desktop/profile/key/Production action is included. The window is now CLOSED
  after Mobile's explicit UI stop and verified device shutdown. The last
  Simulator call unexpectedly targeted MindBodyCompass and is excluded from
  acceptance; its owner was notified. Future UI requires actual window isolation
  or coordination, not another approval of the same Ahoi test. Do not repeat
  successful steps or broaden tests.
  Do not re-run the old 30212/926/SQL steps, v2 promotion suites or Build15
  acceptance merely because they are preserved below.
- Current global/project AGENTS.md apply: total-machine capacity, no fixed
  80%-per-process gate or Ahoi priority; implementation and visible E2E first.

## Historical archive — evidence, not active instructions

All following dates, owners, PIDs, former goals and words such as "current",
"next" or "must" describe their ORIGINAL historical step. They do not override
the current handoff above or reactivate superseded v2/promotion work. Original
results and limits remain intact; a historical pass is not today's acceptance.

### Historical P2 corrections — retained creation evidence, source only

The review of `3964bcb` found that a later synthetic v2 `created_at` clock
could erase the Boolean-only local creation evidence. The correction retains
the original observed HLC in local snapshot metadata, independently of the
replicated field clock and its legacy `createdAt` framing. Old Boolean snapshots
decode compatibly; unknown provenance stays unknown. Promotion and origin
projection use the retained observation, never the later editor. Local-only
evidence changes persist but are excluded from replicated equality/re-enqueue.

The follow-up review of `895daf9` found the second order: an uninformed peer
promotes to v3/Bottom before the locally observed v2 node is promoted here.
V3-result merges now retain existing local evidence even while their replicated
`created_at` clock remains Bottom. No v2 top clock is promoted into wire state,
no second promotion is enabled, and the original creation time stays immutable.

Seven focused regressions are written and referenced by the generated Xcode
project: both merge directions, newer/repeated legacy rewrites, file-store
restart/replay, evidence-only persistence/no echo, old Boolean decoding and
subsequent local edits/promotion, plus peer-first v3/Bottom promotion with and
without an explicit temporary-state change. They are **NOT RUN**. The existing
Xcode project already includes this test file; only source/whitespace review
was performed for the follow-up. No build, simulator, host or CloudKit action.
The previous `895daf9` CPU gate observed FillIt's source-03 bake at
261.3%/174.3%; those are historical samples. This follow-up respects the
Desktop owner's reported active `225df88` guarded build, Session `30212`,
without taking a CPU or runtime slot. No competing intensive phase was started.

Next Swift candidate must include this correction and run
`SharedTabCreationProvenanceTests` with the existing frozen/read/merge suites.
Visible v3 promotion remains technically unavailable while writer/live-tab
activation is off; independent domain tests are permitted under the documented
E2E exception, not a native runtime pass. Build15 Bookmark acceptance is not
repeated. C++/GN/Golden/ADR and the independent Desktop build freeze are unchanged.
Details: `docs/audit-evidence/2026-09-05-mobile-creation-provenance.md`.

### Historical source wave — matching frozen ADR 0008, not built

The owner froze the concrete contract in `09cae9f`. The one canonical fixture
`overlay/chromium/src/ahoi/browser/sync/testdata/shared_tab_wire_v3_contract.json`
was read with SHA-256
`f640a7223c8bcb894625c2fc3041b2c561116c6879333b01ebe3a0b9b72f6777`.
Mobile prepared the matching Swift implementation without starting a build or
test, as requested; the prior `313e351` acceptance below does not cover it.

Prepared: distinct DeviceCapability wire-v2 record/class, UUIDv5 identity,
strict sorted arrays and device-owned clocks, known-Device admission, atomic
capability merge and compatible local persistence. A pure readiness assessment
covers complete bootstrap/local acknowledgments and every non-retired peer;
it is not connected to a writer-enable switch and does not announce capabilities.

V3 targets now carry atomic URL/kind/local-scheme metadata, with empty new-tab
versus unavailable local-only rows distinguished. Local-only wire data cannot
contain original paths/code; a pure logical-row projection retains identity and
can preserve only the origin session's separately supplied local target.
Non-web Presence has a mandatory link and a separate page-consistency validator.
Actual automatic UI materialization remains for the later native/mobile wiring.

Legacy new-field Bottom is recognized semantically as absence, not UUID tie
order. Pure promotion preserves immutable TreeNode creation *time* and defaults
unknown creation *provenance* to Bottom. The original Boolean-only local
creation marker is superseded by the retained-clock correction above, still
never a creator ID or wire field and excluded from replicated equality to
prevent re-enqueue loops. Bookmark creation time remains
the independent positive Int64 native value and its wire-v2 codec is unchanged.

`SharedTabFrozenContractTests` consumes the canonical fixture directly for
capability bytes, promotion/mixed merge, target matrix and gate cases; existing
read tests were updated to the newly frozen target fields. Tests are written,
not run. No global/versioned writer default was raised: encode/upload/live-v3
import and recovery gates remain off. No My-Mac, Chromium, Apple-account,
Production or installed-candidate action was performed for this source wave.

Next: source review, then a separately coordinated candidate build and visible
relevant journey before running frozen-contract and existing regression tests.
Provider-observed initial-fetch completion/announcement acknowledgments and
actual automatic tab UI/native integration still need their runtime proofs;
the pure readiness assessment is not that proof. Do not request ownership again.

### Historical acceptance — Bookmark Mobile package and v3 read preparation

Final source `313e3518c1d6daa8ee4d6a3ae25a01ec99a0d39b`, DebugLocal **0.1 (15)**:
build succeeded; visible Bookmark create/folder/restart/open/delete journey
passed **1/1** on the exact receipt-bound simulator candidate. Then focused
Mobile Core passed **70**, failed **0**, skipped **2** entitlement-dependent
cases (72 discovered); shared Swift package **36/36** and event-contract
repository checks **2/2** passed. Both final Xcode runs exited 0.

The final provider-consent correction is included: direct/rehydrated outgoing
Bookmark records cannot bypass category opt-in; missing consent pauses only
send intent, retaining encrypted data and quarantine. V3 upload remains off.
The secret scan's sole finding was reviewed as a false positive on the
`sortKey.utf8.count <= 1_024` code expression, not a credential literal; the
original red scan is preserved without global suppression.

Report and candidate receipt:
`docs/audit-evidence/2026-09-05-mobile-bookmark-wire2/README.md`.
Canonical raw results:
`artifacts/e2e/mobile-bookmark-wire2-313e351/`.
Reusable clean source/DerivedData:
`/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/`, source at `313e351`.
No own build/test is active; the dedicated test simulator is shut down.
No Desktop/My-Mac runtime reservation exists and TestFlight is unchanged.

Next integration is genuinely separate: matching native Chromium Bookmark
adapter/test execution and candidate-bound cross-client roundtrip; then shared
normal-tab live projection/wiring and activation after the capability, legacy
promotion and nonportable-target freeze. Swift ownership is already granted.
Do not rerun unchanged local Bookmark acceptance or resume old ACK loops.
None of this marks Production sync, cross-device Keychain or all shared-tab
runtime behavior complete. The older registered Internal-Beta goal remains
complete for its narrower scope only.

### Historical resumed implementation — versioned shared-tab reads, writers off

The explicit renewed ADR 0008 grant removes any remaining Swift ownership wait.
Mobile has now added optional `RemoteTab.treeNodeID` to compatible local Codable
state, portable v3 TreeNode/Presence readers with exact versioned field maps,
and read-merge preservation of actual v3 new-field clocks against later v2
records in both directions. Legacy new fields remain absent clocks with
persistent/unlinked read defaults; no promotion sentinel or origin badge is
fabricated. A deterministic Inbox value is defined but not auto-persisted or
used to reorder existing user workspaces.

This is preparation, not activation: v3 encoders/uploads/physical-delete
recovery are gated off, while the live Bridge retains unsupported v3 input in
the existing encrypted quarantine until capability/promotion are agreed.
Bookmark-v2 is unchanged. Concrete Capability12 fields, legacy promotion and
nonportable Presence semantics remain future activation gates, not an ownership
block. The scoped Bookmark cleanup locator now targets the observed confirmation
sheet's single rendered action rather than requiring one AX node.

Candidate `da7315d`, DebugLocal `0.1 (14)`, built successfully and the visible
Bookmark create/folder/restart/open/delete journey passed 1/1. Its exact receipt
was reverified. Then 30/30 focused Bookmark wire/domain/encrypted relay,
SharedTab read/merge and local-operation tests passed with no skips/failures.
The canonical Bookmark Golden SHA is the approved `b09a5f89...244e443`.

A final targeted real-provider audit found a Bridge-only consent gap: after
losing CKSyncEngine state, cached Bookmark records could be scheduled for resend
by the provider's existing rehydration path. A shared runtime consent guard is
now integrated into real/DEBUG transport writes, seeds, merge results and final
upload/recovery authorization. Unapproved resend intents are paused/removed
without deleting the encrypted data or clearing corruption quarantine; explicit
approval permits later rehydration. This correction was accepted on `313e351`
with the repeated visible journey and focused regression tests recorded above.
No My-Mac/Production or Chromium action is authorized or planned by this wave.

### Historical single runtime attempt — window returned

A renewed explicit Desktop handoff authorized one My-Mac CloudKit attempt.
The existing verified inert `9395a9c` CloudKitDevelopment host `0.1 (11)` was
used without rebuild/signing/profile/key changes. `xcodebuild` PID 88055 /
session 84564 exited 65 before executing the named domain test: the invocation
omitted `variant`, so Xcode chose native macOS instead of Designed for iPad/iPhone
and LaunchServices returned `kLSExecutableIncorrectFormat` / -10661. This is an
agent invocation error, not a CloudKit/product/provisioning verdict.

No retry was started. End readback found no host/runner/xctest or own build
remaining; host/test/installed-Desktop executable hashes were unchanged. No
test-zone/store code ran and no server cleanup was necessary for this attempt.
The runtime was explicitly returned to Desktop; a correct variant-specific
attempt requires a fresh window. Report:
`docs/audit-evidence/2026-09-05-mobile-mymac-cloudkit/README.md`.

Historical Bookmark attempt: source `4647f5b` builds as DebugLocal
`0.1 (13)`; visible create/restart/open ran, but the overall Bookmark UI test is
red at the duplicate AX match for the cleanup delete confirmation. The scoped
locator was subsequently corrected and the complete journey passed on Build 14
and final Build 15 as recorded above. Keep that initial failed run red.

### Historical task: matching Mobile bookmarks — owner handoff received

The Bookmark owner explicitly handed off the Swift/Mobile implementation of
the frozen wire-v2 contract in ADR 0006. Mobile owns typed Swift bookmarks,
compatible snapshot persistence, field merge, bridge integration, Mobile
Library/search UI and tests, including necessary `spikes/cloudkit` model and
boundary seams. Native Desktop adapter/Core/GN, C++ schema/merge/serialization,
`config/sync-policy.json` and the one canonical Golden fixture remain with the
Bookmark owner. No new engine/crypto/container or Production rollout is allowed.

Source integration now includes typed Bookmark records/codec, compatible
snapshot decode, a separate field merge/store/hierarchy, existing Bridge
seeding/import/recovery hooks and a native Library with CRUD, roots/folders,
safe activation, lookup/search and browser Add Bookmark action. One-time local
category approval is explicit and still requires configured global Sync;
before approval the Bridge neither seeds local bookmarks nor decrypts imported
bookmark payloads. Cached opaque records hydrate on approval, without a new
engine or erasing prior corruption quarantine. The large Bridge snapshot-seed
method and repository clock observation were moved into cohesive small files.

The helper delivered model/codec, UI and focused tests in disjoint modules;
the main agent completed integration and the bounded verification above.
The owner corrected the Golden's device aliases to UUIDs and committed the one
canonical fixture at `ae38ed7`; `project.yml` references it as a test resource.
Bookmark creation time retains positive Int64 Windows-epoch microseconds,
including pre-1970 native metadata. New tests cover wire negatives, legacy
decode, hierarchy/CRUD/merge, privacy consent and encrypted two-repository relay;
`MobileBookmarkRealE2EUITests` exercises the real Library before those suites.

Desktop granted My-Mac runtime slot `01a070c7-7b59-78d0-b224-bfab8a57b998`, but
Mobile immediately returned it unused in `01a070ef-f293-7c53-971d-411deac9db53`.
No host was started/installed and no account/zone/key was touched. Do not reserve
or block Desktop while this separate source wave is being prepared. Recheck
CPU/build ownership before the first matching Mobile candidate build.

The completed local SavedPage foundation below remains separate evidence.
ADR 0008 now explicitly assigns future shared-tab Swift ownership and stable
Inbox identities. Its capability/bootstrap, legacy-clock and nonportable-tab
freeze points remain pending agreement; this Bookmark wave does not enable v3.

### Historical shared normal tabs foundation — previous bounded step

The user now explicitly requires all normal tabs to stay synchronized across
Desktop, iPhone and iPad. Saved pages remain one uniform structure; temporary
tabs use a subtle origin icon, optionally with tint. A Mobile-added/saved badge
is optional and must not create separate saved-page collections. Private tabs,
cookies and website sessions remain local. See the accepted behavior and pending
cross-client wire contract in `docs/decisions/0007-shared-normal-tabs.md`.

Mobile has implemented the local identity foundation: optional `treeNodeID`
in `MobileTabRecord`, unique normal-only binding/lazy reference/activation in
`MobileBrowserControllerSharedTabs`, and identity-safe Sidebar, Library tree,
Library search, Address search and Focus Voyage callbacks. Saving the active
page uses a per-tab in-flight guard and the initiating runtime ID, even if the
user selects/closes another tab during the async save. A linked saved page is
updated/moved atomically without allocating a replacement identity.

Source `30d73fa`, DebugLocal `0.1 (12)`, now built successfully from a clean
snapshot. The exact-candidate visible Library save/tree/search journey passed
**1/1**, including unchanged runtime Tab-ID sets on reopening. Then
`MobileTabReorderTests`, `MobileSharedPageSaveTests` and
`CompanionOperationFailureTests` passed **11/11**; the provider-event/no-polling
repository contract passed **2/2**. The Save callback binds before exposing the
new row and also updates the initiating tab's matching Undo record.
Report: `docs/audit-evidence/2026-09-05-mobile-shared-tab-identity/README.md`.
No Mobile build/test remains running; the dedicated fixture simulator was
returned to its previous shutdown state. TestFlight Build 10 is unchanged.

Shared temporary publication,
auto-projection on remote arrival, provenance badges and authoritative
TabSwitcher Save/Unsave still require the coordinated wire step. The earlier
internal-beta and encrypted relay results do not accept this new behavior.

The Bookmark owner is actively editing common C++ sync model, serialization,
merge, store/schema and policy files for ADR 0006. Do not edit/stage those files
or change the shared Swift wire schema without the combined field/version
handoff. Desktop owns native tree/session/UI integration and its build/install
lease. Local Mobile implementation may proceed in disjoint files on the same
branch. Queued field-freeze requests: Desktop
`01a07071-5aa1-7e41-86bc-db14e0f24e68`, Bookmark
`01a07071-5750-7d70-99ca-7628947f2329`; no response is assumed.

Next: obtain the combined ADR 0006/0007 field/version handoff, then implement
the shared Swift wire/domain and Mobile live projection together with the
Desktop owner. Do not repeat the completed local identity tests unchanged or
mistake this foundation for complete all-device tab sync. Preserve pending
signed real-CloudKit runner outputs below while working on the new package.

### Historical request: close technical sync verification

The user explicitly requested real testing or meaningful simulation beyond the
completed internal-beta scope. That verification wave changed tests only;
the newer shared-tab work above now changes Mobile product code. Test commits
`270a526` and `9395a9c` add
`testRealContainerTwoLogicalDevicesMergePagesTabsAndDeletion` to the existing
signed CloudKit E2E target. It compiled and signed successfully on `9395a9c`,
CloudKitDevelopment `0.1 (11)`. It has not executed against CloudKit yet.

The test uses two separate file-backed domain/record stores, real
`CompanionSyncBridge`/`CloudKitSyncProvider` instances and a fresh guarded
Development zone. It covers pages/tabs in both directions, offline field merge,
durable local readback, deletion without resurrection, server encryption and
private-record rejection. The Mac identity is simulated inside the iPhone
host, with a run-local shared AES key; it does not prove Chromium execution or
cross-device Keychain bootstrap.

The independent two-repository simulation on `8f98cc1` passed **1/1**, zero
failures/skips, on iPhone 17 Pro Max/iOS 26.5 Simulator. It uses real repository,
bridge, wire/CloudKit-record codec, AES-GCM and field-merge code; only delivery
uses the existing DEBUG transport. It proves bidirectional pages/tabs, disjoint
offline edits, delayed-live-record rejection after delete, no duplicate tabs,
private-record rejection and reopening persisted state. It is not a run of
the Chromium client, real server, Production environment or shared Keychain.

Evidence is retained under `artifacts/e2e/mobile-sync-assurance-8f98cc1/` and
summarized in `docs/audit-evidence/2026-09-05-mobile-sync-assurance/README.md`.

Next: execute only the built real CloudKit domain test on `Servusla` once the
device is available and unlocked. Reuse
`/private/tmp/ahoi-mobile-21de889-cloudkit-real-e2e-derived`. The prepared fresh
run token is `9C8AAE2C-E9C0-435F-B48F-7141E60FD038`; it has not been sent to
CloudKit. Use a new token after any actual mutation attempt.

Current gates: last device readback at 08:38 CEST reported
`passcodeRequired=true`; mirroring subsequently reported `iPhone not found`.
The user has been asked to unlock it. Both owned Xcode runs ended successfully;
no Mobile build/test remains running. Recheck cross-project CPU before another
test. Desktop owner was queued messages
`01a07025-66b7-7c21-a818-bdfa6771efb7` and
`01a07030-09b3-7a62-adbb-5445a2811770` about a real Mac candidate and an
incremental `ahoi_sync_unittests` target. Message
`01a07046-e601-7722-bf5d-e1fe59a42816` asks for a short runtime handoff before
using the iOS test host on My Mac, because it shares the native browser's
bundle ID. No handoff is assumed. The C++ sync test binary remains absent.

The completed release scope was the **Internal Beta Ready** definition in
`outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`. That release-first
contract supersedes the former full-matrix execution order. Do not restart the
old download, performance, iPad or uBlock feature waves.

### Historical ownership

- Work only in `apps/AhoiMobile`, Mobile-only fixtures/tests, Mobile outputs,
  and this checkpoint.
- Do not stage or edit the active Desktop Arc, AnyChat, or uBlock files.
- Desktop and bookmarks work concurrently on the same branch. Their latest
  checkpoints explicitly leave Mobile source and this checkpoint to this thread.
- Shared registries remain untouched; the internal-beta evidence does not
  silently mark the full public-release matrix green.
- uBlock on Mobile remains a documented feasibility boundary, not a beta gate.

### Historical exact boundary

- Branch: `codex/desktop-core-feature-wave-20260830`
- Mobile source: `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`, version `0.1 (10)`.
- Build 10 is processed and distributed to the existing internal TestFlight
  group. App Store Connect app `6808754773`, build
  `84cf0b2e-c1b9-4ff7-b104-8d99cce8ae9f`.
- Disposable build outputs remain under
  `/private/tmp/ahoi-mobile-ab2e709-e2e.1QKjtT/` (`DerivedData`,
  `candidate-receipt.json`). The clean `repo` worktree was retired after its
  source-equivalence and XcodeGen checks; its full source is retained at
  `ab2e709` in the canonical history. No replacement was needed for that beta
  closure; the subsequent shared-tab source wave needs its own candidate.
- Dedicated simulator: `41FF3C64-92FF-44D9-9C32-29F9B9D0D9B0`,
  `AhoiMobile Build10 E2E Fresh`, iPhone 17 Pro Max / iOS 26.5.
- Exact test runner:
  `DerivedData/Build/Products/AhoiMobile_iphonesimulator26.5-arm64-e2e-fresh.xctestrun`
  below the existing build directory.
- Installed app was rechecked against the receipt: app-tree hash
  `ef0d813072dd3b772f3ea2d8c68f1272baf1d3842a161f72d07a3e54e2d1bdc5`;
  executable receipt-domain hash
  `7912f7d27bfe4f158a7c6ef6102be9d3210690fabd37873ac1a4251e703a09b9`.
  These are the domain-prefixed hashes from `mobile_evidence_artifacts`, not
  plain `shasum` values. The simulator app is DebugLocal, not the signed IPA.
- At the internal-beta closure, product and CloudKit source were unchanged since `ab2e709`.
  `afa5cb6..ab2e709` changes only generated project version settings, so the
  real Development CloudKit pass on signed build 9 remains applicable to that
  same sync implementation; it does not prove Production transport.
- Device readback at 2026-09-05 07:15 CEST still finds `Servusla` on developer
  build `0.1 (9)` (`builtByDeveloper=true`). Official TestFlight `4.3.1 (681.1)`
  is now installed; its initial terms approval and physical Build 10 install
  remain external gates. Mirroring currently reports the phone is in use.
  The iPad (6th generation) is below the required OS version.

### Historical bounded acceptance

- `e2e-harbor-collapse-build10-astra.xcresult`: 1/1 passed on the exact
  simulator candidate; deliberate document scroll collapses the deck and
  reverse scroll restores it.
- `e2e-cold-launch-build10-astra.xcresult`: failed because synthetic address
  entry lost characters and its chunked retry lost the editor. Keep this result
  red. Direct Computer Use reproduced loss under rapid synthetic key injection;
  individually delivered keys with intervening observations entered the exact
  address, and normal `https://example.com/` plus IANA navigation loaded visibly.
- Current manual Golden Smoke has confirmed launch, HTTPS/visible lock and
  origin, second-page navigation, Back, Reload, an additional normal tab, a
  private tab without normal suggestions, and separate normal/private tab lists.
- Screenshots are retained under
  `docs/audit-evidence/2026-09-04-mobile-testflight-fix/golden-*.png`.
- The download overview and exact-candidate sync-status UI test are now green;
  `golden-sync-status.xcresult` passed 1/1 including restart and fail-closed
  local-only behavior.
- After the visible smoke, `internal-beta-core.xcresult` passed 213 tests with
  two expected entitlement skips; `internal-beta-analyze.log` ends with
  `ANALYZE SUCCEEDED`.
- The repository suite passed 38/44. Five receipt-fixture failures came from
  macOS's `/var` symlink; the root is now canonicalized in the fixture. One
  stale sync test expected the old zero-argument call; it now checks the
  bounded durable read model. Production source remains unchanged. The focused
  rerun passed **7/7** in 1.026 seconds, covering all six original failures and
  one already-passing test. The initial red run is retained, not relabelled.
- XcodeGen 2.46.0 generated the exact source project without a diff; its hash
  remains `bd7c59414bfa66ef5804c7e3de95535a71dd304daef5c7a8ef45b51c725af016`.
  Redacted Mobile-source, CloudKit-package and evidence scans found no leaks.
  The unchanged CloudKit package reuses its verified 36/36 result.
- Raw results/logs and the signed IPA have been copied to canonical, ignored
  `artifacts/e2e/0.1-10-ab2e709-internal-beta/` for durable local inspection.
- The user locked `Servusla`; mirroring now connects and the official TestFlight
  app has been installed. Its first-launch terms await explicit user approval
  requested through the asynchronous question. Do not accept them without the
  answer. Build 10 itself is not yet installed.
- Follow-up readback at 07:15 CEST still finds developer Build 9. Mirroring
  reports that the iPhone was used and disconnected; reconnect only when the
  device is available. No approval of the TestFlight terms has been received.
- Lightweight final readbacks passed: both receipt JSON files parse, owned
  diffs have no whitespace errors, and all Mobile Swift sources/tests remain
  at or below 800 lines. The shared branch's latest Desktop commits are outside
  Mobile ownership; the index was empty at the latest check.

### Historical internal-beta handoff

The earlier CPU gate cleared at 07:13 CEST; the remaining targeted checks then
passed. No Mobile build or test remains running. The closure changes consist
only of the two Mobile repository test corrections and Mobile acceptance
documentation/screenshots. The closing DCO-signed Mobile commit on the shared
branch is limited to these paths; Desktop acceptance remains owned by its thread.

The local acceptance is complete. Do not reopen the full matrix or create a
successor build without beta feedback or a new release-blocking finding.

Remaining external work is explicit and does not block this internal-beta
scope: user approval of TestFlight's first-launch terms, physical Build 10
install and short HTTPS smoke; a compatible physical iPad; Production CloudKit
and a Mac/iPhone/iPad domain roundtrip; public TestFlight review/link;
default-browser grant and post-grant build; public Store release approval.
The receipt and report are in
`docs/audit-evidence/2026-09-04-mobile-testflight-fix/`.

### Historical bookmark-owner coordination — 2026-09-05

- No additional Mobile bookmark source has been implemented. Library/Saved
  Pages use `model.snapshot.visibleTreeNodes`, scoped to the Ahoi workspace,
  and the existing local search index. There is no shared Chromium BookmarkModel
  collection yet. The user has now requested that separate collection; ADR 0006
  freezes its bookmark fields. Implementation remains coordinated with its owner.
- A historical real Library UI journey is available:
  `testSavePageToWorkspaceThenOpenFromTreeAndLibrarySearch` passed in 162.347s
  on source `640516791522e7604d5619bee354420f2cc56b7e`, DebugLocal `0.1 (1)`,
  iPhone 17 Pro / iOS 26.5 simulator. Its surrounding result bundle passed 5/5.
  The test creates a workspace, saves a real fixture page, reopens it through
  the tree, then finds/reopens it through Library search. Result, log and both
  candidate receipts are retained in `artifacts/e2e/mobile-library-6405167/`.
  This is historical Saved Pages evidence, not a Build 10 or cross-platform
  Chromium-bookmark pass. No new E2E or CPU-intensive phase was started.
- The Desktop combined Ninja build was observed under `out/AhoiDev`; its owner
  retains checkout/build authority. Mobile starts no competing build or tests.
- Normal cleanup removed only five unused ModuleCache/Index cache directories
  from the owned `ab2e709` simulator DerivedData, `ab2e709` release DerivedData,
  and `afa5cb6` focused-test DerivedData: 338408 KiB (about 330 MiB). No process
  had an open file in those roots. Products, archives, IPA, logs, receipts and
  all XCResults remain. Deleted caches can be regenerated by Xcode.
  Latest disk readback is about 97 GiB free, still below the 120-GiB roll floor;
  the entire free-space increase is not attributed to this small cleanup.
