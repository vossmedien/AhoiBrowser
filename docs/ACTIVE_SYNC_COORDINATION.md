# Active sync coordination

## Current handoff — 22 September 2026

Mobile product correction `45330d2` / DebugLocal39 is installed and visibly
passed the user's WinFuture URL-warning journey and neutral “Tabs” label on
dedicated iOS27 A168. Direct `javascript:` input remains blocked; one focused
policy XCTest passed after excluding two unrelated stale SharedTab test sources.
Exact source, screenshots, candidate and limits are in
`artifacts/e2e/mobile-url-policy-20260922/README.md`. This is provider-free and
does not replace the earlier Mobile38 CloudKit first-use proof. Matching
`CloudKitDevelopment` build40 from the same source/fe842 is now installed on
C645/iOS27: initial CloudKit fetch9/notAuthenticated was red, then the user's
native Apple Account confirmation plus normal Ahoi restart yielded visible
Ready/encryption-ready/Synchronized and a successful manual Sync action.
Evidence/cleanup: `artifacts/e2e/mobile-cloudkit40-ios27-20260922/README.md`.
This is Mobile provider-status evidence, not Mac/mobile record transport.

M153 source fixes `252617b`/`752c8c6` were applied through the guarded
overlay refresh. Root's incremental build60308 is **terminal EXIT2** at
500/3,860: four newly exposed Sidebar API causes, not an installable candidate.
Root stopped only the identified Ninja scheduler after confirmed machine
pressure; objects and logs remain and workaround sources were restored.
Sidebar source84a3405 is committed. Its cached build28891 is terminal EXIT2
after finding the remaining BookmarkMenu forward-declaration cause; source
317df37 closes it. Root refreshed the guarded overlay (3113 EXIT0); cached
build15002 is terminal EXIT2 at898/3,185 after independent BrowserView
incomplete-type and removed `is_type_normal()` causes appeared. Sourceea57e08
addresses these plus matching sites in base patch0001. Guarded overlay25795
EXIT0; app-only build34638 is **terminal EXIT2** at30/2,289 because removed
`is_type_normal()` survived in later patch0028. Root stopped only Ninja56176
under fresh resource pressure; objects/logs and clean workaround sources
remain. Sourceba7a062 corrects exact patch0028, with full43-patch apply check.
Root's guarded overlay82650 EXIT0, and cached app-only build65612 now RUNNING
with two jobs from cleanba7a062 in same out/AhoiDev. No install/pass yet.
First attempt0a1de6a remains separate terminal EXIT1. Desktop current details
and exact next gates are in `ACTIVE_DESKTOP_CHECKPOINT.md`. After a real Mac candidate,
Development-sign the existing isolated scope and perform normal visible
Mac/mobile Sync roundtrip. No overall Sync pass or goal completion yet.

Updated: 2026-09-20. Root coordinates the same canonical branch,
`codex/desktop-core-feature-wave-20260830`. The user explicitly confirmed both
former Terminal agents stopped and authorized restarting the two workstreams.
Their old thread IDs and September8 leases are not current runtime grants.

## Current executable state — September20

- **Priority:** Apple-first macOS/iOS and real CloudKit acceptance. Windows/Android
  are later, demand-dependent work; no new backend now. Use Simulator, not a
  physical-iPhone/unlock request. The user signed in on own C645.
- **New user direction, September20:** prepare the Chromium153 update now rather
  than finish a long M152 candidate followed immediately by another major build.
  The user stopped Docker; Root verified its VM/backend PIDs absent. Official
  discovery proves fully rolled153.0.8010.53, commit792bf6722e73a45aa9e47c163b9901bdc17f3230.
  Reviewed non-production candidate binding is updated through chromium_roll;
  config/chromium.json now selects this exact target in058ea5a for acquisition;
  shared checkout HEAD now equals that M153 target after guarded dependency
  continuation34124; installed app and all Sync scopes remain unchanged.
  No build until the patch and overlay handoffs form a coherent source candidate.
  **Current implementation handoff:** toolchain95449bb is source-ready; the same
  helper now owns canonical Ahoi-overlay consumers of removed Browser APIs,
  with no shared-checkout/build writes. Patch worker
  `/root/mobile_digest_regression_20260920` owns the43-patch sequential rebase in
  `/private/tmp/ahoi-m153-patch-rebase.aMruqC/chromium`. Initial0001 three-way merge
  resolved251 files automatically and exposed38 real conflicts; those are now
  resolved and0001/0002 committed in the isolated chain. Root ported the native
  Browser/Popup/Glass/Startup seams to the M153 owners rather than restoring old
  APIs. Canonical overlayb7d6156 removes the obsolete PrivacySandbox delegate:
  M153's native implementation directly denies the affected APIs. Worker rebuilds
  the final synthetic overlay parent from current overlay before exact preflight.
  This is source preparation, not a build/runtime pass. Diskupdatef0af52a follows
  explicit user authorization below64 GiB with32 GiB absolute reserve; first
  checkout limits remain unchanged. No current disk/ownership ACK blocker.
  **Actual acquisition:** old overlay restore75447 reversed the source delta but
  its final tree check rejected the separately fetched Sparkle prebuilt after the
  overlay ignore rule disappeared. Root verified Sparkle's existing material
  receipt, moved it recoverably to the preflight artifact's retained-sparkle/
  directory and independently proved exact clean M152 base tree
  4a41dac5a4e1a870dc8fa983df52d75cd4efb4ce. Old overlay/hook states are archived,
  not silently reused. Source inventory70631 EXIT2 is an intentional dry-run:
  431,295 present/34,120 missing target blobs, no state mutation. Full resumable
  hydration73969 used128-object batches, but its first request timed out; Root
  interrupted only the identified parent/request group, preserving report130
  and verified unchanged protected state (34,120 still missing). A bounded
  single-object Git fetch54449 then succeeded. Changed-prerequisite continuation
  61468 uses16-object batches, one attempt/45s timeout, adaptive splitting and
  per-batch progress in `artifacts/build/chromium-m153-preflight-20260920/full-source-hydration-small-batches.json`.
  **That continuation is now terminal130, not live:** it also failed to hydrate
  the first batch. Root terminated only the identified leftover Git child group
  after its parent exited. Exact missing object002c1a318a5209e928c566f5243654127ea34bd8
  (M153 ios_strings_hu.xtb) timed out through Git in both stdin and argv forms,
  while official Gitiles returned it promptly with matching Git object hash.
  The bounded transport handoff f07064e is now committed and Main-reviewed.
  It reuses the EXISTING Gitiles URL/response/hash helpers, leaves Git default
  unchanged and preserves metadata/resume guards. Real CLI sample17185 fetched
  all8 requested missing objects, zero failures,29,761 decoded bytes; EXIT2 is
  the deliberate request-budget stop with34,097 objects still missing. Final
  guard proves HEAD/index/worktree/refs/FETCH_HEAD/shallow unchanged. Evidence:
  `artifacts/build/chromium-m153-preflight-20260920/gitiles-cli-sample.json`.
  Acquisition99103 is now deliberately terminal130: it worked, but individual
  requests were too slow for34k files. Verified progress remains;33,705 missing,
  final protected-state guard unchanged. Report: `gitiles-acquisition-01.json`.
  Changed method: the standard Git `--refetch --no-filter --depth=1` fetches ONE
  complete pinned snapshot pack, not Chromium history. This knowingly transfers
  some existing objects again rather than spending hours on single requests.
  Wrapper31124 / Git73579 is TERMINAL EXIT0:508,808 objects/1.33GiB transferred
  and deltas resolved in341.9s. HEAD/index/worktree/refs/FETCH_HEAD/shallow and
  Git config are byte-identical before/after. Standard Git replaced hours of
  per-file requests without a new acquisition framework or a second checkout.
  Exact wrapper/log/result: `fetch-snapshot-pack.py`,
  `snapshot-pack.log`, `snapshot-pack-result.json` in the preflight directory.
  It bounds runtime1800s and disk32GiB; before/after protected-state and config
  hashes match. Full no-network inventory91465 EXIT0 proves all465,415 unique
  target blobs present, zero missing, protected state unchanged. Evidence:
  `snapshot-pack-inventory.json`. Guarded fetch40991 stopped before checkout
  (EXIT1): M153 vendors former `third_party/aria-practices/src` gitlink as normal
  tracked files. Exact all-gitlinks comparison found this as the sole collision.
  Old dependency was clean at its pinned7b134ce6d19497cce8a67db4a9f59980baf853dc;
  Root moved it intact to `retained-aria-practices-m152/` in the preflight evidence
  and left an empty uninitialized gitlink directory. Superproject remains clean;
  nothing deleted/forced. Corrected normal fetch now34124, existing clean59a0bf5
  snapshot, canonical AHOI_WORK_ROOT,2 workers; log `dependency-sync-aria-preserved.log`.
  Fresh gate57% aggregate idle, no competing compiler and129GiB free.
  Root verified checkout HEAD792bf6722e73a45aa9e47c163b9901bdc17f3230.
  Dependency sync34124 is now TERMINAL EXIT0. Exact M153 checkout and dependency
  closure were verified at10:09:36 UTC; both normal receipts are retained in the
  preflight directory. Gclient's26:37 "STALL DETECTED" was merely its silence
  heuristic: Root verified V8's pack still growing and correctly did not restart.
  Clean hooks completed at10:12:09 UTC with normal final verification line and
  exact clean hook receipt. Tool handle5294 was lost across the user's interrupt;
  completion is established by the actual log/state and overlay's fresh checks,
  not an invented observed exit. Overlay34816 then explicitly EXIT0, atomic delta
  4df029d6cefc8c81351e01a53c88736a1cf42d4b286420277c3d43c53cd84b49.
  Both receipts retained. Verified Sparkle prebuilt APFS-copied back only after
  overlay restored its native directory/ignore rules; original retained intact.
  Product attempt84666 is TERMINAL EXIT128 before GN/compiler: the rebased Rust
  dependency patch's final hunk declared11/12 lines but contained10/11. Root fixed
  only that count and its pinned SHA in0a1de6a. Real `git apply --check` against
  the current original M153 wrapper PASS; original target hash unchanged and V8
  clean. Original log/exit kept in `artifacts/build/native-m153-59a0bf5-20260920/`.
  **Current corrected build31816 is RUNNING**, clean0a1de6a in the SAME snapshot
  and out/AhoiDev, six jobs/keep-going/chrome only. No product overlay/Chromium
  patches/GN-args changed, so no new overlay or dependency acquisition required.
  Log/exit/receipt under `artifacts/build/native-m153-0a1de6a-20260920/`.
  Fresh68–77% aggregate idle/66% memory headroom/0 swapouts, no competing compiler.
  Normal guarded hooks/GN/compile/stage/sign path; no successful build, installed
  update or Sync roundtrip is claimed yet.
  GN now succeeded (32,713 targets/4,982 files); actual Ninja53051/parent53015
  is compiling with6 jobs. At the latest readback4,297/56,804 actions completed,
  no FAILED/error diagnostics yet. Do not restart this live continuation.
  Root's existing clean build snapshot is advanced to59a0bf5, no compiler yet.
  Retained Sparkle2.9.6 revalidated against its old material receipt; reuse after
  overlay instead of another download. The helper's accidental dry-run network
  contact transferred no pack and left metadata unchanged; it is not acceptance.
  No more unchanged Git retries; no new transport backend for product Sync.
  It does not switch HEAD or apply Ahoi patches. A wrong-shell prelaunch attempt
  failed before hydration and was corrected to bash; no checkout operation ran.
  All43 rebased patches are now complete in the worker's isolated chain; final
  regeneration against canonical overlay9d0ea12+db04ea6+b7d6156 is COMPLETE.
  Canonical29dfe7a contains the43 ordered M153 patches, renamed basis patch,
  resource-test path bindings and README status. Final preflight ready=true:
  43 applies/0 conflicts/0 already-upstream, exact result tree
  a83de08b4ed29927e7cd9e5093876a42be03c459, shared checkout mutation guard unchanged.
  Independent fresh index matched that tree; Root read the actual report and
  diff-check. Evidence is final-patch-preflight.json in the preflight directory.
  Source-only, no M153 compile/pass. Next gate is verified source/dependency
  acquisition; no more patch-design/ownership wait or M152 build restart.
  **Merge efficiency direction:** prefer current Chromium owners and reusable
  existing helpers over old compatibility shims. Remove obsolete code when the
  native implementation demonstrably covers its contract; preserve Popup
  Browser-owned lifecycle and all privacy/permission invariants. Do not expand
  this instruction into a separate architecture rewrite or review phase.
