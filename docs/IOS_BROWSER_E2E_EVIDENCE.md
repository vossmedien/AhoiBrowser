# AhoiBrowser Mobile E2E evidence contract

## Current internal-beta acceptance — 2026-09-05

The active completion scope is **Internal Beta Ready** in
`outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`. The older full
public-release registry below is retained with its real statuses; it does not
reintroduce that full matrix into the internal-beta goal.

Build `0.1 (10)`, source `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`, has
now passed the short source-equivalent **DebugLocal simulator** smoke:

- Cold launch, exact address entry, fully rendered `https://example.com/`,
  visible lock/origin, IANA navigation, Back and Reload.
- An additional normal tab, a distinct private start page without normal
  suggestions, and separate normal/private tab lists.
- The empty download overview and return to the browser.
- The receipt-bound Harbor collapse/reverse-scroll test: **1/1 passed**.
- The receipt-bound sync-status/opt-in/restart/fail-closed UI test:
  **1/1 passed**. This proves provider-free local behavior, not Production sync.

After that visible smoke, the same candidate's full Core suite passed:
**213 passed, 0 failed, 2 expected entitlement skips (215 total)**.
`xcodebuild analyze` also completed successfully. The Mobile repository run
passed 38/44 tests; its remaining six results exposed a macOS temporary-path
fixture issue and a stale bounded-read-model assertion. Only those two test
files were corrected; their focused rerun then passed **7/7** (all six original
failures plus one overlap). All 44 distinct repository cases are therefore
covered by the initial passing subset and the corrected focused run; the
original full run remains recorded as failed. XcodeGen 2.46.0 regenerated the
exact project without a diff. Redacted source/package/evidence scans found no
leaks, and the current Mobile line limit remains 800. The unchanged CloudKit
package retains its 36/36 result.

The older cold-launch automation result remains **failed**: fast synthetic
typing lost characters, and its retry lost the address editor. Direct Computer
Use successfully entered the exact address with separately delivered key
events and loaded independent HTTPS pages. Rapid synthetic input also reordered
characters in Apple's App Store through iPhone Mirroring, independently of
AhoiBrowser. This is a host-input/harness limitation; the failed XCTest has not
been relabelled as a pass or used to weaken TLS.

The installed simulator app's tree and executable were independently checked
against `candidate-receipt.json`. Its product code matches the signed
TestFlightBootstrap source, but it is not the same platform/configuration or
the signed IPA. No Mobile or CloudKit product source changed during this
acceptance. The real Development transport/privacy pass at `afa5cb6` remains
applicable to the unchanged sync/schema implementation in `ab2e709`.

Current retained results, logs and the signed IPA are under the intentionally
Git-ignored `artifacts/e2e/0.1-10-ab2e709-internal-beta/`. The committed
[Build 10 report](audit-evidence/2026-09-04-mobile-testflight-fix/README.md)
contains the compact acceptance record and seven Computer Use screenshots.

The internal TestFlight group was refreshed live and still shows Build 10
as `Im Test`. TestFlight itself has now been installed on `Servusla`; its
first-launch terms await the requested user confirmation. The 07:15 CEST
readback still finds developer Build 9, and mirroring reports the phone in use.
A physical Build 10 install/runtime result is not yet claimed. This is the
concrete external gate allowed by the internal-beta completion definition.

## Candidate lineage and full-release boundary

