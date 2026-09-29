# Crest adoption A6 and C1: Swift tests (29 September 2026)

Candidate: `4f7b677d` (branch `worktree-agent-abc2d37f1c3746507`, base
`20a8d419`); the tested working tree was byte-identical to that commit for
`apps/AhoiMobile`, `spikes/cloudkit` and the overlay sync testdata.

Headless only: `xcodebuild build-for-testing` then `test-without-building`,
scheme `AhoiMobile`, configuration `DebugLocal`, `-jobs 4`, destination
`platform=iOS Simulator,id=CE3513BF-61D7-4718-99A0-29D4111D6855` (owner
simulator, iPhone 17, iOS 27.0), own `-derivedDataPath` under the worktree's
ignored `.work/`, `-parallel-testing-enabled NO`, `CODE_SIGNING_ALLOWED=NO`.
Xcode 27.0 (27A266a). No Simulator.app, no GUI input; the simulator was
Shutdown afterwards. **TEST BUILD SUCCEEDED** without warnings in the changed
files. Test binary SHA-256
`267fd0e4f0bac6e5c1408106fb3f117a17518baae7fca13553822aa17a84424b`.

## Focused run: 111/111 passed, 0 failed, 0 skipped

| Class | Result |
| --- | --- |
| `CompanionWorkspaceRetentionTests` (pending since 28 Sep) | 5/5 |
| `SyncMergeConformanceTests` (pending) | 10/10 |
| `CompanionWorkspaceMergeTests` (pending) | 24/24 |
| `MobileWebExtensionRuntimeTests` (pending) | 5/5 |
| `SyncRecordTextFittingTests` (new, A6) | 7/7 |
| `SeparatedWorkspaceStateTests` (new, C1) | 4/4 |
| `SeparatedWorkspaceSyncTests` | 20/20 |
| `SharedTabWireReadTests` | 6/6 |
| `UnifiedSyncWireContractTests` | 5/5 |
| `MobileBrowserCoreTests` | 25/25 |

Per-test list: [focused-tests.txt](focused-tests.txt).

## Whole `AhoiMobileCoreTests` target on the same bundle

371 executed, 0 failures, 2 skipped (the two existing
`CompanionCoreTests` CloudKit-provider cases: "CKSyncEngine requires an
entitled Apple test target").

## Not covered

- The desktop C++ half of A6 (`sync_record_limits_unittest.cc` in
  `ahoi_sync_unittests`) is source-only: no Chromium build ran here.
- No visible journey, installed candidate or real CloudKit peer.
- No RED run of the new tests on the parent commit (they use the new API).