- **Root:** sole Native/Common integration, shared checkout/out, build/sign/install.
  Frozen clean sourceb23c7d9 in `/private/tmp/ahoi-native-unified.1FqoG5/repo`.
  M152 continuation81284 is now deliberately TERMINAL EXIT2 at4,656/20,804 after
  Root interrupted exactly verified Ninja43443. Both temporary patches restored
  to pinned original SHA256; all objects/logs/old candidates preserved. DO NOT
  resume this old build as an automatic next action. Evidence:
  `artifacts/build/native-unified-b23c7d9-20260920-resume/`.
  Previous64423/97055 is gone after interruption at11,177/31,981, without a final
  exit or successful receipt. Root verified no surviving Ahoi build, recovered
  only the two pinned temporary dependency patches from SHA-matching original
  backups, preserving mtimes and objects, then invoked the normal guarded script.
  Do not resume/sign/pass old64423/97055 or earlier handles.
- **Mobile acceptance:** product-only38/24898 EXIT0, exactf21d089, existingfe842
  scope/C645. Normal visible opt-in reached Ready, encryption ready, Synchronized;
  manual Sync also returned synchronized. Root independently read candidate.json
  and viewed/hash-checked screenshotcfcce85148d4db0501189f27c722de060adb93dfde7d2d23fe30f6a39bb1f5c7.
  Evidence: `artifacts/e2e/mobile-cloudkitdevelopment38-xcode27-20260919/`.
  Normal OFF/app termination/own Shutdown followed. This proves mobile startup/
  provider status, not matching Mac roundtrip, physical iPhone, push or Production.
  **Fresh runtime correction,20September:** user queried the removed26.5 runtime.
  Live simctl initially listed ONLY iOS27 available, with C645 still bound to
  unavailable26.5. Native clone refused (401, missing old runtime); Root then
  used Apple's supported `simctl upgrade C645… com.apple.CoreSimulator.SimRuntime.iOS-27-0`,
  EXIT0. The SAME C645 is now available on iOS27, still Shutdown, no erase/reset
  or manual key/data copy. Installed38/f21d089 remains byte-identical to its
  original receipt: tree0b70e757…/binary85421eec…/plist50f9975d…, verified with
  the existing `mobile_evidence_artifacts.sha256_path` (typed hashes, not raw
  shasum). No reinstall needed. Account/session usability and visible startup
  on27 remain untested; do not relabel the old26.5 pass. Next mobile boot uses
  this upgraded C645, not a new empty device. This is separate from Native builds:
  Xcode26.5 still exists and provides macOS SDK26.5, M153's exact
  `mac_sdk_official_version`; installed Xcode27 provides macOS SDK27. Do not
  conflate removal of a Simulator runtime with removal of the Mac build toolchain.
- **Bounded regression:** helper finished its one existing test-file handback.
  Root aligned the test with AppEntry's MainActor caller and committed33bddb4.
  Both concrete digest calls with authorization=false must throw authorizationRevoked
  before Security access, not return nil. Execution now PASS: one selected test,
  zero failures/skips,0.018s, xcodebuild EXIT0. Root independently read actual log.
  Evidence: `artifacts/tests/mobile-digest-regression-20260920/README.md`.
  The scheme rebuilt extra artifacts because of snapshot-path invalidation,
  but only one Core test ran; two known stale files remain explicitly excluded.
  No Keychain/cloud/UI action. Current Native scheduler43443/43419 was briefly
  stopped for the test and then revalidated/CONT→R; handle81284 is unchanged.
- **Actual Mobile37 diagnosis:** unchanged installed candidate hit the protocol-
  extension nil default at CompanionKeyLifecycle.swift186, immediately called by
  concrete keyStore at AppEntry.swift344. The coordinator's existential path had
  used the real actor witness and reached Ready. This nil was not a Keychain
  read or missing/deleted-key proof. Optional remote signer was not reached.
  Debugger detached, normal OFF and C645 Shutdown completed. Primary-source
  research and rejected hypotheses are in `docs/KEY_BOOTSTRAP_SYNC.md`.
- **Preserve scopes:** old bba;969 (Build35 correctly reports unverified ownership);
  and new37b55dda-30d6-4664-a91c-c91cab92d14b (Build36 pending first-use). Never
  delete/copy/inject/promote their keys, journal, records or receipts to pass a test.
  New current tuplefe842784-4865-4272-8bda-4bcf81a64a84 is prepared separately in
  `artifacts/e2e/shared-sync-direct-bootstrap-scope-20260919.json`, SHA
  d1605b4622269f1d374440c1fc45e3c4701d209f84d7c675d60d125cbe6b1364.
  The previous37b manifest and its immutable hash remain linked in that file.
- **Next:** bounded M153 patch/toolchain compatibility and safe disk preparation,
  then one coherent updated Native candidate for the existing Mobile38/fe842
  partner. Do not repeat the already verified Mobile38 setup or its passing
  regression. Native build/sign/runtime and real matching peer roundtrip remain
  open. No repeated unchanged test or broad matrix.
- **Other retained packages:** native Reader/Markdown69ee9f5 is source-complete but
  not in the runningb23 snapshot. Mobile Peek4ff9835 is compiled in current apps,
  but its long-press journey is NOT_RUN because current CUA gestures navigated
  instead. The three existing ImageGen directions remain unselected/unimplemented.

## September19 chronology — evidence only, not current resume instructions