- Repository: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`
- Shared branch: `codex/desktop-core-feature-wave-20260830`
- Latest Mobile release candidate:
  `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`, configuration
  `TestFlightBootstrap`, version/build `0.1 (10)`, bundle
  `app.ahoibrowser.AhoiBrowser`
- Current test/evidence head:
  `afa5cb64f79d348919001f27efea4db0f8c10bca`; commits `21de889` and
  `afa5cb6` fix account-binding and zone-scoping defects found by the real
  CloudKit journey, while `acae707` corrects its privacy assertion.
- Latest signed physical-device build: version/build `0.1 (9)`, configuration
  `CloudKitDevelopment`, source `afa5cb64f79d348919001f27efea4db0f8c10bca`,
  Team `248AJ5BN47`
- Latest processed internal TestFlight bootstrap: version/build `0.1 (10)`,
  source `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`, App Store Connect build
  `84cf0b2e-c1b9-4ff7-b104-8d99cce8ae9f`, status `Im Test` in
  `AhoiBrowser Intern`.
- Retained baseline candidate ID: `0.1-1-4113c14-debuglocal`; source
  `4113c14c1e0d21061a1c5927e25ab55663d547a7`
- Retained machine-readable receipt:
  [`artifacts/e2e/0.1-1-4113c14-debuglocal/manifest.json`](../artifacts/e2e/0.1-1-4113c14-debuglocal/manifest.json)
- Preserved pre-consolidation Mobile Git snapshot commit:
  `7bd492fa9d669868dceb1dbd91450cae5ae3bd3a`
- Source state: every result below names its exact source boundary. The
  retained baseline manifest still binds the older `4113c14` candidate; the
  newer completion-wave artifacts remain transient under `/private/tmp` and
  are not silently reclassified as release evidence.
- Claimed development stages: `SOURCE_COMPLETE`, `LOCAL_BUILD_PASS`,
  `UNIT_PASS`, `SIMULATOR_VISIBLE`, `DEVICE_VISIBLE`
- Candidate boundary: the real Development transport pass remains bound to
  signed build `0.1 (9)` at `afa5cb6`. Archive, export, upload, processing and
  internal distribution are proven for release build `0.1 (10)` at `ab2e709`.
  A physical readback on 2026-09-04 at 22:57 CEST still found developer build
  `0.1 (9)` on `Servusla` and no installed TestFlight app, so physical
  installation of build 10 is explicitly not proven.

This file is an internal-beta evidence contract and historical development
summary, not a public Mobile release receipt. The full-release status remains
`config/test-registry.json`. Every `MOB-USER-01` through `MOB-USER-15` entry
is release-critical `CU_E2E` with status `NOT_RUN`. Every `IOS-01` through
`IOS-15` entry remains release-critical `ASSISTED_E2E` with status `NOT_RUN`.

Source presence, a screenshot, an XCTest result, a simulator launch, an
archive, an upload or a processed TestFlight build can support a later
candidate chain, but none changes a registry status by itself. Candidate-bound
evidence must name the commit, generated project inputs, bundle/build version,
Team ID, bundle ID, profile, entitlements, device and OS build, and retained
artifacts.

## Required test ordering

Visible E2E on the exact build has priority whenever the requested behavior
can be exercised safely. Build, signing, installation and deterministic
fixture setup may run first because they are prerequisites to that visible
interaction. Programmatic tests run after the visible pass. A substantial code
or project adjustment requires the affected visible journey to be repeated
before relying on the later programmatic results.

If visible E2E is genuinely blocked or impossible, the exact blocker and the
last reachable boundary are retained. Independent programmatic tests still run
so their scope can be validated; they do not convert a blocked journey into a
pass.

## Mobile completion wave from 2026-08-31

Visible browser E2E ran first on exact product source `954643d`. Subsequent
physical CloudKit E2E found and drove the narrowly scoped sync fixes in
`21de889` and `afa5cb6`; the final transport journey below ran on that changed
product source before its focused programmatic regressions.

| Surface | Result | Retained evidence and boundary |
| --- | --- | --- |
| Cold external-URL lifecycle | `PASS`, 1/1 visible journey | `/private/tmp/ahoi-mobile-954643d-url-e2e-01.xcresult`, `.log` and `.mp4`; `testColdExternalURLIsVisiblyDeduplicatedWithoutDefaultBrowserGrant` passed on the iPhone 17 Pro, iOS 26.5 simulator. The repeated cold URL remained one claimed activation instead of creating a phantom third tab. This does not claim system-default routing without Apple's grant. |
| Harbor Deck and bottom address chrome | `PASS`, 5/5 visible journeys | `/private/tmp/ahoi-mobile-954643d-visible-suite-e2e-01.xcresult` and `.log`; document and nested scrolling, jitter rejection, intentional reverse travel, alert/file-presentation restoration and the system Reduce Motion journey passed. Hidden controls are removed from the compact hierarchy and the address, tab and menu slots remain stable. |
| Provider-free sync UI | `PASS`, 1/1 visible journey | The same six-test result bundle includes `testDebugLocalSyncOptInStaysLocalAndFailClosed`: local opt-in persists across restart, CloudKit/key actions remain unavailable, and opt-out cleans the local state. This is intentionally not Apple transport or cross-device evidence. |
| Physical `CloudKitDevelopment` build | `BUILD AND SIGNATURE PASS`; no install/runtime claim | `/private/tmp/ahoi-mobile-954643d-physical-cloudkit-build-01.xcresult`, `.log` and `/private/tmp/ahoi-mobile-954643d-physical-cloudkit-derived/Build/Products/CloudKitDevelopment-iphoneos/AhoiMobile.app`. `codesign --verify --deep --strict` passed for thin `arm64`; Team `248AJ5BN47`, exact app ID, Development push/iCloud environment, container `iCloud.app.ahoibrowser.AhoiBrowser`, CloudKit service and dedicated sync/command Keychain groups are present. The device-specific profile is valid through 2027-08-31 and contains only the connected iPhone `Servusla`; no Default Browser entitlement is present. |
| Real Development CloudKit smoke | `PASS`, 1/1 on the physical iPhone | Attempts 1-3 retained the earlier harness and locked-device boundaries. Attempt 4 (`95241c6`) exposed a real initial-account replay loop before upload and was fixed in `21de889`. Attempt 5 (`21de889`) completed transport but exposed an incorrect test assumption about `CKRecord.allKeys()`, fixed in `acae707`. Attempt 6 (`acae707`) showed that a historical deletion for another custom zone incorrectly gated the current provider, fixed in `afa5cb6`. Attempt 7 on exact source `afa5cb64f79d348919001f27efea4db0f8c10bca`, build `0.1 (9)`, passed active-record write/read, encrypted-field privacy, tombstone write/read and marker-authenticated cleanup on `Servusla`, iPhone 16 Pro Max, iOS 26.6.1: `/private/tmp/ahoi-mobile-afa5cb6-cloudkit-real-e2e-07.xcresult` and `.log`. |

The guarded real smoke wrote a UUID-scoped encrypted owner marker and active
record, read them back, published/read a tombstone, asserted that server records
exposed no plaintext user data, then deleted only marker-authored scope. This is
a real Development-container, same-account transport pass; it does not claim a
Production TestFlight or Mac-iPhone-iPad domain roundtrip.

## Exact-candidate visible development evidence from 2026-08-30

| Surface | Result | Retained evidence and boundary |
| --- | --- | --- |
| iPhone 17 Pro simulator, iOS 26.5 | `PASS`, 11/11 selected visible journeys across two result bundles | `ui-iphone-simulator.xcresult`, `ui-iphone-simulator-additional.xcresult`, their logs and `visible-iphone-simulator.png` in the retained candidate directory. Coverage includes local/private lifecycle, local-only sync opt-in, revocation scope, Harbor document/nested scroll collapse and reverse restore, interactive-presentation expansion, unsafe-scheme rejection, offline recovery and private Focus Voyage isolation. |
| Physical iPhone 16 Pro Max, iOS 26.6 | `PASS`, 7/7 selected visible journeys | `ui-iphone-device.xcresult` and log in the retained candidate directory. The app was Apple-Development-signed for Team `248AJ5BN47`. A separate retained `devicectl` receipt proves an HTTPS payload launched the app; it does not prove system-default routing or the visible duplicate-tab count. |
| iPad Pro 13-inch (M5) simulator, iOS 26.5 | `PASS`, 3/3 selected visible journeys | `ui-ipad-simulator.xcresult` and log in the retained candidate directory. Focus Voyage/core controls, Harbor Deck collapse/reverse restore and persistent Workspace Canvas passed. |
| Physical iPad | `BLOCKED` before install | The available iPad (6th generation) runs iPadOS 17.7.10 and cannot run the iOS/iPadOS 26 target. Simulator and programmatic evidence remains bounded to its own scope. |

Every file named above is hash-bound by the candidate manifest. No product
source changed after these visible runs.

## Historical and auxiliary visible development evidence from 2026-08-30

Simulator destinations:

- `AhoiBrowser E2E iPhone`, iOS 26.5 Simulator,
  `15C1EB97-A65C-4D93-842D-AB889339BE8D`
- `AhoiBrowser E2E iPad`, iOS 26.5 Simulator,
  `712F0CD5-D7F9-4B4B-8254-CFBCA4D138F9`

| Check | Result | Retained evidence and boundary |
| --- | --- | --- |
| Current broad iPhone DebugLocal regression | 12 total: 9 passed, 1 obsolete UI-harness ambiguity, 2 explicitly skipped | `/private/tmp/ahoi-mobile-ui-current-iphone-03.xcresult`, `/private/tmp/ahoi-mobile-ui-current-iphone-03.log`, `/private/tmp/ahoi-mobile-current-iphone-e2e-03.mp4`. Passed scope includes local-only sync opt-in/persistence/fail-closed state, private lifecycle, offline failure, unsafe-scheme rejection, Focus Voyage, document and nested-scroller Harbor collapse/reverse restore, presentation restore and private isolation. The one failure was a duplicate XCUI match for a single visible revocation-confirmation button, not a second product control; the original aggregate run remains truthfully non-green. |
| Corrected revocation confirmation | `PASS`, 1/1 focused visible check | `/private/tmp/ahoi-mobile-ui-current-iphone-corrected-01.xcresult`; the harness now selects the first matching accessibility element and the product confirmation journey passed. The same follow-up exposed a separate Settings-harness issue while trying to toggle Reduce Motion, which was corrected and rerun independently below. |
| Current system Reduce Motion journey | `PASS`, 1/1 focused visible check | `/private/tmp/ahoi-mobile-ui-current-iphone-reduce-motion-02.xcresult`, `/private/tmp/ahoi-mobile-ui-current-iphone-reduce-motion-02.log`, `/private/tmp/ahoi-mobile-current-iphone-reduce-motion-e2e-02.mp4`; the system setting was visibly enabled through its switch, the Harbor compact/expanded journey passed without spatial motion, and the setting was restored to off. |
| Current iPad DebugLocal regression | `PASS`, 3/3 focused visible checks | `/private/tmp/ahoi-mobile-ui-current-ipad-workspace-01.xcresult`, `/private/tmp/ahoi-mobile-ui-current-ipad-workspace-01.log`, `/private/tmp/ahoi-mobile-current-ipad-workspace-e2e-01.mp4`; Focus Voyage/core, Harbor collapse/reverse restore and persistent Workspace Canvas sidebar passed on the dedicated iPad simulator. |
| Earlier iPhone Harbor Deck document scroll and interactive restore | `PASS`, 2/2 focused visible checks | `/private/tmp/ahoi-mobile-harbor-stable-motion-20260830-01.mp4`; visibly covers expanded-to-compact document travel and restoration for browser interaction. It is a transient development recording, not candidate proof. |
| Earlier iPad Harbor Deck and Workspace Canvas | `PASS`, 2/2 focused UI tests | `/private/tmp/ahoi-mobile-harbor-stable-motion-ipad-01.xcresult`, `/private/tmp/ahoi-mobile-harbor-stable-motion-ipad-20260830-01.mp4`; `testHarborDeckCollapsesOnPageScrollAndRestoresOnReverseScroll` and `testInteractiveWebPresentationsExpandCollapsedHarborDeck` passed. Sidebar, fixture content and both expanded/compact states are retained. |
| Earlier iPhone document and nested-scroller policy | `PASS`, 2/2 focused UI tests | `/private/tmp/ahoi-mobile-scroll-e2e-fixed-02.xcresult`, `/private/tmp/ahoi-mobile-scroll-e2e-fixed-20260830-02.mp4`, `/private/tmp/ahoi-mobile-scroll-e2e-fixed-02.log`; both document and nested-container collapse/restore journeys passed. |
| Earlier system Reduce Motion journey | `PASS`, 1/1 focused UI test; an earlier 1/1 repeat also passed | `/private/tmp/ahoi-mobile-reduce-motion-crossfade-e2e-01.xcresult`, `/private/tmp/ahoi-mobile-reduce-motion-crossfade-e2e-20260830-01.mp4`, `/private/tmp/ahoi-mobile-reduce-motion-crossfade-e2e-01.log`; earlier repeat: `/private/tmp/ahoi-mobile-reduce-motion-e2e-02.xcresult`. The system switch was visibly enabled, the stable hierarchy crossfaded compact/expanded state, and the setting was restored to off after the run. |
| Earlier DebugLocal local-only sync persistence | `PASS`, 1/1 focused UI test | `/private/tmp/ahoi-mobile-sync-local-only-03.xcresult`, `/private/tmp/ahoi-mobile-sync-local-only-20260830-03.mp4`, `/private/tmp/ahoi-mobile-sync-local-only-test-03.log`; opt-in shows `Nur lokal`, transport/key actions remain disabled, the state persists across app restart, and cleanup turns it off. This is local UI/persistence evidence only, not CloudKit or cross-device sync. |
| Earlier combined iPhone UI/sync regression | `PASS`, 4/4 focused visible checks | `/private/tmp/ahoi-mobile-ui-sync-candidate-06.xcresult` and `/private/tmp/ahoi-mobile-ui-sync-candidate-06.log`; on the iPhone 17 Pro iOS 26.5 simulator, local-only sync opt-in/restart, document scrolling, nested-scroller collapse/restore and JavaScript/file-presentation restoration all passed in one 114.597-second test execution. This binds the related dirty-tree changes to one simulator build, but it is still not a clean release candidate or real CloudKit proof. |
| Earlier normal CloudKitDevelopment browser UI | `PASS`, 1/1 focused UI test after deterministic project regeneration; earlier 1/1 run also passed | Latest retained run: `/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-03.xcresult`, `/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-20260830-03.mp4`, `/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-03.log`. The scheme's implicit build products are under `CloudKitDevelopment-iphonesimulator`, and the regular app opens Focus Voyage and Harbor Deck rather than the inert E2E host. Earlier screenshot source: 02 artifacts in the still-image manifest. No CloudKit mutation was attempted. |
| Physical iPhone development smoke | Bounded visible smoke on iPhone 16 Pro Max, iOS 26.6 | A signed development app was installed. The initial CLI launch was rejected while the device was locked; after unlock, visible host device-management launch covered Ahoi cold start, HTTPS `example.com`, Browser Actions, a private tab and normal-tab restore with the private tab excluded after targeted Ahoi process termination. This is not a full candidate journey or sync proof. |
| Physical iPad | Blocked before run | The available iPad (6th generation) runs iPadOS 17.7.10 and cannot run the app's iOS/iPadOS 26 deployment target. Programmatic and simulator tests remain valid only in their stated scopes. |

The 12 retained UI stills, provenance and SHA-256 manifest are in
[`audit-evidence/2026-08-30-mobile-ui-sync/README.md`](audit-evidence/2026-08-30-mobile-ui-sync/README.md).
The historical large videos and result bundles in this section remain transient
under `/private/tmp`. The exact-candidate result bundles named in the preceding
section are retained with the candidate manifest.

The earlier physical-iPhone stills remain bounded separately:

- [`iphone-16-pro-max-01-cold-launch.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-01-cold-launch.png)
- [`iphone-16-pro-max-02-example-https.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-02-example-https.png)
- [`iphone-16-pro-max-03-browser-actions.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-03-browser-actions.png)
- [`iphone-16-pro-max-04-private-tab.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-04-private-tab.png)
- [`iphone-16-pro-max-05-normal-restore.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-05-normal-restore.png)

