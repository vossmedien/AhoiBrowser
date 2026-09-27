# 130 — Shared merge vectors for the remaining six Format-3 entities

Status: integrated by owner in `8e96de7` (Mobile runner) and `81a26f3`
(Sync runner/testdata); native C++/Swift execution and actual-output comparison pending
Owners: desktop/sync and mobile
Dependency: integrate [128](../128-inventory-asset-field-merge/HANDOFF.md)
first. This patch is incremental to its runner/testdata changes. The two-patch
stack was applied in a disposable copy of current committed owner source;
product files were not changed by Crest.

Fixture SHA-256:
`516d5bfa2665bc28e387b055e305896c717a87e3e119460d0f6360f6473d9f38`
(36 cases). Patch SHA-256:
`202a75b9080e9500d5b7d691378d68cb691ab390194c7d6c38e7487c49b06cb4`.

## Scope

`merge_remaining_entities_v3.json` adds six cases each for device (0),
historyVisit (3), deviceTab (4), bookmark (11), deviceCapability (12) and
splitGroup (13): identical, newer single-field, delayed older but independently
newer group, disjoint union in both arrival orders and equal-clock conflict.
The generator reads the published Format-3 field groups and the existing
canonical wire fixture. Its model maps the atomic URL/location/capabilities
groups and the immutable groups from C++ `sync_field_merge.cc`. Every vector
has `inputValid: true`, so a decoder rejection cannot count as a valid merge
conflict. Capability edits use the authenticated DeviceID as writer and keep
features sorted; split topology retains its two valid members; bookmark
location preserves exactly one root/parent selector.

The patch adds only test-runner cases and Desktop testdata registration.
Mobile uses existing typed `CompanionReadModelFieldMerge`,
`CompanionBookmarkFieldMerge`, `CompanionCapabilityDomain` and
`CompanionWorkspaceStructureMerge` methods. It resolves Golden fixture
Device/Workspace dependencies for remote-tab decoding. Both runners export
actual typed codec results through 122. Review mirrors are complete files;
apply the incremental patch after 128 rather than replacing newer owner work.

With 128 plus 130 integrated, all 15 declared EntityType IDs occur in shared
pair-merge fixtures (140 standard + 8 UTF-8 + 6 inventory/asset + 36 here).
This **does not** complete H1: actual native results, direct comparison,
general multi-operation sequences, full domain-value coverage and the store/
apply cases remain separate gates. Any native disagreement must be pinned as
a concrete vector and handed to the Sync owner rather than waved through.

## Validation and owner intake

All **74** local Python conformance tests pass, including generator freshness,
field-map completeness, shape-sensitive inputs, hand-derived decisions and
exact testdata mirror identity. The Swift runner mirror parses. The incremental
patch reverse-checks against its complete mirror, and the 128→130 stack applies
to a disposable copy of current committed owner source. No native typecheck,
unit test, simulator, browser, API or shared lock was used by Crest.

After the host and owner test gates permit it, run the new C++
`SyncMergeConformanceTest.RemainingEntityVectors` and Swift
`testRemainingEntityVectors` on exact frozen source. Export using the same
36-case fixture bytes, capture direct exits and binary hashes and run the 122
comparator. A native decoder failure for `inputValid: true` is a failure, not
a valid conflict. Exercise field unions and delayed batches through production
store/import separately; the pair runner cannot prove those side effects or
real CloudKit delivery. The six-class supplement adds no wire version, new
capability or outbound authorization.