Root verified no previous worker or Ahoi build was still active. The old
ephemeral worktrees are absent, not resumable handles; their committed sources
and canonical artifacts survive. Root now owns Native/Common integration and
the sole shared Chromium build. The independent
`/root/simulator_cloudkit_20260919` owns the matching Mobile Simulator
candidate and short real normal CloudKit/status journey. No physical-iPhone or
Mac-unlock request is pending; no synthetic key, fixture or injected Sync opt-in
may be counted as CloudKit acceptance.

Core coalescing correctioneffe985 is retained. Root completed its exact
ReadOutbox pagination seam in108e91aea7bae43542885d54fce8b35a9a92d232:
oldest entity first, newest existing convergence/original within that entity
first, original consent filters and accepted-row limit unchanged. The provider
still proves actual field dominance before acknowledging originals. This
source also includes Nativea47's required compiler corrections and complete
archive/search/restore/delete/Home source. No schema/engine or new clocks.

Initial combined invocation77793 ended overlay0/build69 before compilation:
the former Xcode26.6 default had been replaced by Xcode27 and its build path
requested license acceptance. The original log/exit remains under
`artifacts/build/native-unified-108e91a-20260919/`; no license was accepted.
The user expressly authorized Xcode26.5 while the Simulator download runs.
Commitb23c7d9 selects installed26.5/17F42 with iOS SDK23F73 ONLY for compatible
development, preserving all exact checks and pinned/release26.6/17F113/23F81a.
The actual host check and two existing config/provenance checks passed; global
xcode-select and all downloading/Simulator services remain untouched.

Corrected app-only run7980 used clean detached source
b23c7d9dfaf40752dc5128455579ec4781d45870 in the same preserved
`/private/tmp/ahoi-native-unified.1FqoG5/repo`, initially two jobs/no test targets.
The changed toolchain path invalidated a large part of the previous cache:
the actual graph has49,475 actions, not a short incremental frontier. After
4,955 actions Root deliberately interrupted only its revalidated NinjaPID1230
to increase useful concurrency. Handle7980 ended2 with the normal wrapper
restoration; this is an operational interruption, not a compiler failure or pass.
Its original logs/exits remain under
`artifacts/build/native-unified-b23c7d9-20260919/`.
The six-job continuation44519 reached7,456 further actions, then Root reduced
only its own workload after new sustained pressure (foreign VM14GiB, CPU9–14%
idle, swap growth to about5.7GiB). Own NinjaPID43687 was verified and interrupted;
the wrapper ended2 with restored temporary patches, original log/exit retained
under `artifacts/build/native-unified-b23c7d9-20260919-jobs6/`. This was not a
product error or pass. Completed object files remain.
The SAME clean source/output resumes as guarded handle4212, TWO jobs, no tests,
after47–48%idle/49%memory headroom/stable swapouts. Current evidence:
`artifacts/build/native-unified-b23c7d9-20260919-jobs2-continuation/`.
No installation or runtime pass yet; old32GiB gates are historical.

**Current Native handle supersedes4212:** after stable57–63%idle, pressure level1
and no sampled swapouts, Root raised only its own concurrency from2 to4. The
exact verified Ninja99332 was interrupted normally;4212 ended2 after5,080 actions,
not a compiler failure. Same cleanb23c7d9/output now runs guarded handle64423,
app-only/four jobs, preserving all completed objects. Evidence is
`artifacts/build/native-unified-b23c7d9-20260919-jobs4-continuation/`.
At the latest readback it was in the normal dependency/hook validation, not a
missing process or new source build. Do not signal old99332 or resume4212.

The user confirmed Apple-first: finish macOS/iOS with CloudKit now; Windows and
Android are future demand-dependent work. Keep existing provider boundaries,
but do not introduce a new backend, account system or cross-platform scope now.

Mobile26.5 compilation reached product sources, but device-specific and generic
actool attempts failed against the changing Xcode27 CoreSimulator device-type
inventory. The worker retains those red logs and does not repeat the unchanged
attempt or restart shared services. The download affects that Simulator phase,
not the independently runnable Native build. No real CloudKit pass is claimed.

### Independent product development while Simulator data downloads

The user's direct instruction is to continue development, not wait for iOS27.
After securing the terminal actool boundary, the existing Mobile worker now
implements the required explicit native link-preview/Peek in the existing
Swift browser lifecycle. It owns Mobile sources/resources only; normal/private
context, original page state, deliberate adoption and no premature Sync record
remain binding. No automatic preview, new transport or mobile Split UI is added.

`/root/desktop_page_actions_20260919` independently implements required native
Reader/ReadingMode and explicit Markdown-link copy through existing Chromium
page/command services. It owns scoped sidebar page/context/command code and
the existing command/localization bindings, not Common Sync, Swift or build/out.
Active-pane identity, URL credential removal and private clipboard boundaries
remain binding. Both workers are source-only while the frozen build runs.
Their product changes enter one later coherent integration, never the running
snapshot. Neither worker spawns helpers. Root retains integration, the single
Native build, subsequent representative E2E and result verification.

**These two bounded source tasks are now complete and handed back.** Mobile
Peek is4ff983594a473db5295844953bbfcc532c12159f, including real sheet-onDismiss
handoff, preview navigation errors/retry, same-WebPage adoption and separate
normal/private lifetime. Root read the adoption/datastore/cleanup/presentation
paths; parsing/project checks are not runtime acceptance. Desktop Reader and
link-copy are362e338b181f02c73025c180695c8afba5a15808, followed by the literal
Markdown-label correction69ee9f5307f54aaa9dab0c951a163a7fd2011de1 after Root's
specific finding. Reader uses M152's existing reading-mode command; context
actions revalidate WebContents/navigation-entry/URL. URL userinfo is removed,
and CommonMark punctuation cannot turn the title into inline HTML/formatting.
Root read these implementation boundaries. Both packages are DCO-pushed and
NOT_BUILT/NOT_E2E; they do not enter the immutable runningb23c7d9 snapshot.
Do not claim their source-only checks as a visible or Sync pass, and do not
restart the expired Mobile actool attempts without a changed prerequisite.

One useful independent Mobile gate is assigned to the same worker: compile
only the existing product `AhoiMobileCore` framework containing Peek, if its
normal target avoids app-asset generation. At most one Xcode/Swift job after
a fresh capacity check, no new scheme/harness, tests, Simulator boot or Cloud
action. This is a product type/link check, not a substitute CloudKit/UI pass.
The worker initially deferred during actual pressure; the later two-job Native
continuation leaves it eligible only when its fresh aggregate check permits.

That bounded Core gate is now complete, not another pending build:41067 EXIT0,
exact clean4ff983594a473db5295844953bbfcc532c12159f, Xcode26.5/17F42 with SDK23F73,
one job and three product/dependency targets. Root read the actual exit/log and
independently hashed the preserved AhoiMobileCore executable as
d93f54ddcf4ef20fa3d7aa52bcab49e112f5836b0a9db3772046e05037967453.
It includes compiled/linked MobileLinkPreview. Evidence/limitations are in
`artifacts/build/mobile-peek-core-4ff9835-20260919/`, handbackca5dbcb.
The initial target/derivedData CLI rejection64 is retained separately. Redundant
internal(set) and AppIntents warnings are retained, not suppressed. This is a
Core-framework build only: app/assets, signing/install, CloudKit and visible
Peek remain unaccepted.

**Runtime prerequisite changed:** the user reported the iOS27 download complete.
Root confirmed public simctl inventory lists available iOS27.0/24A434 and
iOS26.5/23F77; the AhoiC645 device remained Shutdown. The Mobile worker now owns
one coherent app-candidate/normal-UI continuation including Peek4ff9835 and the
existing non-fixtured CloudKit-status case. Prefer the matching Xcode27/Simulator
pair to avoid the prior26.5 actool/service mismatch; record its actual source,
SDK/signature/config and any account boundary. No license acceptance, global
xcode-select or shared-service change is implied. Native4212 remains26.5/two jobs.
Mobile uses at most one compiler job after a fresh whole-machine check and only
its own explicitly identified device; the foreign TerminalCockpit build and
other Simulator projects remain untouched. No physical-iPhone action or repeated
DebugLocal25 journey is requested, and readiness alone is not a CloudKit pass.

**Own-compiler handoff consumed:** Root briefly stopped only its verified
Ninja99332/99307, letting its compile children end naturally, to let the small
Mobile phase finish. After Mobile's real terminal handback, Root revalidated
the exact same Native PID/parent/command/StateT and sent CONT, observing StateR.
Handle4212/source/output continue unchanged, with no new wrapper/hook run or
discarded object files. No foreign job, download or global service was stopped.

**Actual Mobile28 result:** build99539 EXIT0 on clean4ff9835, CloudKitDevelopment
0.1(28), Xcode27.0/27A266a with SDK27.0/24A430; actual ownC645 runtime remained
iOS26.5/23F77. Candidate receipt SHA12ad28fd1fe2e33c4ee5189c6162cb31c7710ad30dfc7131319d17202e9aaf70
and app-tree SHAa41005a57539c65d698d5c64ea37e7baed9bb5b263ee193bb9fb933dc79f4d3a
are under `artifacts/e2e/mobile-cloudkitdevelopment28-xcode27-20260919/`.
Initial UI8291 failed before execution because copied xctestrun __TESTROOT__
pointed at the wrong directory; that red run is retained. Corrected UI5536
finished EXIT0, one normal non-fixtured Settings journey,1/1 with no skips.
Root read its actual summary/exit/receipt and inspected the status screenshot.