None of the bounded visible results above changes a `MOB-USER-*` or `IOS-*`
registry status.

## Programmatic development evidence

These checks followed reachable visible E2E boundaries or ran after the real
CloudKit/device journey had failed closed at an external prerequisite.

### Current test/evidence head `afa5cb6`

| Check | Result | Retained evidence and boundary |
| --- | --- | --- |
| Focused CloudKit recovery regression | `PASS`, 9/9 | `/private/tmp/ahoi-mobile-afa5cb6-focused-cloudkit-01.xcresult` and `.log`; account/zone recovery and persistence-failure contracts ran after the green physical CloudKit journey. |
| Complete `AhoiMobileCoreTests` DebugLocal suite | `PASS`: 115 passed, 0 failed, 2 expected entitlement skips out of 117 | `/private/tmp/ahoi-mobile-3c1af83-core-full-01.xcresult` and `.log`. The new isolated-world source-telemetry contract is included; the two skips still require real CloudKit entitlements. |
| Provider-free CloudKit/security package | `PASS`, 36/36 | `/private/tmp/ahoi-mobile-3c1af83-cloudkit-package-01.log`; offline crypto, convergence, account-transition and cleanup contracts, not Apple transport. |
| Mobile repository contracts | `PASS`, 20/20 | `/private/tmp/ahoi-mobile-3c1af83-repository-contracts-01.log`. |
| Development signing contracts | `PASS`, 5/5 | `/private/tmp/ahoi-mobile-3c1af83-development-signing-01.log`; validates the separated build/signing modes without claiming an Apple distribution grant. |
| Swift parse, shell syntax and Mobile line budget | `PASS`: 94/94 Swift files parsed; maximum exactly 800 lines | `/private/tmp/ahoi-mobile-3c1af83-swift-parse-01.log` and `/private/tmp/ahoi-mobile-3c1af83-line-budget-01.log`; `release-preflight.sh` also passed `bash -n` and the Mobile-scoped diff passed `git diff --check`. |

