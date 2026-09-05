# Mobile Bookmark-v2 and Shared-Tab reader preparation

Date: 2026-09-05. Final Mobile source:
`313e3518c1d6daa8ee4d6a3ae25a01ec99a0d39b`, DebugLocal **0.1 (15)**.

**Local Mobile package verified. Full cross-device sync is not accepted.**
The separate Bookmark Library and matching Swift Bookmark-v2 domain work through
the existing repository/crypto/transport boundaries. Shared normal-tab v3 is
read/merge preparation only: writers, live import activation and recovery upload
remain gated pending ADR 0008's capability, migration and compatibility contract.

## Candidate and test order

The clean detached build snapshot is
`/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/repo`. The build succeeded with
Xcode 26.6 / 17F113 and Swift 6 strict concurrency; the ad-hoc simulator signature,
source, project and intrinsic app identity are bound by
[candidate-receipt.json](candidate-receipt.json). The installed TestFlight build
and native `/Applications/AhoiBrowser.app` were not replaced by this package.

First the visible UI journey ran on the exact candidate. Only afterward did the
focused Core suites, shared Swift package and repository contract execute.
After the final provider-consent correction, the visible journey was repeated
on source `313e351` before rerunning the relevant programmatic checks.

| Check | Result |
| --- | --- |
| `build-for-testing`, DebugLocal 0.1 (15) | Exit 0, test build succeeded |
| Bookmark Library visible journey | 1 passed, 0 failed/skipped; exit 0 |
| Focused Mobile Core | 70 passed, 0 failed, 2 entitlement skips; 72 discovered; exit 0 |
| Shared `AhoiCloudKitSpike` Swift package | 36 passed, 0 failed/skipped; exit 0 |
| `test_mobile_sync_event_contract` | 2 passed; exit 0 |
| Owned Swift line budget / whitespace | At most 800 lines per file; diff check clean |
| Redacted changed-source secret scan | One reviewed false positive; no actual credential found |

## Visible Mobile behavior

`MobileBookmarkRealE2EUITests/testBookmarkFolderPersistsAndOpensWebPage` ran on
the dedicated iPhone 17 Pro / iOS 26.5 Simulator
`CAE7F82B-52D2-4607-992C-EDF40C323DE3`. It opened the separate Library, verified
that provider-free DebugLocal cannot enable CloudKit Bookmark sync, created a
folder and bookmark through the real UI, restarted the app, found the same
identities and folder linkage, opened the saved local HTTPS page in WebKit,
then explicitly deleted the newly created test folder and its contents.

Screenshots were inspected directly:
[Bookmark Library](bookmark-library.png), [opened page](bookmark-opened.png).
This is visible XCTest UI evidence, not a separate manual Computer-Use pass.
The pre-existing HTTPS fixture's simulator CA and normal TLS validation were
retained; no new user-keychain trust or TLS exception was installed.

An earlier run on `4647f5b` was red only at cleanup: SwiftUI exposed the one
rendered delete-confirmation action as nested AX buttons with the same ID.
The screenshot/video and hierarchy were examined; the test now targets the
observed confirmation sheet and its first matching action. Product deletion
semantics were not changed to accommodate the test. Both later `da7315d` and
`313e351` journeys passed including deletion. The old failed run remains red
in the retained artifacts. Its separately identified synthetic folder was not
silently treated as cleaned by those later runs.

## Verified domain and boundaries

- Bookmark-v2 uses the single canonical fixture from `ae38ed7` at
  `overlay/chromium/src/ahoi/browser/sync/testdata/bookmark_wire_v2.json`.
  SHA-256: `b09a5f898a07351f4cd80a68521dffadb05e21abb9799c0d86d61672d244e443`.
  The test bundle references that file directly, not a duplicate fixture.
- Native URL metadata, positive Int64 Windows-epoch creation dates including
  pre-1970 values, root/parent exclusivity, exact field clocks and byte limits
  are covered. Workspace saved pages remain a separate domain.
- Legacy snapshots retain their old domains and decode an absent Bookmark
  collection compatibly. CRUD, rooted hierarchy, child-before-parent retention,
  cycle/invalid-parent rejection, atomic location moves, field merge and delayed
  live-record rejection after deletion are covered.