The observed product state is NOT CloudKit success: `Nur lokal`, encryption
recovery required, configurationMissing=true, manual Sync disabled. Normal UI
opt-out and app termination completed; the own Simulator is Shutdown. The
bounded trace found a real product diagnostic collapse: the runtime factory
catch in CompanionAppModelSyncLifecycle maps every thrown activation error to
keychainFailure, while bootstrap recovery can produce the same visible state.
Static configuration and Simulator entitlements are present; retained evidence
does not identify noAccount, a particular Keychain OSStatus or a server failure.
Do not call this an external-account-only gate or a cross-device pass.

The same Mobile worker now owns the exact corrective product seam: preserve
sanitized typed configuration/account-access/Keychain/bootstrap reasons and
render the appropriate short status/action. No raw NSError userInfo, account,
record or key data; no new telemetry system. Do not make readiness/config/keys
or consent artificially true. After a coherent source fix, repeat only the same
normal visible activation journey on its exact candidate to establish the actual
cause; no fixture, manual key copy/reset, account injection or matrix expansion.
The current Build28 remains preserved and
the new diagnostic result must not be retroactively assigned to it.

**Build29 compute handoff (September19, 14:22 local):** typed setup source
42a1f835a3350ed35b758e92576f60061b6add39 is committed. Root read the
classification/lifecycle/UI seam and revalidated own Native scheduler99332,
parent99307, command/output before STOP (StateT). Only this scheduler is paused;
existing children finish normally and no foreign workload is changed. Mobile
owns one one-job candidate build and the same normal activation journey after
its fresh capacity check. Root resumes the SAME scheduler on actual terminal
handback; do not start another Native wrapper or discard objects. This is a
new bounded handoff, distinct from the already consumed Build28 handoff above.

**Build29 handoff now consumed (14:25 local):** build42569 EXIT0;
normal UI56143 EXIT0, exactly one case in30.247s. Root independently read the
terminal exit/log and candidate binding. Same source42a1f835, Xcode27/SDK27,
actual C645 iOS26.5 runtime. Observed cause is
`cloudkit-account-or-permission`, configurationMissing=false, not a proven
noAccount or specific permission error. Existing bootstrap maps both CK errors
to this category. No CloudKit/peer-roundtrip pass. Normal opt-out, termination
and own Simulator Shutdown were handed back. Root verified scheduler99332/99307
StateT and resumed it to StateR; Native4212 continues unchanged. Evidence:
`artifacts/e2e/mobile-cloudkitdevelopment29-xcode27-20260919/`.

The subsequent read-only native Settings check on own C645 showed the generic
Apple Account sign-in prompt: no Apple Account is signed in. No identity or
account screenshot was retained, no login/key action performed; Settings ended
and C645 returned to Shutdown. Root offered the user a nonblocking self-login
option; the Native build and independent product work do not wait for it.
The exact CKError was discarded upstream. Also accountUnavailable can originate
from local authorization/shutdown guards, so the Build29 code alone is not proof
of a particular server error. The Settings observation independently proves the
missing account prerequisite, not successful CloudKit access after login.

**User login changed the prerequisite:** on the user's request, own C645 was
opened at the native sign-in start and handed to the user without reading
credentials. After the user's login update, the worker observed no remaining
sign-in sheet/prompt and retried normal Ahoi29 activation. It still reported
cloudkit-account-or-permission, keys off and manual Sync disabled. Opt-out,
app termination and own Simulator Shutdown completed. Therefore missing login
alone does not explain the remaining failure; do not ask for another login or
claim a permission-specific cause. The worker now checks safe existing error
codes, or minimally preserves the actual CK access code and distinguishes local
authorization cancellation at the existing bootstrap seam before ONE corrected
candidate. No key/account reset or consent bypass; Native4212 continues.

Build30 source719024631c05fa95d68fb21ce2b8e74f35e84eac now retains actual CK9/10
separately from local authorization cancellation; c38ff81 also makes the existing
disabled Sync action visibly disabled without changing its authority condition.
Root read both small diffs. For this one corrected candidate/activation run,
Root revalidated99332/99307 and STOPped only its scheduler (StateT); children
finish naturally. Mobile uses one job and its own already signed-in C645.
Resume the same Native4212 scheduler after the real terminal handback; no new
Native wrapper, account/key reset or extra test matrix.

Build30/7190246 is now terminal: build9427 EXIT0, normal UI96295 EXIT0, one
case34.998s. Actual cause is `bootstrap-recovery:accountChanged`, not CK9/10 or
local-authorization; disabled action now visibly dims. Root read terminal
log/exit and resumed revalidated99332/99307 to StateR. Own C645 ended OFF/Shutdown.
The concrete remaining product seam is bootstrap handleEvent: every accountChange
was treated as failure, including signIn from a fresh engine. The existing normal
provider already distinguishes initial/matching signIn from signOut/switchAccounts.
Mobile owns a bounded matching bootstrap correction preserving real identity/
authorization boundaries and remote inspection before claims; no blanket ignore
or recovery upload. Native bootstrap was read separately: it uses account status,
identity checks before scan/final completion and account-change notification,
not this CKSyncEngine signIn path. No Native change from this finding.

The matching Swift correction32b5751 plus sticky account-invalidation5031197
preserves identity checks before/after asynchronous boundaries and before the
accepted-receipt callback; operation clears cannot revive an invalidated
transport. Root inspected the concrete race correction. Build31/5031197
30909 EXIT0; normal UI29883 EXIT0, one case34.887s. The prior accountChanged
loop is absent; actual new boundary is `cloudkit-error:2` (partialFailure),
not a successful Sync pass. Own C645 returned OFF/Shutdown; Root resumed the
same verified Native99332/99307 after this bounded compute handoff.
Next Mobile diagnosis is the specific per-item CloudKit failure, preserving
only safe numeric codes. A missing fresh requested zone may be wrapped in
partialFailure, but no generic partial error may be treated as an empty zone.
No unchanged retry, key reset, new test matrix or account action is authorized
by this result. Native4212 remains the same live build.

Source4d34b08 adds the bounded partial-error handling: only confirmed zoneNotFound
for the single exact requested zone can enter the existing fresh-zone path;
userDeletedZone, other zones and mixed failures remain errors. Numeric leaf codes
are retained without identifiers/descriptions. Root read the implementation.
Own99332/99307 is temporarily STOPped (verified StateT) for the one corrected
Mobile32 build/activation; resume the SAME Native4212 after actual handback.

Mobile32 completed on compile-corrected57a2e0a (4d34's operation semantics
unchanged): Xcode build succeeded; its wrapper had a post-build zsh readonly
variable failure, retained separately, not a product compile failure. UI34398
EXIT0, one case33.717s, still `cloudkit-error:2` without leaf codes. No Ready or
Sync pass. Own C645 ended OFF/Shutdown; Root verified99332/99307 and CONT→R.
Root identified remaining diagnostic loss at zone-create/claim-send thrown and
delegate errors, which still reduce CKError to rawValue; the fetch partial path
now emits a different typed value. Mobile now preserves one bounded operation/
code/leaf description across these existing boundaries, without raw identifiers
or a new logging system. No unchanged UI rerun or more speculative zone handling.

Mobile33/1acf62d now gives the actual fault: build79716 EXIT0; UI46453 EXIT0,
one case33.016s; safeCode `cloudkit-error:claim-send:26` (zoneNotFound).
Fetch/ensureZone returned without a failure but the claim's zone did not exist.
This disproves treating default zoneExists=true/no delegate error as server proof.
Own C645 ended OFF/Shutdown; Root resumed verified99332/99307 to StateR.
Mobile now corrects the bounded bootstrap to use authoritative zone existence/
save acknowledgement and actual full remote zone inspection before claims,
using the same CloudKit private database and existing claim/journal/consent
contract. Normal CKSyncEngine provider stays unchanged. No pending key deletion,
new engine/category, broad retry or premature Ready claim.

Mobile34 sourceb5cb6f3 incorporates direct server zone/save/full-scan0200bd2,
account-notification fencing2698e96 and its Swift6 observer-cleanup correction.
Build39664 succeeded, receipt509f79ef…; initial UI81381 EXIT65 reached the
harness30s activation deadline without a typed error or Ready state. Preserve
this red run; it is not proof of a specific product/CloudKit error. Actual host
load had risen to1.6–8.2%idle, so Root stopped only freshly verified Native
Ninja97055/97032 (current handle64423), leaving completed objects intact.
The worker now has ONE normal manual-UI continuation on unchanged Mobile34
after this changed capacity prerequisite, no rebuild/fixture/key reset. Allow
the actual operation to settle rather than treating30s as a product limit.
97055 staysT until that concrete handback; old99332 is terminal/historical.