### Exact candidate `4113c14`

| Check | Result | Retained evidence and boundary |
| --- | --- | --- |
| Complete `AhoiMobileCoreTests` DebugLocal suite | `PASS`: 111 passed, 0 failed, 2 expected entitlement skips out of 113 | `unit-core.xcresult` and `unit-core.log` in the retained candidate directory. The two skipped tests require real CloudKit entitlements and remain skips. |
| Mobile repository contracts | `PASS`, 20/20 | `repository-tests.log` in the retained candidate directory. |
| Provider-free CloudKit/security package | `PASS`, 36/36 | `cloudkit-package-tests.log` in the retained candidate directory. This verifies offline model, crypto, convergence and safety contracts, not Apple transport. |
| Swift parse, shell syntax and 800-line limit | `PASS`: 94/94 Swift files parsed; Mobile maximum exactly 800 lines | `static-checks.log` in the retained candidate directory. |
| DebugLocal static analysis | `ANALYZE SUCCEEDED` | `analyze-debuglocal.log` in the retained candidate directory; one AppIntents metadata warning only, no source/analyzer errors. |
| Deterministic project generation | `PASS`: two XcodeGen 2.46.0 generations matched and left Git clean | `xcodegen-determinism.log`; project path/content SHA-256 `a5dc0cad689c623b6e4013b59b93099115d64a43024d7a1059b604eeee0e134e`, six-scheme SHA-256 `dec669a5a8ebd8b499bf9d750aa26f44ff31bca8864a583d58cee5f72eaced65`. |
| DCO focus range | `PASS`, 3/3 non-merge focus commits | `dco-focus-range.log`; exclusive baseline `7bd492fa9d669868dceb1dbd91450cae5ae3bd3a`, inclusive head `4113c14c1e0d21061a1c5927e25ab55663d547a7`. The log separately records that the preserved historical side lineage contains one older unsigned commit; it is not silently reclassified. |
| Redacted secret scan | `PASS`, no leaks found in all three scopes | `gitleaks-mobile.log`; Gitleaks 8.30.1 scanned exact-commit `apps/AhoiMobile`, exact-commit `spikes/cloudkit` and the retained candidate evidence with 100% redaction. An untracked scanner from parallel Desktop work was absent from the candidate and is not used for this claim. |
| Evidence validator | `PASS` against a detached source worktree | The manifest, paths, hashes, source commit, claimed stages, devices and result counts passed `verify_mobile_release_evidence.py`. A second Git-roundtripped validation is required after the evidence commit. |

