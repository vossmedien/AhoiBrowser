# 128 — Inventory and developer assets lose concurrent field edits on Mobile

Status: ready source patch; native RED/GREEN, C++ execution and differential
comparison pending
Owners: mobile and sync; C++ testdata intake by desktop/sync
Base: `476dd51` with Crest 122 integrated. Apply the incremental patch; full
`files/` mirrors are review copies and may omit newer owner changes.

Fixture SHA-256: `e02a1de79e8d14a3b56bd690728d525b652e9b081d2727a91c3ccc6d58e80ea1`
(6 vectors). Patch SHA-256:
`ce26b4a89e7e65df6c3bf2ef49aa4c08f21b85643c1400ff4ebc382f2147a43d`.

## Proven source divergence

Format 3 declares separate field clocks for `extensionInventory` (ID 9) and
`developerAsset` (ID 10). Desktop's `MergeRecordFields` compares those groups
independently. Mobile `CompanionProductStore.selectRecord` and
`CompanionImportBatch` currently choose an entire record by its top clock.

Example from the new valid wire fixture: the Mac renames an extension at T1;
the iPhone changes its `enabled` flag at T2. C++ yields the renamed extension
with `enabled=false`, retaining both field clocks and minting a successor
record clock. Mobile currently chooses the T2 record, restoring the old name.
Reversing arrival order produces the same lost edit. Developer assets have
the same failure. Equal-clock conflicting names must be rejected, rather than
silently selecting one.

The six cases cover both delivery orders and a same-field clock conflict for
each entity. `inputValid: true` requires successful wire decoding before a
merge conflict counts. They are a separate supplement; the accepted 140 and
8 vectors and 112 projection fixture are unchanged. The Python reference
model uses the published field maps and C++ immutable groups; a control
demonstrates that the old whole-record selection differs from the required
union. This is source/model evidence, not a native C++ result.

## Proposed patch and owner gates

`128-inventory-asset-field-merge.patch` changes the two mobile merge call
sites to use new typed `CompanionProductFieldMerge.merge` overloads. They keep
the existing immutable identity checks, compare portable text by UTF-8 bytes,
apply each group's clock, mint a successor only for a true union, reframe a
winning tombstone to the resulting record clock and validate the complete
record through its existing initializer. The import path retains its existing
`shouldReenqueue = merged.version > incoming.version` rule; this handoff does
not widen outbound class authorization or turn opaque product data into a new
capability. The owner must verify that an offline union's re-enqueue follows
the actual device and consent policy before accepting the changed behavior.

Two repository upsert tests exercise the actual existing caller and fail with
the old whole-record selection; both C++ and Swift conformance runners gain
the same six-case supplement and export their actual results through 122.
The fixture is registered as C++ testdata. No wire field, Format-3 version,
serialization default or secret policy is changed.

For a native RED control, add the two `CompanionProductRecordTests` methods in
an isolated exact-source checkout while retaining the old `selectRecord`
callers. Both should fail because the older name is lost; an unrelated wire
round-trip method should pass. Then apply the full patch and run the affected
product/conformance classes for GREEN. Run `SyncMergeConformanceTest`'s new
InventoryAssetVectors on the matching C++ candidate, capture both 122 exports
and their direct process exits, and compare with the lane tool. Bind source,
fixture and actual tested binaries. C++ source syntax alone is insufficient.

All 70 local Python conformance tests pass; fixture generation and owner
testdata mirror are byte-identical. Five Swift review mirrors parse, and
`git apply --check` passes on the observed tree. Crest ran no native compiler,
simulator, browser, paid test or API action and holds no build lock. Owner
compaction work remains separate. This resolves only two of the eight classes
missing from the original shared 140-vector corpus; H1 and H7 remain open.