That manual continuation is now terminal: installed34 remained setup-pending
for over2:27. A bounded read-only sample of ownPID31766 showed actual
sentRecordZoneChanges→acceptSentChanges→acceptedHandler→markClaimAccepted,
blocked in Security SecItemCopyMatching/SecItemUpdate XPC. A saved/decoded claim
and its receipt callback were reached, not a completed key promotion/Sync pass.
Normal SyncOFF, app termination and C645 Shutdown completed; Root revalidated
97055/97032 and CONT→R. The worker moves receipt persistence outside the
CKSyncEngine delegate callback, after send completion/account verification but
before createClaim returns created. This addresses the concrete callback/XPC
coupling; the sample alone does not prove its causal deadlock. Preserve all969
pending/canonical/journal/server state. No manual promotion or fresh-scope reset
may be inferred. Old Build34 red timeout remains evidence, not overwritten.

Receipt callback correctionbc12536 is committed and Root-read: only decode/store
receipt in delegate; persist after send completion and continuity proof, before
returning created. For ONE Mobile35 build and manual normal activation on the
unchanged signed-in C645/969 state, Root revalidated97055/97032 and STOP→T.
This supersedes its immediately previous CONT; Native handle remains64423.
Manual activation replaces the known30s harness deadline, not the receipt/key
safety checks. Existing incomplete-receipt recovery must remain honest; do not
delete, inject or promote stored state to make this run pass. Resume97055 after
the exact terminal handback.

Mobile35/bc12536 build12295 EXIT0 and normal manual activation reached terminal
bootstrapOwnershipUnverified recovery rather than another indefinite callback.
This is correct fail-closed old-scope behavior, NOT Sync acceptance or proof of
the new first-creation callback path. Own C645 ended OFF/Shutdown; Root CONTed
verified97055/97032→R. Preserve35 and all old969 local/server/key material.
Root now prepared d9faeaa's separate fresh scope37b55dda-30d6-4664-a91c-c91cab92d14b
in `artifacts/e2e/shared-sync-development-scope-20260919.json`, SHA
7201aa070922ad7ca9f27ef20601024df3b863646e87d434510aa1b0206e79f7.
Existing MobileDevelopmentScope already isolates stores/defaults by this tuple;
no reset, account copy or new persistence harness is needed. The worker prepares
one configuration-bound Mobile36 from unchangedbc12536 for normal fresh opt-in.
The same tuple is reserved for the eventual guarded Native Development copy;
no native install/sign or new CloudKit mutation has occurred from preparation.

Installed55ab and all matching Device24/25/26 artifacts remain unchanged.
The15September storage release was for exactly21 enumerated obsolete bundles
sent directly to019e5926, not `.work`, sources, logs, profiles or current apps.
The installed55ab, direct rollback26, provider-free fallbacka24 and open-file-
referenced c8 rollback were specifically protected; old blanket rollback text
below must not revoke that exact scoped owner handoff or release other paths.

DebugLocal25's normal, non-fixtured Simulator Settings/Sync journey passed1/1
after the recorded runner correction (086e9b8); it proves local-only behavior,
not CloudKit. Do not repeat it instead of checking the entitled Simulator path.
The three ImageGen designs remain unselected and are not implemented.

All older owners, PIDs, resource waits and next-step phrases below are history;
use them only as evidence for the specific failure or candidate they describe.

## Latest user direction and actual peer result — September13

The user now explicitly requests proceeding WITHOUT the physical iPhone,
using an own Simulator where possible. Stop repeating the Phone/Servusla and
Mac-unlock requests. Mobile26 remains signed/preserved but uninstalled;
DebugLocal25 is provider-free and cannot be used as a CloudKit proof. The
Mobile worker is preparing a device-bound normal Simulator journey, using
real UI/screenshots and no fixture/SyncProjection or injected opt-in. Any
Simulator iCloud/account or access limitation must remain explicit; no local
simulation is a physical or genuine CloudKit pass. BetterConvo751D is foreign
and remains untouched; there is no blanket MBC/Simulator reservation.

The once-unlocked Mac allowed a real Native55ab MacA/MacB profile journey.
Both normal clients reached successful uploads/ACKs, and B visibly reached
Ready with A's existing logical rows. Both were on ONE physical Mac with
different device UUIDs. The new IANA navigation exists in A's NativeTree and
its History record reached both Common stores, but the page did not reach
Shared Tree/Presence. Readback now shows A knows B's old empty capability,
whereas B has the newer shared-normal-tabs-v3 declaration. The writer is
therefore correctly gated. The frozen55ab Core upload loop coalesces by entity
but keeps only the last mutation ID, leaving older Outbox versions behind to
be sent with newer server ChangeTags. Root read the loop and saved-record
byte-match/ACK handler; persisted receive chronology matches the backward-write
path. The actual current server state was not queried. This is not a proven
Native capture defect or permission to bypass matching capability readiness.
The Desktop worker now explicitly owns the narrow corrective Common writes in
`cloudkit_sync_provider_mac_consent.mm` and
`cloudkit_sync_provider_mac_internal.h`: preserve all original covered mutation
IDs/versions, require actual fieldclock/tombstone dominance for coalesced ACKs,
and stage/merge concurrent server state through existing authorized durable
paths. No topclock-only shortcut, new clock invention, wire/schema/engine or
consent change is allowed. Source is to join the ONE corrected Native candidate,
not launch an extra build in the current disk reserve.

The Mac locked again during the later UI step. Only the two freshly identified
own test PIDs49166/90916 were controlledly terminated; handles89088/46806 both
EXIT0, processes absent, data retained. Final A/B Outbox0/0, retry0/0, ACK20/10
are bounded evidence, not new-tab/focus acceptance. Exact observations and
capability versions are in
`artifacts/e2e/native-peer-55abcf7-20260913/{README.md,result.json}`.
No Root Native/UI run remains active. Installed55ab and physical Device24 are
unchanged. Older PID/lock/wait instructions below are historical to their step.

Native Structure source89993af's first app-only68856 run ended EXIT1. Six
compile causes were narrowly corrected in five Native files in a47b185;
handoff67deb61 preserves the failure and exact next run script. The corrected
worktree `/private/tmp/ahoi-native-structure.HoBjQO/repo` and shared out remain
protected. No build is currently active or paused; the actual remaining build
constraint is about32.3GiB disk headroom, not a source/role/UI permission wait.

## Current parallel-work rule — explicit user decision, September13

The user explicitly removed the blanket MBC/Ahoi window reservations and repeated
START/handback coordination: parallel work is intended. The current global and
project AGENTS require equal whole-machine capacity assessment across projects,
without a special Ahoi resource priority or preemption. No worker waits for MBC's
whole build or UI journey. Separate builds, devices and directly addressed
windows may run concurrently. Only actual sustained machine pressure or a
concrete collision over global input warrants separating the affected short
action. No foreign process is stopped and no general START/ACK cycle is required.
This supersedes the dated UI leases and earlier resource-priority wording below.

`/root/desktop_recovery_20260913` proceeds autonomously through the exact guarded
build/sign/install and short native E2E with fresh candidate, scope and own-window
checks. Verify the corrected OFF label/sidebar, then visible Sync ON and, if
needed, the authorized local-upload recovery in MacA only. Use documented
`cua_repl` actions and own-window captures, not unverified global keystrokes or
an AppleScript/System Events/CGEvent fallback. No hidden AX
action, flag/key/store reset or real-Default operation is authorized. Existing
file/build ownership, consent and data-safety boundaries remain intact.

## Actual recovery and current workers — September13

**Current installed candidate is55abcf7, with the event-driven receiver and
original per-setting delivery leases.** Build61607, scoped sign/verify40936 and
install28512 completed EXIT0. The actual remaining visible gate is the locked
Mac, described below; this candidate has no live-peer acceptance yet.

**Previous bounded runtime evidence belongs to26b01b1.**
Build65225, scoped sign/verify65919 and install69712 completed EXIT0. The real
retained-MacA journey performed a visible manual retry before the old deadline,
then one justified follow-up with server metadata. The same process stayed
stable and displayed "Synchronisiert und bereit" before ordinary quit.
Root independently read the actual installed26 source and post-quit database:
outbox0, acknowledged16, native_observations2; retry0/0/0/empty and both recovery
flags false. Bookmark consent remains revoked. This proves the native MacA
upload/ACK boundary, NOT a peer/physical Mac–iPhone roundtrip. Exact receipts,
counts and retained failures are in the current
[Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md) and
[26 runtime report](../artifacts/e2e/native-sync-26b01b1-20260913/README.md).
Canonical product commits614d297/2948361/628d163/f7d276c/6fee878 preserve these
corrections; old55/178 builds, failures and retry deadlines are historical.

Root has asked the user for the one ordinary physical action needed on the
confirmed installed Device24: open Ahoi Settings on Servusla, enable CloudKit
Sync and report the status without account/key details. Until an actual response
or readback, Phone opt-in is unproved. No manual key copy, zone/store reset or
artificially seeded phone state is allowed.