The hashes and boundaries for these four post-E2E gates are indexed in
`post-e2e-gates.json` beside the candidate manifest.

### Earlier and auxiliary programmatic evidence

| Check | Result | Retained evidence and boundary |
| --- | --- | --- |
| Current full `AhoiMobileCoreTests` DebugLocal run | 113 total: 111 passed, 0 failed, 2 expected entitlement skips | `/private/tmp/ahoi-mobile-full-core-01.xcresult`, `/private/tmp/ahoi-mobile-full-core-01.log`. `testCloudKitProviderQueuesAllowedRecordWithoutNetworkRoundTrip` and `testCloudKitProviderStopsSensitiveRecordBeforeOutbox` require a real CloudKit entitlement and remain skips, not passes. |
| Current focused Mobile Core contracts | `PASS`, 49/49 | `/private/tmp/ahoi-mobile-focused-core-01.xcresult`, `/private/tmp/ahoi-mobile-focused-core-01.log`; selected browser presentation, policy, sync, crypto and key-lifecycle scope, not real transport proof. |
| First persistence-failure harness attempt | Historical `FAILED`: 4 sync tests passed and 3 provider-construction tests crashed | `/private/tmp/ahoi-mobile-sync-persistence-focused-03.xcresult` and `.log`; those tests constructed `CKContainer` inside the intentionally provider-free `DebugLocal` host and trapped on its missing entitlement. The provider-independent failure seam and the current 113-test run now pass, but this original red artifact remains part of the audit trail. |
| Current CloudKit/security Swift package | `PASS`, 36/36 | `/private/tmp/ahoi-mobile-cloudkit-swift-test-current-01.log`; offline model, convergence and security contracts only. |
| Scroll policy/bridge focus | 6 executed, 0 failures | `/private/tmp/ahoi-mobile-scroll-programmatic-01.xcresult`; validates reducer/decoder contracts, not pixels. |
| Motion contract focus | 1 executed, 0 failures | `/private/tmp/ahoi-mobile-motion-programmatic-01.xcresult`; validates the approved timing window, not the visible transition by itself. |
| Mobile repository contracts | `PASS`, 20/20 | All `test_mobile_*.py` repository contracts passed. The separate synthetic five-mode preflight matrix also passed 5/5 positive and 5/5 expected-negative cases; its 11 focused repository fixtures passed under Python 3.14. No distribution or entitlement grant is inferred. Evidence: `/private/tmp/ahoi-mobile-static-gates-20260830.KiOFxO/` and `/private/tmp/ahoi-mobile-preflight.cqoUpM/`. |
| Swift parse and static gate | `PASS`, 94/94 Swift files; 0 parse failures | 78 source and 16 test files parsed. `bash -n` also passed for `release-preflight.sh`. Evidence: `/private/tmp/ahoi-mobile-static-gates-20260830.KiOFxO/gate-verification-summary.log`. |
| DebugLocal static analysis | `ANALYZE SUCCEEDED` | `/private/tmp/ahoi-mobile-analyze-debuglocal-02.log`; no source/analyzer errors. Two Xcode AppIntents metadata warnings remain build-tool warnings rather than analyzer failures. The earlier `-parallelizeTargets NO` invocation failed before building because the harness supplied an invalid flag/value pair. |
| Deterministic XcodeGen project generation | 2 successful no-op generations with identical combined project/scheme SHA-256 `49e503d5da232d2067094999b653096c9e5b923f7a8b8ebd561273a7f7f42643` | Generated-project determinism only. It does not make the development runs a distribution candidate. |
| 800-line convention | All Mobile Swift sources/tests are at or below 800 lines; maximum exactly 800 | `CompanionStore.swift` is exactly at the limit. This Mobile evidence does not claim unrelated Desktop/shared line-budget findings as green. |

