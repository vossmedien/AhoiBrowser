# Mobile owner changes: compile-only build-for-testing

Snapshot of `c6958dfd` (`git archive` of `apps/AhoiMobile`, `spikes/cloudkit`,
`fixtures/sync-conformance` and the overlay sync testdata) built with
`xcodebuild build-for-testing`, DebugLocal, generic iOS Simulator, `-jobs 2`,
no code signing. **TEST BUILD SUCCEEDED**; no warnings in the changed files.
The owner `build.lock` was held and released. No simulator was booted and no
test was executed (test pause).

This covers the compaction lease-fault handling and regressions, Crest
132/138/140 Swift runners, the Crest 136 Files spike with its import guard,
and the Crest 126 provenance-bound merge-root projection. It proves they
typecheck and link as a test bundle only; their behavior, 138 RED/GREEN, the
Crest 122 comparison and any visible or CloudKit evidence remain open.
Receipt with source/binary hashes: [receipt.json](receipt.json).

## Focused test run on the same bundle (approved 28 September)

`test-without-building` on the owner simulator CE3513BF, one job, no parallel
testing: **54/54 passed, 0 failed, 0 skipped** across
`CompanionWorkspaceRetentionTests`, `SyncMergeConformanceTests`,
`CompanionWorkspaceMergeTests`, `CompanionTreeReorderingTests` and
`MobileWebExtensionRuntimeTests`. This includes the compaction lease-fault
regressions, Crest 132 sequences, 138 marker-collision frames (GREEN for the
Crest 126 fix), 140 domain groups, the 136 Files spike and the merge-root
position rule. The simulator was shut down afterwards. Swift 138 RED on the
pre-fix parent `54c2901` is still to run; Crest 144 postdates this bundle.
Per-test list: [focused-tests.txt](focused-tests.txt).