**Receiver gap found in the previous candidate:** read-only inspection of the exact26/Device24
sources found that Mobile already schedules domain import/projection after
unsolicited CK fetches, while Native only persists its inbox and waits for a
later startup/manual/local/five-minute sync. That is not the required live
arrival in an already open native window. Desktop has now implemented
the minimal durable-inbox -> provider signal -> existing domain import/native
projection wake, without a new engine or a faster polling timer. Receive-only
work must not wait for an outgoing backoff or clear it as a fake upload success.
The active bounded scope includes the existing provider/interface/factory,
pump/backend/service, and the necessary SyncStore import/precommit lease plus
preserve-outgoing-retry option. Coalesce events, retain original consent and
generation through import/ACK, and never focus or eager-load peer pages.
No other Common/Structure WIP is transferred. The preserved26 candidate remains
intact; receiver source/build evidence is not a live-peer pass before E2E.

Receiver455f1bb build60531 completed EXIT0 but was NOT installed. The final
55abcf7 correction retains original per-ID/category read leases in an exact
delivery-token authority through dispatch, Store commit, provider ACK and UI.
Root reviewed that boundary. Final app-only61607 completed EXIT0 with receipt
SHAd1455677a2e8810ab4782bf029c7b5e13a073b76ab188cca55ea6798963d7cd8.
The final receiver is now installed as scoped Development55abcf7; Root read
the actual installed source. Its install receipt is
`artifacts/install/ahoi-dev-receive-55abcf7-scoped-20260913.json`.

**Actual remaining UI access gate:** the normal app and Finder CUA paths
failed to obtain a visible window. A fresh OS read, independently repeated by
Root, shows IOConsoleLocked=Yes and CGSSessionScreenIsLocked=Yes. This is a
locked Mac, not an MBC reservation or a demonstrated receiver deadlock. Root
asked the user once to unlock; do not repeat start/reset/input attempts until
that event. MacB was verified absent and has NOT been created or started.
Its normal isolated Native-peer journey is explicitly authorized after access
returns, without seeds/key copies or replacing the required physical-peer pass.
Only minimal useful existing focused receiver checks may proceed under the
explicit access-blocked E2E exception; no new harness or general test matrix.

**Bounded receiver checks are terminal: four of four PASS.** The initial
handle36731 built successfully but its four cases failed before meaningful
receive validation: the synthetic tab lacked the Format3 TreeNode link and
target kind. Source0f63dd8 adds only those two required fixture fields, without
changing a product validator or assertion. Corrected handle97733 completed
build EXIT0 and run EXIT0, exactly `SyncReceiveBoundaryTest.*`, one job and zero
retries. Root read both runs, the exact fixture diff and all four SUCCESS entries
with one attempt each. The corrected summary SHA256 is
`386f713666fbe07e64d9be628d12c586b054f07add2fc09f1e88e42b9e959887`.
Original failures remain under
`artifacts/tests/receive-boundary-48f7d71-20260913/`; corrected evidence is under
`artifacts/tests/receive-boundary-0f63dd8-20260913/`. Only the existing test file
differs from installed55ab; the test-stamped app was not installed. These are
consumer/store/consent checks with a synthetic provider, not a transport,
open-window or physical-peer pass. No more receiver checks are queued.

The repeated access gate has no remaining independent receiver test task:
resume its visible journey on an actual Mac-unlock / phone-activation event,
not another unchanged status turn. Preserve the full shared-Sync acceptance;
do not mark it complete from these tests or reduce it to one Mac's upload/ACK.

**Independent confirmed Crest/Sync source work continues.** After the terminal
receiver handback4caaca3, the same Desktop worker explicitly owns the existing
Native Structure WIP: native/workspace structure and SessionBridge consumers,
NativeTree persistence and ResourcePolicy integration. It will close coherent
Split capture/dormant projection and local archive/restore lifecycle against
the committed e7abcff/c2718ac contract, without writing Common wire/provider or
Swift files. Local Sync-OFF authority, original intent/receipt/CAS, protected
active pages and no eager loading/focus changes remain binding. This is not a
new feature request or a reason to await the locked screen. Root checks the
matching existing Mobile integration and candidate path in parallel. No new
Chromium build, installed55ab/Device24 replacement, bba reset or design-variant
implementation is authorized by this source handoff; the later schema7 pair
needs its own exact fresh acceptance binding.

That non-secret binding is now prepared in
`artifacts/e2e/shared-sync-structure-development-scope-20260913.json`, scope
`96950f6b-50e0-4e2c-9a94-852dc5099446`, SHA256
`5b3586741c8ab4711bf096ac6960b046732a43489bb4ae9c6018afa64439c9f7`.
The existing Native `development_acceptance_policy` accepted its exact tuple
and unchanged rights/constraints. The current Mobile preflight/runtime scope
parsers use the same UUID/zone/subscription/account convention. Only the Native
configuration validator ran; no candidate, profile, key or server state was
mutated, and server freshness is NOT yet proved. Native schema7 and Mobile
structureRevision1 belong to this later matched pair, not the preserved bba pair.

`/root/mobile_actions_recovery_20260913` completed ONE product-only
generic-Simulator DebugLocal candidate and handed it back in7afc8f2: exact clean
source6446b534b3269befaf36a07fb80bde6e0251e745, version0.1(25), four product
targets, two build jobs, EXIT0. The committed Home/Reader/Markdown, Structure
and private-lock sources compiled together without a product correction. Root
independently read build.exit/Info.plist and verified the existing candidate
receipt against the preserved app and clean Xcode-project snapshot. App-tree SHA
is2bd065664f717ace4641ac16a61f1ea9e937d3845e662e816067771299cad31e;
report/app/receipt are under
`artifacts/build/mobile-debuglocal-6446b53-20260913/`. The isolated source and
DerivedData at `/private/tmp/ahoi-mobile-debuglocal25.7q8d3O/` remain protected.
No test target, Simulator boot, app/device launch, provisioning or Cloud access
occurred. This provider-free local-UX candidate is NOT the entitled Structure
sync partner. Its visible Home/Reader/private-lock acceptance remains open;
Device24, installed Native55ab and the bba scope are unchanged. No Mobile build
or test is currently queued.

**Later physical partner prepared, not activated:** Root built the same clean
6446b53 Mobile source as CloudKitDevelopment0.1(26), generic iOS/arm64,
handle88339 EXIT0, four product targets and no tests. This separate build binds
the prepared969 Structure scope. A preserved copy was signed with the existing
iOS Development profilebbf658ff and exact147982d5 signing identity; handle92531
and strict/deep verification EXIT0. Actual extracted certificate, exact signed
Development entitlements, embedded profile bytes and unchanged Info.plist were
verified. All seven actual built runtime fields match the prepared scope.
Signed app-tree SHA256 is
c11ed4f723c888a10b7e4876361c6c8d0fb3bf227b031700ba0262eb44e75414;
unsigned/signed apps and exact receipt/report are under
`artifacts/build/mobile-structure-development-6446b53-20260913/`.
No device installation/launch, payload-key operation or CloudKit request took
place. Existing profile/certificate/private keys were not created or replaced;
ordinary signing used the already authorized identity. The old bba/Device24 and
Native55ab remain unchanged. This is not a Runtime/CloudKit pass.

Native source683d91b/e29ef31 now connects Structure persistence, capture,
archive/restore/policy and Home actions. Root checked that Mobile/Swift and the
format manifest/Golden are unchanged from6446b53 through e29ef31. Before the
ONE following native candidate, the Desktop worker is closing the two still
explicitly required Master items: searchable archive with reason/time and
separately confirmed final deletion through the existing absorbing tombstone
contract. These are unfinished required product behavior, not a new test matrix
or an unchosen general UI redesign. No Native build/install has started.

A single selective physical-preference read found Device24 still Sync OFF;
its temporary local copy was removed and the original device state was not
modified. Do not repeat that read or question without a user response or
another relevant change. The three ImageGen options still await selection.

The previous two workers were no longer in the live collaboration inventory.
The old `/private/tmp/ahoi-native-sync-build.eqejEO/repo` is missing; the Git
worktree registration is stale, NOT a running or resumable process. Its exact
e4de9e14ca3073876e9aa17730876d15cfa41683 commit and original logs survive.
No cause of the missing directory was established or attributed to the storage
owner. Nothing was pruned or reset.

Desktop recovery secured the commit under
`refs/ahoi-preserved/native-sync-e4de9e1-20260913` and restored a clean detached
snapshot at `/private/tmp/ahoi-native-sync-recovery.DxdGLK/repo`. Its new guarded
overlay/app-only run is82567, with logs/exit/receipt under
`artifacts/build/native-sync-e4de9e1-recovery-20260913/`. Initial capacity was
68 percent CPU idle, no compiler,94 GiB free and0 swap; three build jobs were
selected. This is a NEW recovery invocation, not a claimed terminal result or
resumption of September12 PIDs. Desktop supplies its true terminal state.
The storage owner received this exact new protected path in01a0982b.