## CloudKit and sync truth

Development signing and the real target container now pass the complete
same-account active-record/tombstone transport smoke on a physical iPhone.
An encrypted Mac-iPhone/iPad domain roundtrip and end-to-end Keychain lifecycle
have not completed.

| Gate attempt | Observed result | Meaning |
| --- | --- | --- |
| Unsigned harness compile | `PASS` with real mutation off | `/private/tmp/ahoi-mobile-cloudkit-harness-compile-04.log`; source compile only. |
| Simulator `AhoiMobile-CloudKitE2E` build-for-testing | `PASS` with `AHOI_CLOUDKIT_E2E_REAL_MUTATION_OPT_IN=NO` | `/private/tmp/ahoi-mobile-cloudkit-e2e-mutation-off-build-01.log`; app code-sign entitlements are `{}`. The simulated `.xcent` contains requested container/service/environment values but no real team identifier, so this is not an entitled candidate. |
| Simulator-hosted queue/deny tests | Expected fail-closed boundary: 2 tests stopped before provider/CloudKit construction | `/private/tmp/ahoi-mobile-cloudkit-hosted-mutation-off-01.xcresult` and `.log`; both report missing `com.apple.developer.team-identifier`. The XCTest invocation is red by design at the missing entitlement and is negative safety evidence, not a green CloudKit suite. |
| Physical iPhone mutation-off build-for-testing | Blocked in provisioning before compile/install/test | `/private/tmp/ahoi-mobile-cloudkit-e2e-physical-preflight-01.log`; the profile does not support `iCloud.app.ahoibrowser.AhoiBrowser`, lacks `com.apple.developer.icloud-container-environment`, and does not match the requested container identifiers. No portal mutation or CloudKit call occurred. |
| Current physical Development signing | `PASS` for build, signature and provisioning | The `0.1 (6)` `954643d` candidate is correctly signed for Team `248AJ5BN47`, the target Development container and `Servusla`. This supersedes the older provisioning absence for Development only; it is not a transport or distribution pass. |
| Real mutation attempt 1 | Harness failure before any CloudKit write; corrected | `/private/tmp/ahoi-mobile-954643d-cloudkit-real-e2e-01.xcresult` and `.log`; XCTest rejected observer registration off the main thread. Commit `469d00d` removed that invalid boundary. |
| Real mutation attempt 2 | Expected account safety gate reached; harness then corrected | `/private/tmp/ahoi-mobile-444e995-cloudkit-real-e2e-02.xcresult` and `.log`; `accountTransitionRequiresConfirmation` stopped local upload, and marker-first cleanup refused deletion because no marker existed. Commit `3c1af83` explicitly confirms only the empty synthetic E2E scope and uses async idempotent teardown. |
| Real mutation attempt 3 | `BLOCKED` at locked device before launch | `/private/tmp/ahoi-mobile-3c1af83-cloudkit-real-e2e-03.xcresult` and `.log`; build/sign completed, but Xcode waited at `Unlock Servusla to Continue`. No current-head test or CloudKit mutation started. |
| Real mutation attempt 4 | Product failure before first upload; corrected | `/private/tmp/ahoi-mobile-95241c6-cloudkit-real-e2e-04.xcresult` and `.log`; fresh-engine `.signIn` replay reopened the account confirmation gate. `21de889` now distinguishes initial/same-account binding from a real account switch. |
| Real mutation attempt 5 | Transport completed; test assertion corrected | `/private/tmp/ahoi-mobile-21de889-cloudkit-real-e2e-05.xcresult` and `.log`; active and tombstone records reached server readback, but the privacy assertion failed because `CKRecord.allKeys()` also reports encrypted field names. `acae707` subtracts `encryptedValues` keys before checking clear metadata. |
| Real mutation attempt 6 | Foreign-zone deletion isolation failure; corrected | `/private/tmp/ahoi-mobile-acae707-cloudkit-real-e2e-06.xcresult` and `.log`; the fresh provider saw the preceding UUID scope's deleted zone and incorrectly required recovery. `afa5cb6` now gates only deletion of its configured zone. |
| Real mutation attempt 7 | `PASS`, 1/1 on physical iPhone | `/private/tmp/ahoi-mobile-afa5cb6-cloudkit-real-e2e-07.xcresult` and `.log`; exact source `afa5cb64f79d348919001f27efea4db0f8c10bca`, build `0.1 (9)`, Team `248AJ5BN47`, `Servusla` iPhone 16 Pro Max/iOS 26.6.1, fresh token `265572B5-A6EC-4A81-97C0-062DBC52DFF9`. Active/tombstone write and readback, encrypted-field privacy and authenticated cleanup passed in 10.830 seconds. |

