# 138 — Shared RED fixture for the merge-root marker collision

Status: ready test-source patch; native RED, product fix and GREEN pending
Owners: desktop/sync and mobile
Base: current source with 112/130 readers; 112's frozen fixture is unchanged.
Fixture SHA-256:
`7cba0bd6156350766b0b5be51af180dcefe8e10393695046b79d53be766dd471`.
Patch SHA-256:
`e915ca621d93f2c95eaf5d9d79aefc3eb083098552ef6e36c0c1a0745170cb32`.

## What this pins

Handoff [126](../126-merge-root-marker-collision/HANDOFF.md) identified a
normal raw B root whose valid opaque key contains
`!:ahoi-merge-root/<B UUID>/x`. The existing C++ `rfind` and Swift literal
suffix search treat that ordinary key as an authored segment position.

`workspace_merge_marker_collision_v3.json` adds two shared raw Format-3 cases
without changing 112:

1. B has ordinary Q with key `Q<marker>x` and Z with key `Z`, **no merge**.
   Required visible order is Q,Z.
2. A really merges into B and late raw X follows from A. The same Q/Z raw
   records must remain Q,Z, with X appended: Q,Z,X.

Both cases run three Workspace array orders × three Node array orders, **18
projections**. The runners use their existing production adapter/snapshot,
repeat projection and assert unchanged raw Page bytes and field clocks. The
fixture's expected order is hand specified; its generator does not emulate
either platform's projection algorithm. The frozen 112 JSON/hash are intact.

The patch registers the supplemental testdata and lets each existing
projection reader load a named fixture. It adds
`SyncWorkspaceProjectionConformanceTest.MarkerCollisionFrames` and
`testMarkerCollisionProjectionFrames`. It also removes the Swift reader's
accidental dependency on the main merge-pair fixture override when resolving
the projection file path. No production source, wire field or clock is changed.

## Owner design boundary

The current implementation should be run **RED** on an exact frozen candidate
before a product fix; source analysis predicts both sibling-order assertions
fail. The test covers a concrete malformed marker-like suffix (`x`). A stricter
grammar can fix that specific false match, but it cannot by itself prove that
every arbitrary opaque key with a fully imitated grammar was authored by the
merge algorithm. Those two intents can have identical raw wire bytes; a
deterministic projector has no additional information to distinguish them.
The owner must define an unambiguous reserved-key/compatibility rule or
another source-backed representation across both clients, respecting Format
3's no-silent-clock-rewrite boundary. Handoff 126's fresh-peer, actual writer,
capture/reopen, undo and marker-compatible key tests remain required. A
localized parser tweak alone must not be called complete R1.

After the owner fix, run both new tests and all of 112 on exact C++/Swift
candidates; record native source/binary/fixture bindings and the failed then
passing sibling orders. Follow with 118's real writer/capture/opposite-platform
replay. No CloudKit or installed acceptance is inferred from the pure
projection fixture.

All **81** local Python conformance tests pass; the new fixture generator,
owner testdata copy and source hash match. The Swift runner mirror passes
syntax parse; patch applicability and reverse mirror checks pass. Crest ran no
C++ compiler, XCTest, simulator, browser or shared resource and changed no
owner product path.