**Actual subsequent result:** app-only82567 completed EXIT0. Root read its
clean-source receipt and verified SHA
ee559365e251b10f7f6a49d60e18f61466a14048f44634e3ae84b2d551d51e37.
Separate Development sign/verify26850 and guarded install42266 completed EXIT0.
Root independently read installed e4, Development and the bba zone, and verified
installed executable SHA
4ca4ef074b1e28bdad8f01a0695ffba596136ed7d0fc71484a1c4321d6217dd9.
Install receipt `artifacts/install/ahoi-dev-e4de9e1-scoped-20260913.json` has SHA
eb54ab0e73174286e0fd3fb06e60bf1e3e661f370a68a59a84b817707047d245.
The bounded e4 journey fixed OFF/ON hierarchy and made the recovery action
visible. After the deliberate local-upload confirmation, account recovery read
back false, but the browser then crashed. Desktop symbolized the fatal path to
`SyncStore::AcknowledgeOutbox`: unsupported UPSERT created an invalid Chromium
statement. Root confirmed the pinned `SQLITE_OMIT_UPSERT` setting. The second
same unsupported SQL form was in the native observation receipt journal.
This is still a failed runtime journey, not transport/roundtrip acceptance.

Desktop received exact ownership of those two Common SQL files. Isolated
ccd24827be87ab2cdcc0e2579137eb7d83700b4b preserves the same schema, transaction,
clock condition and receipt semantics with supported SQL. Its app-only41122,
scoped sign/verify14386 and guarded install57636 completed EXIT0. Root read the
clean build receipt3ad7f306987b090ee23e07d108f88e17a2a6875936303e6cb0b5f47b4e028160
and independently verified the installed ccd source. The two SQL fixes are now
canonical614d297; old e4 and crash evidence remain preserved. No key/store reset
or SQLite build-flag change occurred.

**ccd runtime remains PARTIAL:** the normal retained-MacA start/restore/visible
Sync Now journey was stable and normally quit. Real post-quit state was13
outbox rows,0 ACKs,0 native receipts, retry9/provider_error, with recovery flags
false. Existing public CloudKit logs identify CKErrorDomain2/PartialFailure,
not its redacted item error. The later focused check against the exact Chromium
SQLite library verified the two corrected SQL statements and clock ordering;
it does not prove a real ACK. Details are in
[the ccd report](../artifacts/e2e/native-sync-ccd2482-20260913/README.md).

Root reviewed and explicitly released only the provider HandleSent /
CompleteUpload files for the next correction: a logically resolved server
conflict can collect verified ACKs that the aggregate PartialFailure then
discards. Do not blindly ignore partial failures; current mutation IDs,
remaining item/zone failures, durable remote staging and original authorization
must remain authoritative. A small local code/count/stage diagnostic may
distinguish this path without logging identifiers, payloads, keys or raw errors.
No third runtime loop without that concrete change; other Common WIP remains
unassigned. Device24 stays OFF, freshly confirmed installed on Servusla.
Earlier c8 below is retained baseline history.

Root verified that the e4 correction changes no shared format manifest, model,
goldens or Swift/Mobile source relative to c8. The preserved signed Device24
bundle is present and its actual plist matches all seven shared scope values.
It remains the schema6 partner; this check is not a new physical-device or
CloudKit pass. The manifest's original prepared-status text is historical,
not permission to treat the already exercised bba scope as empty.

The separate Mobile worker completes only the retained Home/Reader/Markdown
product WIP in `apps/AhoiMobile`, not another runner or test matrix. Its source
belongs to the later Structure/Privacy wave, not the current Device24/e4 pair.
That bounded source package is now committed/pushed as8e81cfc: explicit Home
update/return, article extraction/Reader sheet and safe Markdown-link copying
through normal browser actions, with private/local clipboard handling and
DE/EN. Root read the actual product wiring. Syntax/configuration checks passed;
it is NOT_BUILT/NOT_E2E and no longer an active worker task.

