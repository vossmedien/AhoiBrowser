# Workspace merge projection conformance (Format 3)

`workspace_merge_projection_v3.json` is the shared H1/H7 **apply** fixture,
selected by the Desktop/Mobile/Sync owner. It complements the 140 record-field
merge cases in `merge_v3.json`; neither fixture substitutes for the other's
scope. Generator: `tools/sync_conformance/generate_workspace_projection_vectors.py`.
Use `--check` to verify freshness. Expectations are handwritten in the generator,
not calculated by a copy of either production projector.

## Input and iteration

The fixture schema is 1; the payload `modelVersion` stays 3. Each case has one
or more `frames`. A frame contains complete raw `workspaces` (entity 1) and
`nodes` (entity 2) payload objects **after field merge and before projection**.
IDs, timestamps, field maps and optional merge targets use the existing wire
shape. Individual records are valid; parent/workspace availability can be
incomplete, as it can between delivered batches. Reuse the existing wire
conformance decoder/envelope helpers rather than inventing envelope clocks.

For every frame, run the Cartesian product of its Workspace and Node array
orders: as written; reversed; rotated left by one (empty/singleton unchanged).
That is 23 cases, 31 frames, 9 permutations per frame = 279 projection checks.
Expected results are unchanged by array enumeration. They are not 279 real
cross-device journeys. Keep the source revision and full fixture SHA-256 in
both runners' result artifacts.

Frames show successive authoritative states after the indicated deliveries.
They do not instruct a runner to overwrite a newer record with an older one:
field-merge convergence is checked separately in `merge_v3.json`. Case groups
`late_delivery` and `undo_and_explicit_move` have identical final raw record
sets and identical expected views despite differing intermediate states.
A runner may also replay imports to reach each state, but must identify that
additional evidence separately from a pure adapter/snapshot test.

## Expected presentation

- `workspaceRoutes` classifies raw routes: `live`, `resolved-merge`,
  `missing-target`, `cycle`, `deleted-target`, `ordinary-deletion`, or
  `missing-source`. Only live/resolved routes name `targetWorkspaceId`.
  These labels describe common routing outcomes; they do not require adding
  a public product enum or a new wire field. An adapter may expose its private
  resolver to its tests.
- `effectiveNodes` pins the live fixture node's stable ID, effective Workspace
  and effective parent (null = root). A valid re-homed/already moved parent
  stays. Missing/deleted/cross-workspace/non-folder parents on a resolved
  merge detach to the target root, without generic recovery.
- `siblingOrder` pins order among **constrained original fixture nodes** in
  that Workspace/parent group. Pre-existing target roots precede appended
  late source roots, which retain their raw sort-key/ID order. Derived keys
  or ranks may be platform-specific: the user-visible order must match, and
  raw wire keys must not change. This does not define a permanent priority
  over later explicit user reorders or insertions, which need their own tests.
- `unconstrainedNodeIds` belongs to unresolved routes. Do not assert a
  platform's fallback Workspace, synthetic recovery folder/ID, or recovery
  order for these nodes. Filter those nodes and synthetic recovery artifacts
  out of the common sibling-order comparison. Their generic recovery behavior
  still needs the platform's existing tests; it is not excused by this fixture.
- `notLiveNodeIds` must not reappear as live because a Workspace was merged.
  A native store may retain their tombstone rows.

## Raw authority and undo

Before projection, serialize each raw Workspace/Page with that runner's normal
wire codec and snapshot every field clock. After projection, repeated projection,
search/passive capture and persist/reload, require **byte-identical before/after
serialization using the same codec**, and identical field clocks. Test the
Desktop SyncRecord authority, not the intentionally changed native TabTree
presentation; on Mobile test the raw `treeNodes`, not its derived view.
Ordinary Presence/session publication is not a Page-location repair.

`rawRecordSetSha256` fingerprints the fixture inputs independently of array
order: UTF-8 JSON, keys sorted, compact separators, non-ASCII unescaped, arrays
sorted by ID. It identifies fixture data, not the original encrypted transport
bytes or a promise that C++ and Swift serializers escape JSON identically.
`expect.preserveRaw` lists the records that require a local before/after byte
and clock comparison. `unchangedNodeIdsFromPreviousFrame` additionally pins
nodes left untouched between frames. Workspace merge/undo records and explicit
local node moves legitimately have newer clocks between frames.

The explicit-move cases move X into a real existing folder in B, stamping the
location group; this is a genuine edit, not treating projected membership as
an implicit move. Workspace undo returns passive Y to A while X stays in B's
folder. Both delivery orders converge. No synthesized repair clock is required.

## Remaining gates

A valid fixture and green repository shape tests do not prove either product
runner passed. Desktop must integrate the testdata copy and matching adapter
runner; Mobile consumes the same file and adds its matching projection/codec
checks. Build/runtime slots remain owner-controlled.

Retention is deliberately still open: these raw-payload-only frames do not
represent a post-compaction routing ledger. `missing-source` is an unresolved
input classification, **not** acceptance that compaction may lose a previously
resolved destination. Retention evidence must prove stable placement across the
owner's actual compaction path once the preserving design is implemented.
