# 132 — Native sequential merge replay with a pinned random seed

Status: draft awaiting lightweight Swift parse when the host is free; native
execution and direct output comparison pending
Owners: desktop/sync and mobile
Base: `e670ff1` after 128/130 source integration. Apply the incremental patch;
the complete `files/` mirrors are for review and may omit newer owner work.

Fixture SHA-256:
`58123596b9cc485c112e1144d77f38bcfac4623669d13f5d916434d7dc764b99`.
Patch SHA-256:
`8dfa4ac786a86e125a5bf07aeb60e7e8e43247b9559e47169b97afb40937491d`.

## Gap and proposed behavior

The existing H1 vector runners independently evaluate pairs. H1.3 also asks
for deterministic operation **sequences**: a native implementation must feed
its own accepted result into the next merge, preserve state through rejected
batches, and expose the canonical final state. A pair runner whose next
`existing` payload comes from the fixture oracle cannot test that path.

`merge_sequences_v3.json` pins seed 153, 18 cases and 104 ordered operations.
Fifteen seeded cases span Workspace, TreeNode, extension inventory, developer
asset and split group. They vary delivery order and values, then apply a
same-clock conflict, delayed duplicate, newer group edit and stale baseline.
Three fixed cases exercise Workspace merge/late rename/conflicting route/undo,
terminal remote-command status and terminal archive deletion. Each step
records the writer device, valid incoming payload, expected decision and
expected canonical merged payload; each case also has a final expectation for
the 122 comparator. The generator accepts another integer seed, but those
new bytes must run on **both** native implementations through the explicit
sequence fixture override before yielding evidence.

The patch adds `SyncMergeConformanceTest.SeededMergeSequences` and
`testSeededMergeSequences`. The C++ test holds the actual `SyncRecord`
through all steps. The Swift test re-decodes the **actual encoded product
result** as the next input. Invalid steps retain the actual prior state.
Both tests check every intermediate decision/result and export their actual
final codec payload in the existing 122 output protocol. A failed step yields
a nonzero native test exit and cannot become a comparator PASS even if a later
operation happens to repair the final value. The final differential comparison
uses the same fixture/output/binary receipt checks as pair cases. No product
merge policy, wire field, version or capability is changed.

## Evidence and intake

All **78** local Python conformance tests pass, including deterministic seed
and changed-seed controls, replay of every step from the preceding result,
terminal/undo assertions, fixture freshness and exact owner testdata mirror.
`git apply --check` and a reverse check against the review mirror pass. The
current host sample at 11:59 CEST was load 172 with live `xcodebuild` and
`swift-frontend`; Crest therefore did **not** start its own compiler for
Swift parsing/typechecking, nor any native test, simulator or browser. This
handoff remains draft until the lightweight parse check and patch recheck are
recorded on a quiet source slot.

After owner intake, run the two sequence tests on exact frozen candidates,
request complete native exports with the same fixture bytes, record direct
test exits and tested binary hashes, then compare actual final states with
`tools/sync_conformance/compare_runner_outputs.py`. Preserve any mismatching
seed/steps as a fixed regression. The native runner still does not substitute
for production store capture, real transport delivery or CloudKit acceptance.