The dedicated E2E harness remains fail closed: a real run additionally needs
`AHOI_CLOUDKIT_E2E_REAL_MUTATION_OPT_IN=YES`, a unique run token, exact bundle,
team, build, container and SecTask entitlement validation, a fresh read-only
scope probe, UUID-scoped synthetic records and marker-first cleanup. These
guards must not be weakened to make a simulator test green.

## Source seams awaiting candidate-bound runtime evidence

- system `WebPage`/`WebView` rendering with WebKit-owned networking, TLS,
  dialogs, permissions and website storage;
- persistent normal `WKWebsiteDataStore.default()` plus one shared
  `WKWebsiteDataStore.nonPersistent()` for the running private session;
- `WKDownload` transfers tied to the initiating request/data store, with
  filename sanitization, collision-safe destination, progress, cancellation,
  Quick Look and sharing;
- origin-labelled camera, microphone, motion, JavaScript, file-input and
  external-scheme consent surfaces;
- browser-session persistence that excludes private tabs and serializes writes
  by revision;
- tracked Privacy Manifests declaring no tracking and no tracking domains,
  plus browsing history, search history, other user content and device ID as
  unlinked, non-tracking data used for app functionality, and UserDefaults
  reason `CA92.1`;
- local-first encrypted CloudKit records, a durable fetched-envelope inbox and
  event-driven CKSyncEngine transport without a foreground polling loop.

These bullets describe reviewable source boundaries only.

## Mobile Computer Use journeys

