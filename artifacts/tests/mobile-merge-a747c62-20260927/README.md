# Focused Mobile merge verification — 27 September 2026

Exact clean source: `a747c62a74219c807b0bcf232bab29e5586248fc`.
Xcode 27, DebugLocal, arm64 iPhone 17 / iOS 27 Simulator
`CE3513BF-61D7-4718-99A0-29D4111D6855`; one build job, no parallel tests.
No Chromium build, installed Desktop app, real CloudKit peer or paid API used.

## Results

- Final GREEN: **27 passed, zero failed, zero skipped**, xcodebuild exit 0.
  Merge class 19; tree reordering 2; saved-page behavior 4; conformance 2.
- The conformance runner consumed the pinned **140** shared vectors. The
  companion coverage test confirms every vector entity is covered, and the
  runner iterates each covered case. Fixture SHA-256 before and after:
  `da09b4c1feb6247c30975376642893eb95a56a4ce0a3f3bcf059327121efacf8`.
- Crest 098 RED: remove only the undo guard in the isolated test worktree,
  leaving all tests unchanged. The three refusal methods fail; the unrelated
  root-addition control passes (**3 failed, 1 passed**, exit 65). The failing
  snapshots show the unexpected undo mutation in memory and after reloading
  the same store. The exact guard-removal diff is `negative-098.patch`.
- The guard was reapplied and its bytes matched Git exactly. The clean final
  GREEN reran all four selected classes. The shared product worktree was never
  changed for the negative control.

The first attempt on `3d61893` failed to compile the new MainActor test because
its async fixture captured non-Sendable XCTestCase state. `a747c62` makes the
stateless helpers static. The first `a747c62` attempt then exposed an incomplete
sparse checkout: Xcode's bundled `sync_wire_v3.json` resource was absent. The
declared overlay testdata path was included before either successful run.
Neither preparation failure is presented as a product/runtime result.

## Evidence and limits

`receipt.json` pins source, fixture, toolchain and final framework/test-binary
hashes. `green-run.json` retains the exact command and source binding. The
summary JSON files are extracted from Xcode's result bundles and retain the
test identities. Full result bundles/logs remain at their recorded persistent
paths under `.work/agent-queue/`; these are unit-test evidence, not browser E2E.

The blank WebPage identity/URL/selection regression does not prove a loaded
website, native authentication or visible user journey. Root-end ordering,
retention/compaction behavior and full apply/mutation convergence remain open.
C++ execution of the 140 vectors and of `61cab96`'s native parent-fallback
regressions is NOT_RUN. No full Master-DoD, installed-browser, real-device or
cross-device Sync acceptance is claimed.