- The two-repository relay uses actual Swift domain/wire/AES-GCM/CKRecord codec
  implementations with a known fixture key and the existing DEBUG transport.
  It proves its simulated bidirectional Bookmark and persistence behavior,
  not real CloudKit delivery or execution of Chromium's adapter.
- Portable v3 tree/presence readers preserve `is_temporary`/`tree_node_id` and
  their real clocks. Later v2 updates cannot replace those fields in either
  merge direction. V1/v2 keep their original layouts; new legacy-field clocks
  and creator badges are not synthesized. New writers cannot be enabled by a
  generic schema-version bump; encode/upload/recovery reject v3.
- After the initial 30-test pass, targeted review found that a Bridge-only
  Bookmark opt-in could be bypassed by real-provider CK-state rehydration.
  `313e351` adds the same runtime category check to direct enqueue, seeding,
  imported winners, final upload and physical-delete recovery. An unapproved
  pending-send intent is removed without deleting encrypted local records or
  clearing corruption quarantine. Explicit later approval permits rehydration.
  Targeted tests cover default/revoked consent, all DEBUG transport write entry
  points and v3 rejection even with Bookmark approval. This is not a claim that
  an entitled real-provider recovery was executed on the simulator.

The two expected Core skips are
`CompanionCoreTests/testCloudKitProviderQueuesAllowedRecordWithoutNetworkRoundTrip`
and `testCloudKitProviderStopsSensitiveRecordBeforeOutbox`: CKSyncEngine requires
an entitled Apple host. They are recorded as skips, not passes.

The redacted Gitleaks scan returned exit 1 for exactly one generic-api-key
match: `BookmarkModels.swift:97`, the source expression
`guard !sortKey.isEmpty, sortKey.utf8.count <= 1_024`. This is executable length
validation, not a credential literal. The reviewed false positive and raw scan
result are retained; no scanner-wide suppression or source change hid it.

## Remaining work, not ownership waiting

- Real native Chromium Bookmark adapter integration, matching built C++ tests
  and candidate-bound Desktop–Mobile Bookmark roundtrip remain with the
  respective owners. Neither fixture bytes nor this Swift relay proves them.
- Shared-normal-tab live propagation, automatic logical rows, origin badges,
  full save/unsave/close wiring and v3 activation still require the exact
  capability/legacy/nonportable agreement and matching-client gate in ADR 0008.
  Swift ownership is granted; no additional ownership acknowledgment is needed.
- Real CloudKit Development/Production and cross-device Keychain bootstrap are
  separate gates. The previous My-Mac attempt did not execute its test body and
  its runtime slot was explicitly returned. No new My-Mac host was started here.
- Build 15 is a local DebugLocal candidate, not a TestFlight upload or release.
  The current internal TestFlight distribution is unchanged.

## Retained artifacts

Logs, build/XCTest results, original failed attempts and screenshots are retained
under `artifacts/e2e/mobile-bookmark-wire2-313e351/`. The original/copy hashes
were compared. Hashes below use the existing domain-prefixed
`tools/mobile_evidence_artifacts.py:sha256_path` algorithm.

| Artifact | SHA-256 |
| --- | --- |
| `consent15-candidate-receipt.json` | `841ea56302e9e920395e490181ed02576400b942bf69d9b7dc821cdb678eb356` |
| `bookmark-consent-build-313e351.xcresult` | `e740ec8192b51a7e5651fd6e5740cd00b0b8eadf668f7609a1f4ecffe7f30b0b` |
| `consent15-bookmark-ui.xcresult` | `046e13143125c364bd70cdd94694d37dcc58655d30809e04dcaef5a049ada790` |
| `consent15-core.xcresult` | `b116155316d9f9c84b9bd326fd39c2584e36e27f3d440a717394a4ce0f02e9bd` |
| `consent15-shared-package.log` | `51d6ef981f7643191edf3e36e21403f0e041a9ec3b81f14f04909edfb5b8757e` |
| `consent15-repository.log` | `374e211b1bf9eb6c8af60b419afd29b5df6316090d858fed56fd1513656f477f` |

No own build/test remains active. The dedicated Mobile test simulator was found
shut down at final readback; other projects' processes/devices were not stopped.
The one clean source/build snapshot remains for incremental integration of the
pending matching-client package, not a competing development branch.