| Journey | Status | Candidate-bound evidence required |
| --- | --- | --- |
| `MOB-USER-01` cold start, URL and HTTPS | `NOT_RUN` | Installed candidate cold launch, URL entry, HTTPS navigation and visible origin on supported iPhone. |
| `MOB-USER-02` search and navigation | `NOT_RUN` | Configured provider, result navigation, Back/Forward/Reload/Stop and no duplicate loads. |
| `MOB-USER-03` tab lifecycle and restore | `NOT_RUN` | Create, reorder, rename, close/undo, terminate and cold-restore normal tabs. |
| `MOB-USER-04` workspace save/move | `NOT_RUN` | Save and move a live page, then reopen it through tree and search. |
| `MOB-USER-05` private separation | `NOT_RUN` | Normal/private cookie and storage probes plus process-death proof that private tabs never enter session, history, search, sync or device tabs. |
| `MOB-USER-06` default-browser callback | `NOT_RUN` | Apple-entitled system default selection and external HTTP(S) callbacks into the exact signed app. |
| `MOB-USER-07` upload/download/share/popup | `NOT_RUN` | Real file provider upload, normal/private authenticated downloads, progress/cancel, Quick Look/share and popup attribution. |
| `MOB-USER-08` permissions/dialogs/external app | `NOT_RUN` | Origin-labelled allow/deny/cancel across main/subframes, JavaScript/file dialogs and external-app confirmation. |
| `MOB-USER-09` rotation and accessibility | `NOT_RUN` | Portrait/landscape, Dynamic Type, VoiceOver, high contrast, Reduce Motion and Reduce Transparency on device. |
| `MOB-USER-10` iPad interaction | `NOT_RUN` | Real iPad sidebar, multitasking, keyboard, pointer, rotation, reorder and workspace gestures. |
| `MOB-USER-11` failure and restore | `NOT_RUN` | Offline/TLS/WebContent failure, background/termination, memory pressure, incomplete download and deterministic recovery. |
| `MOB-USER-12` cross-device tabs | `NOT_RUN` | Final entitled Mac-iPhone/iPad CloudKit and Keychain roundtrip, offline queue, conflict, revoke and private-data exclusion. |
| `MOB-USER-13` unsafe actions | `NOT_RUN` | Reject local/script/credential/unknown schemes and verify labelled confirmation for permitted external schemes. |
| `MOB-USER-14` 1/5/20 tabs | `NOT_RUN` | Normal/private scale, switching, reorder, discard, persistence and absence of phantom tabs. |
| `MOB-USER-15` visual consistency | `NOT_RUN` | iPhone/iPad, normal/private, light/dark, tint/fallback and accessibility appearance matrix. |

## Physical cross-device assisted journeys

| Journey | Status | Required boundary |
| --- | --- | --- |
| `IOS-01` | `NOT_RUN` | Browse workspaces, tree, tabs and history on real iPhone/iPad. |
| `IOS-02` | `NOT_RUN` | Create saved page and folder. |
| `IOS-03` | `NOT_RUN` | Move, rename and delete tree nodes with Mac confirmation. |
| `IOS-04` | `NOT_RUN` | Open link through the selected system default browser. |
| `IOS-05` | `NOT_RUN` | Send link to a selected Mac and workspace. |
| `IOS-06` | `NOT_RUN` | Remotely open one normal Mac tab. |
| `IOS-07` | `NOT_RUN` | Remotely focus one normal Mac tab. |
| `IOS-08` | `NOT_RUN` | Remotely close one normal Mac tab after confirmation. |
| `IOS-09` | `NOT_RUN` | Offline command and TTL behavior. |
| `IOS-10` | `NOT_RUN` | Queued/delivered/executed/failed status progression. |
| `IOS-11` | `NOT_RUN` | Replay rejection across restart. |
| `IOS-12` | `NOT_RUN` | Wrong target and invalid signature rejection. |
| `IOS-13` | `NOT_RUN` | Device approval and revocation. |
| `IOS-14` | `NOT_RUN` | Private tabs remain invisible and uncontrollable. |
| `IOS-15` | `NOT_RUN` | Reject arbitrary schemes, shell commands and bulk actions. |

## External gate mapping

| Gate | State | Closure evidence |
| --- | --- | --- |
| `ios-final-bundle-team-profile` | TestFlight bootstrap processed and internally distributed | Team `248AJ5BN47`, bundle `app.ahoibrowser.AhoiBrowser` and the exact container are proven in signed Development build `0.1 (9)` and processed internal TestFlight bootstrap `0.1 (10)` from `ab2e709`. Physical TestFlight installation and the post-grant profile remain open. |
| `ios-managed-default-browser-entitlement` | `blocked-entitlement` | Both Default Web Browser and Browser App Installation showed `No Requests`; Apple grant, profile attachment and the physical system-default journey remain absent. |
| `ios-cloudkit-keychain-capabilities` | Development transport passed; cross-device runtime open | The signed `0.1 (9)` Development build passed real active/tombstone transport and encrypted-field privacy in `iCloud.app.ahoibrowser.AhoiBrowser`. An entitled Mac counterpart, Production TestFlight transport, Mac-iPhone/iPad domain roundtrip and operational key rotation/revocation evidence remain absent. |
| `ios-physical-device-journeys` | `required-user-assistance` | The bounded browser smoke and real Development CloudKit journey pass on `Servusla`. A 2026-09-04 readback found only developer build `0.1 (9)` and no TestFlight app. A supported physical iPad, physical build-10 installation and the full Mobile/IOS journey registry remain open. |
| `ios-app-store-connect-testflight` | `internal-testflight-ready` | App Store Connect app `6808754773` has processed build `0.1 (10)` (`84cf0b2e-c1b9-4ff7-b104-8d99cce8ae9f`) in internal group `AhoiBrowser Intern`, with `christian@vossmedien.de` invited and status `Im Test`. Physical install, external Beta Review/public link and public metadata gates remain open. |

This state records only the bounded development results above. It does not
claim a complete physical-device matrix, encrypted cross-device roundtrip,
default-browser entitlement, build-10 TestFlight installation or Mobile
public release.
