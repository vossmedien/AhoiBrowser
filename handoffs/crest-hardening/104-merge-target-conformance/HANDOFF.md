# 104 – Shared merge-target equality/convergence regressions (H1, after 084/096)

Status: integrated 61b9e93 (140-vector copies byte-identical; 26 repository checks pass; Swift 140-vector runner and coverage pass on clean a747c62, no skips; exact C++ execution pending)
Owner lanes: desktop (refresh testdata); sync/mobile (exact-candidate runners)
Base: owner source freeze `4f2b154`; Desktop fixture intake 096 was `de52766`.
Vector SHA-256: `da09b4c1feb6247c30975376642893eb95a56a4ce0a3f3bcf059327121efacf8`; 140 cases (134 plus six).

## Source review and purpose

The owner identified that Swift's old `incomingWins` compared only `isDeleted`
for the Workspace tombstone group. A different or missing `mergedInto` could
therefore survive an equal field clock without quarantine. The fix is already
in `4f2b154`: `WorkspaceDeletion` includes both values; projection equality and
local field stamping do too. C++ `sync_field_values.cc` also treats the target
as part of this group. This lane reviewed that source and adds common wire
vectors so both implementations must exercise the same edge cases.

No second Mobile implementation or re-homing writer is started here.

## Six new vectors and independent expectations

| Name after `workspace.` | Expected decision | Required result |
| --- | --- | --- |
| `merge_target_identical` | `duplicate` | Same destination and clocks are valid |
| `merge_target_equal_clock_conflict` | `invalid` | Equal tombstone clocks, different destinations |
| `merge_target_absent_equal_clock_conflict` | `invalid` | Equal tombstone clocks, missing versus present destination |
| `merge_target_newer_wins` | `acceptIncoming` | Newer tombstone-group clock replaces destination |
| `merge_target_older_loses` | `keepExisting` | Reverse arrival retains that newer destination |
| `plain_delete_clears_merge_target` | `acceptIncoming` | Newer ordinary deletion clears destination but stays deleted |

The conflict inputs individually satisfy the wire invariants: valid UUID,
not self-targeting, deleted Workspace, complete field maps. They have identical
field clocks and differ only in `merged_into`. The independent Python assertions
require rejection in both arrival orders; nonconflicts pin target, tombstone,
field stamp, projected result and record clock in both orders.

## Intake

- Copy this commit's `fixtures/sync-conformance/merge_v3.json` into
  `overlay/chromium/src/ahoi/browser/sync/testdata/merge_v3.json`. Its path mirror
  in `009-sync-merge-conformance/files/overlay/.../testdata/merge_v3.json` is
  byte-identical. Verify the SHA above when freezing the candidate.
- Swift reads the shared fixture directly. Pin the fixture bytes **and** source
  revision in the run evidence, even if the fixture lives outside the binary.
- Expected: 140/140 in `SyncMergeConformanceTest.SharedVectors` (C++) and
  `SyncMergeConformanceTests` (Swift) after owner integration. C++ and Swift
  runs remain subject to current resource/ownership/test gates.
- 096's previously integrated 134-vector source/evidence remains historical;
  it does not prove this new set. An expected repository skip persists until
  Desktop refreshes its copy. Do not edit the overlay from the Crest lane.

## Validation and limits

26 local conformance tests: 25 passed, 1 expected stale-overlay skip. Generator
check and the 37-copy drift gate pass with no findings. A scalar-tombstone
negative control in the Python model produces `duplicate` for both conflict
vectors, proving these cases distinguish the omitted-target bug; this is not
an execution of the old Swift product. No build, simulator, E2E or browser run.

084 remains **partial**: mobile incoming-node re-homing and exact-candidate
C++/Swift/visible acceptance still belong to the owner. This handoff tests the
record field merge, not the apply/re-homing or CloudKit transport rules.