**Physical UI boundary, September13:** Servusla is reachable and unlocked since
boot but currently requires its passcode. The ordinary Mirroring app binding
did not establish a connection; the normal Finder check found no selectable
Mirroring app and was returned to its original folder. No phone, account or
security setting was changed. [Apple's current support page](https://support.apple.com/en-ca/120421)
also states that iPhone Mirroring is unavailable in the EU; this is not proof
of a specific local account error. No region bypass or replacement UI harness
was attempted. The actual phone opt-in/observation still needs its normal
device UI after the native prerequisite succeeds. CUA's ordinary native Finder
binding/actions now work; old September12 CUA timeouts are not current evidence.

## New user UI/UX direction — selection pending

The user explicitly rejected the entire Sync form, its sidebar position and
layout, then requested substantially better general UI/UX with ImageGen layouts.
Root produced three independent, reference-grounded concepts via the built-in
ImageGen tool. All three were inspected and displayed exactly once in the order
recorded in [the design handoff](design/2026-09-13-browser-sync/README.md), with
byte-identical project copies and the full prompt set. They are NOT implemented
or runtime evidence; their connected-device/status examples are mock data.

The common design direction is ordinary central Sync settings, one clear global
control, readable data/device groups and remote-control technical fields kept
separate from normal setup. Sidebar/toolbar integration stays compact; no
large wizard, new transport or arbitrary data-category expansion. User selection
of the displayed1/2/3 establishes the visual target before redesign code begins.
The proven native SQL crash fix continues independently and must not wait for
that visual choice. Mobile8e81cfc remains a separate unbuilt source packet.

## Bounded compiled-artifact release — September13

The storage owner019e5926 has explicit release for exactly these six superseded
compiled bundles, after its own fresh use/path checks. Actual bundle plists
match the named source revisions and their build/signing receipts remain present:

- `artifacts/build/desktop-arc-preserve-e241191-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-arc-preserve-e241191-20260908/cloudkit/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/cloudkit/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-left-715afc2-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-left-715afc2-20260908/cloudkit/AhoiBrowser.app`

No surrounding directory, source, log, receipt, evidence or external provisioning/
key material is released. c8 artifacts, all `.work` checkout/output, the existing
native-sync temporary snapshot and its pending correction, `/Applications` and
ALL rollback bundles remain protected. The historical instruction to retain the
then-newest e241 CloudKit copy is superseded only for the exact artifact bundle
above. Root performed no deletion, build, test or process stop; release is not
evidence that cleanup occurred or that a measured amount of space was reclaimed.

## Current owners

| Assigned owner | Exclusive product scope |
| --- | --- |
| `/root/desktop_recovery_20260913` | Current exact e4 recovery: isolated source, shared Chromium checkout/out, guarded app build/sign/install and the bounded native UI handoff above |
| `/root/mobile_actions_recovery_20260913` | Completed source8e81cfc; idle, no build/device/signing or Common/Native ownership expansion |
| `/root` | Coordination and acceptance, this checkpoint; no competing product implementation |

Workers use the current collaboration tools. The old desktop_resume and
sync_mobile_resume assignments are preserved history; unassigned Common and
Native Structure WIP must not be taken over implicitly. Existing uncommitted
September12 product-contract edits remain untouched. Global/project AGENTS apply.

## Binding scope and packet order

Use the relevant current sections of
[the coordinator prompt](../outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md),
[ADR0009](decisions/0009-unified-prelaunch-sync-format.md),
[ADR0010](decisions/0010-full-browser-setup-sync.md) and
[the master](../outputs/AhoiBrowser-Master-Zielprompt.md), including their
September12 user changes. Old wording in the registered goal does not reopen
v2/mixed-writer or elaborate legacy migration work.

1. Close real settings/extension consumers and shared-key setup; prepare matching
   entitled candidates, not another build with an unsupported native install/
   enable placeholder. Preserve native consent and original authorization
   through commit/readback/ACK.
2. Implement the binding September12 additions in the SAME format3: logical
   Desktop split groups, non-destructive automatic archive/restore, saved-page
   Home URL distinct from current URL, and typed routing/shortcut/archive prefs.
   Common publishes exact maps/IDs and coordinates Native adapters. Mobile
   preserves recognized metadata losslessly; Mobile Split View UI is not required.
3. Integrate coherent candidates; exercise short representative real journeys,
   then needed focused checks. Signing or a simulated peer cannot close the real
   Mac–iOS / Desktop-pair roundtrip requirement.

Private state, secrets, raw extension storage, cookies/login/site data and local
runtime handles remain excluded. Received state must not steal focus, navigate
or force-close active peer pages or change their website-account context.
Fresh acceptance does not authorize deleting or resetting existing data/keys.

The full Crest-derived product additions also remain binding, not just their
sync fields: routing/remembered targets, Home/Peek, shortcuts/MRU, Reader/Markdown,
import preview/portable export, developer context/help/status and Mobile privacy
lock. Master WORKFLOW-01..08 and the matching registry IDs are present. Both
workers received the complete platform-specific follow-up scope; documentation
or an optional UI switch is not implementation/acceptance.

## Actual progress on September12

- **Native storage request hook delivered:** `4ee694e` adds patch0040 after0036.
  Root verified commit, series entry and actual request-observer declarations.
  Common consumed the direct Source handoff; the old48-line permission wait is
  CLOSED. No build/runtime pass is inferred from patch composition.
- **Native follow-up in progress:** Desktop is closing actual trusted install/
  enable/disable/uninstall plus0039, category/retry UI and the bound Development
  Prepare/Verify/Install configuration. Inventory `cf56c04` alone was not restore.
  Native/Signing source block a58e84c and bounded subsequent compiler/start-scope
  corrections are delivered. The current frozen candidate/handle and exact
  terminal diagnostics belong in [the Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md),
  not duplicated stale RUNNING instructions here. Root observed the first real
  wrapper/log; that failed first run must not be resumed as the current candidate.
- **iOS Development22 signed:** source `9658f945d7c0a80b6b5b331d6fecb4be3e40bb10`,
  iPhoneOS/arm64. Separate signed copy and receipt are under
  `artifacts/build/mobile-development-9658f94-20260908/`.
  Receipt: `development-signing-receipt-20260912.json`, evidence `2c15b97`.
  Signed tree `c28c15105060d9004433e5d91881fc9a694e928ed71e64e1e8ec3e58053953c5`.
  Root independently verified deep/strict signature, unchanged unsigned/signed
  Info.plist bytes and signed executable SHA
  `d6215784a3be8b32aeec87d564894f89395f936683b170c892471a7f5792b8d8`.
  Unsigned original remains preserved. No install, app start or CloudKit pass.
- **Locally isolated Device23 is built and signed:** fa53e31 binds domain files,
  defaults/identity, normal WebKit storage and downloads to the validated
  Development namespace. Build67411 completed EXIT0. Receipt f4c54a7 under
  `artifacts/build/mobile-development-fa53e31-20260912/candidate.json`; signed tree
  65dcd44bc99178a7baec16d84fa401c3f79ee3de93d5dab35465ffd47ed0e1df.
  Root read the receipt/actual plist and independently verified its deep/strict
  signature. Original22 and unsigned23 remain preserved. Runtime namespace
  readback, app installation and CloudKit are NOT_RUN, not signature failures.
  Device23 was installed headlessly with no Ahoi process before/after, but must
  NOT be started: three UI AppStorage properties still used standard defaults.
  Pure backport7e19476 fixes those exact bindings. Device24 e2faf54 is the clean
  fa53e31+7e19476 correction, without e7/Privacy additions; its actual build/sign/
  install result belongs in the Mobile checkpoint/receipt. This correction is
  required before the first ordinary UI/opt-in, not another feature-only rebuild.
- **DDI boundary cleared by actual user unlock:** readback20029 at10:55:57UTC
  found Servusla's DDI17F113 mounted, compatible and usable. No Xcode roll was
  needed. A later lock does not invalidate that proof or authorize blind retries;
  read the actual state at the next device action. No passcode was requested.
- **Corrected Device24 installed and ordinarily launched:** e2faf54 build54890
  and headless install8681 completed EXIT0. The signed tree is
  281f5c8b137973cb3624c894c1ea3ab6db5d23fe10d3ca5850c283b45dd16a88.
  Normal launch at11:28:36UTC used no fixture or harness arguments. Scoped local
  session/snapshot/defaults readback confirms the intended namespace and Sync
  remains OFF. This is not a visible iPhone UI or CloudKit pass. Device22/23 above
  are preserved predecessor evidence, not candidates to restart.
- **Scoped Mac installed and independently verified:** guarded install64142
  completed EXIT0 on c8d9161057cae20913ddd83bfbfadf22598d3950. Root verified the
  installed deep/strict signature, actual Development/container/zone/subscription/
  account values and executable SHA
  29118f340a5dbaceaee1c9164353bca6d9b8d9c57dc0ee17242df7b9e7402ee5.
  The real Default/Local State/tree are preserved; a24 remains in atomic rollback.
  Installation itself did not start the app. Subsequent ordinary MacA startup
  and the first real visible Sync opt-in are recorded below. Acceptance startup
  is protected to its dedicated MacA/MacB profiles, not the real Default.
- **Structure source freeze e7abcff is separate:** record13/14, Home/archive
  fields, both codecs/merge/stores/provider and common golden are implemented in
  Source, not runtime-accepted. Native DTO/persistence/capture/scheduler/UI remain
  the following integration. Its schema7/structureRevision1 is NOT in c8/Device24.
  Use a new jointly bound fresh scope if the baseline populates the earlier one;
  do not migrate or empty existing baseline data to claim a fresh next wave.

## Exact isolated Development configuration

[The shared non-secret scope](../artifacts/e2e/shared-sync-development-scope-20260908.json)
has SHA `851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe`.
It binds Development, the dedicated Ahoi container, scope UUID
`bba96b17-f044-4923-9d40-67b15014d59e`, matching acceptance zone/subscription and
payload-key account, existing service/group, key version1. Domain wire version
remains3; those numbers are unrelated. Prepared names do not prove an empty
server zone. Use actual Claim/Journal/commitment plus synchronizable Keychain,
not copied key bytes or independently provisioned peer keys.

Both actual candidates must use that same configuration and genuinely fresh
local stores before opt-in/start. iOS23 does not contain the new September12
split/archive/Home/routing/shortcut implementation; do not claim its acceptance.
See [key setup](KEY_BOOTSTRAP_SYNC.md) and the current
[implementation checkpoint](UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md).

## Runtime and remaining visible work

**September12 UI history, superseded by the current handoff above:** Root explicitly returned the completed
Ahoi-c8 window to Desktop and MBC (01a095a0). The visible Sync switch was set
OFF and read back0; ordinary application quit succeeded, with PID39235 absent
and no remaining own windows. Root holds no UI slot. The unused MBC START01a09524
was revoked; MBC's later11:50 handback was consumed once. None of these events
starts BetterConvo or revives September8 slots.
MBC subsequently started a genuinely new8998792A session (launchd_sim77724,
freshly identified on September12). Its natural handback is requested in
01a095c1 before the next native Sync UI journey. This is not an old-slot hold.
Its brief boot-time capacity spike subsided and Desktop resumed its own paused
preflight after two sufficient samples; no continuing blanket CPU block exists.

**First native visible result:** ordinary c8 MacA launch and navigation through
the existing Settings UI succeeded. CUA timed out, but permitted native macOS
Accessibility actions and own-window captures work. Root scrolled the actual
Ahoi Sync checkbox into view and changed it from0 to1. CloudKit is recognized;
the UI then reports an account/zone recovery prerequisite. AX exposes the two
account-recovery buttons with size0x0 and their Sync disclosure at0x28, so no
invisible confirmation was performed. Common's selective live flag readback
confirmed accountTransitionPending=true and zoneRecoveryPending=false. The
native AccountChange handler reset state even on first/identical sign-in.
Common793d58a now binds that event to the account identity already verified by
bootstrap, preserving real-change revocation and existing recovery flags.
Desktop fixed the zero preferred width in6a2cd6f, with the recovery
disclosure shown without confirming anything. Source-only sidebar polish29b5f8a
also addresses the user's gray-pill screenshot. The misleading OFF-state
"CloudKit unavailable in this build" copy is corrected in30e2a1a.
Screens are under artifacts/e2e/native-sync-c8d9161-20260912/. This is a blocked
changed journey, NOT a bootstrap/transport/roundtrip pass. The bba96b17 scope may
now contain setup state and must never again be assumed unused from its name.
Device24 stays Sync OFF while this exact native prerequisite is resolved. The
next c8 correction combines those bounded fixes, not the separate Structure WIP.
Its initial a95 compilation found one protected LabelButton API in the sidebar
polish; Desktop10ee5e8 uses the public API. The exact corrected candidate and
actual guarded build state are in the Desktop checkpoint, not a duplicated
handle here. For the isolated MacA test data only, Root has explicitly chosen
the visible "Lokale Daten weiter hochladen" recovery option after candidate/
scope/visibility verification. No hidden flag reset or real-Default upload is
authorized by that choice.

The physical Device24 UI access preparation found no validated attach-only
runner for those installed bytes. Existing fixture/seed/launch-based UI tests
must not be repurposed as this live-device journey. The current normal device
entry is More -> Settings -> CloudKit Sync, with an actual device screenshot/
observation still required; do not infer it from devicectl or local JSON. No
Phone opt-in was performed. Independent Mobile Home/Reader/Markdown work proceeds
in its source ownership while the native correction runs, without another
harness or an uncoordinated hardware/CloudKit action.

No September8 Simulator reservation or START message is valid today. Before
starting, the current owner checks actual surfaces/workload and gives a concrete
current handoff where needed; an unrelated idle process is not a blanket block.
Desktop retains the sole native installation/UI path; no Mobile My-Mac host with
the same bundle ID is started implicitly.

Mobile19 icon/app start is already evidenced in `184adef`; do not repeat that
solely for branding. Host-label/recognized-metadata observations remain open and
can be folded into the next relevant candidate journey. Build18's Search/Restart/
Reset proof stays bounded to its old artifact, not full Sync acceptance.

Still open: the native first-opt-in/recovery path, visible device acceptance and
shared-key bootstrap/real encrypted roundtrip, native extension restoration/settings,
September12 domain extensions and their representative cross-client acceptance.
No Production publication is authorized or claimed here.

Detailed earlier receipts and abandoned wait states are retained in
[the September8 history](SYNC_COORDINATION_HISTORY_20260908.md). They are evidence,
not current instructions. Consult other old chronology only for a concrete gap.
