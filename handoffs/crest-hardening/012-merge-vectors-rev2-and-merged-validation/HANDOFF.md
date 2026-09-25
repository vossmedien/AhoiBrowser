# 012 – Merge vectors revision 2 and validating merged records (H1)

Status: ready
Owner lanes: desktop/sync (overlay testdata, `sync_store.cc`), mobile (Swift rerun)
Base: `5033235`; follows the results that handoff 009 reported back

## 1. Vector revision 2 (fixes the 16 reported vectors)

The generator produced invalid inputs:
- string-suffixed `last_seen`, `color_mode` and `value_json`;
- an accent colour next to the system accent;
- a tombstone on remote commands.

It now uses explicit, typed alternatives per entity. Accent and system accent
always change together. Remote commands get no second group, because a
command is never tombstoned and `request` is immutable; this drops
`remote_command_open_shape_only.disjoint_union` and `.logical_overflow_successor`.
A new repository test (`test_inputs_pass_the_wire_invariants`) mirrors the
reported decoder rules for every input. There are now 130 vectors: 46
`mergeFields`, 29 `invalid`, 28 `acceptIncoming`, 20 `keepExisting` and 7
`duplicate`.

Apply: copy `fixtures/sync-conformance/merge_v3.json` over
`overlay/chromium/src/ahoi/browser/sync/testdata/merge_v3.json` (the same
bytes as the refreshed copy in handoff 009). Rerun
`SyncMergeConformanceTest.SharedVectors` with the next planned build and
`SyncMergeConformanceTests` on the simulator; no extra build. Until then,
`test_integrated_overlay_copy_matches` reports the stale copy as skipped.

## 2. Finding for the Sync owner: a union can violate a record invariant

`SyncStore` stores and re-publishes the result of `MergeRecordFields` without
validating it (`sync_store.cc`: `MergeRecordFields` → `SerializeRecord` →
`UpsertRecord` → convergence change). The appearance rule "no accent with
the system accent" (`sync_merge.cc:369`) spans two independently merged
field groups, `use_system_accent` and `accent_argb`.

Legal concurrent edits, computed with `tools/sync_conformance/merge_model.py`:

| Device | Edit | Clock |
| --- | --- | --- |
| Mac | turns on the system accent (`use_system_accent = true`, accent removed) | T2 |
| iPhone | picks accent colour X (only `accent_argb`) | T3 |

Merge result: `mergeFields` with `use_system_accent = true` and
`accent_argb = X`. The Mac stores it and publishes it. Every other peer's
`DeserializeRecord` rejects it, so it lands in quarantine and the devices
diverge.

Options (Sync owner decides):
- (a) Validate merged records with `ValidateRecord` before `UpsertRecord`,
  and quarantine a union that fails.
- (b) Make accent colour and system accent one atomic field group, as
  `location` is for tree nodes. This changes the format-3 field map and the
  contract.
- (c) Define a deterministic repair rule, such as "the system accent wins and
  clears the colour", applied in both C++ and Swift.

Whatever is chosen, the case becomes a fixed vector here. Other entities with
cross-group invariants need the same review (for example command status
against tombstone, and archive `state` against `tombstone`).
