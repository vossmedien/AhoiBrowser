# 009 – Shared merge conformance vectors for C++ and Swift (H1)

Status: ready
Owner lanes: desktop/sync (C++ runner, `BUILD.gn`), mobile/sync (Swift runner)
Base: `119548a`

## Purpose

The format-3 model, codec and merge exist twice, in C++ and in Swift. The
existing goldens pin bytes, not merge results. `fixtures/sync-conformance/merge_v3.json`
has 132 vectors: 72 hand-structured cases and 60 seeded random concurrent edits.
Each vector holds an existing payload, an incoming payload, the expected
decision and the expected merged payload. Both languages run the same file.

Expectations come from `tools/sync_conformance/merge_model.py`, an executable
reading of `MergeRecordFields` (`sync_field_merge.cc`). Repository tests pin
it by hand-derived cases and prove it converges independent of order.

Covered entities, with the rules exercised:

| Entity | Rules |
| --- | --- |
| workspace, treeNode, deviceSession, appearance, permittedSetting, remoteCommand, tabArchiveEntry | duplicate, newer, older, disjoint union with successor clock, equal-clock conflict, device tiebreak, logical-counter overflow, incomplete map, field clock after record clock |
| workspace, treeNode, deviceSession, remoteCommand, permittedSetting | immutable-group change |
| treeNode | atomic `location` move |
| remoteCommand | terminal status kept, status never regresses |
| tabArchiveEntry | archive deletion is terminal; `state` still merges |

## Files

- `files/overlay/chromium/src/ahoi/browser/sync/sync_merge_conformance_unittest.cc`,
  its `testdata/merge_v3.json` copy, and `BUILD.gn.patch` (adds the test
  source and the data file to `unit_tests`). A payload rejected by
  `DeserializeRecord` counts as `invalid`. Merged results are compared as
  canonical `SerializeRecord` bytes.
- `files/apps/AhoiMobile/Tests/AhoiMobileCoreTests/SyncMergeConformanceTests.swift`
  reads the repository file through `#filePath`. It runs workspace, treeNode,
  deviceSession and tabArchiveEntry. Swift has no decision enum, so a vector
  passes when the merged model equals the decoded expectation, or when both
  sides reject the input.

## Apply

1. Desktop/sync: copy the overlay files and run
   `git apply handoffs/crest-hardening/009-sync-merge-conformance/BUILD.gn.patch`.
   Run `ahoi_sync_unittests --gtest_filter='SyncMergeConformanceTest.*'` in the
   next planned package build; no extra build.
2. Mobile/sync: copy the Swift test file and run it on the Mobile simulator
   (a Mobile-owned resource).
3. On any mismatch, fix the implementation or report the vector to the lane
   checkpoint; never edit expectations by hand. Regenerate with
   `python3 tools/sync_conformance/generate_merge_vectors.py`, which
   `test_sync_conformance_merge.py` checks.

## Findings for the Sync owner

- The Companion has no field merge for `remoteCommand` (6), `appearance` (7)
  or `permittedSetting` (8); Chromium merges all three per field. The Swift
  runner lists them explicitly (`testUncoveredEntitiesAreExplicit`). Decide
  whether iOS may overwrite these records whole or needs the same merge.
- `CompanionFieldMerge.mergedVersion` takes the maximum clock of each field.
  C++ `MergeRecordFields` only advances a field clock when the value is
  copied, and it applies the successor clock only for a true union. The
  vectors `*.disjoint_union`, `*.incoming_older` and `*.logical_overflow_successor`
  will show whether both give the same result.
