# 009 – Shared merge conformance vectors for C++ and Swift (H1)

Status: integrated 095b959 (runner fix 11bf324; 16 vectors reported back, see "Results")
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

## Results (desktop/sync/mobile owner, 2026-09-25)

- **C++** (`ahoi_sync_unittests`, build 16 of `993a151`,
  `SyncMergeConformanceTest.SharedVectors`): every vector whose inputs the
  wire decoder accepts (118 of 132) gives the expected decision and canonical
  merged bytes. The test fails only on 14 vectors whose *inputs* are rejected
  by `DeserializeRecord`, although the expectation is not `invalid`.
- **Swift** (`SyncMergeConformanceTests`, simulator A168): the runner needed
  envelope tombstone metadata for deleted payloads (the codec requires it to
  match the payload clock) and now compares wire fields only; runner-
  synthesized tombstone metadata and the local `OrderKey` tie-breaker of an
  opaque `sort_key` are excluded (`11bf324`). No expectation was edited.
  67 of the 69 vectors for the four covered entities pass; the other 2 are in
  the list below.
- **Vectors for the lane: generator produces invalid inputs.** Please fix in
  `tools/sync_conformance/generate_merge_vectors.py` and regenerate; neither
  side should accept them:
  - `first_alt` appends `" (other)"` to typed strings: `last_seen`
    (timestamp: `device_session.incoming_older`, `.device_tiebreak`,
    `.equal_clock_conflict`), `color_mode` (enum:
    `appearance_custom_accent.incoming_older`, `.device_tiebreak`),
    `value_json` (JSON text: `permitted_setting_glass_enabled.incoming_older`,
    `.device_tiebreak`).
  - Appearance edits break the accent/system-accent invariant the decoder
    enforces: `appearance_custom_accent.disjoint_union`,
    `.logical_overflow_successor`, `random.153.009/019/022/057.appearance`.
    If a group edit can legally produce such a pair after merge, that is a
    merge question for Sync; as inputs they are invalid.
  - `remote_command_open_shape_only.disjoint_union` and
    `.logical_overflow_successor` combine `tombstone: true` with a
    non-terminal `status`, which the decoder rejects.
- The two Companion findings above (no field merge for entities 6, 7, 8;
  `mergedVersion` taking each field's maximum clock) are unchanged: on the
  covered entities the Swift results equal the C++ results, so the clock
  difference produced no observable mismatch in these vectors. Whether iOS may
  overwrite entities 6–8 whole remains a Sync-owner decision, recorded in
  `docs/ACTIVE_SYNC_COORDINATION.md`.
