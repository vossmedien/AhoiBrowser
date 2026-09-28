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
