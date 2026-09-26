# 096 – Merge vectors for `merged_into` (H1, follows 084)

Status: ready
Owner lane: desktop (copy one file); mobile and sync (rerun)
Base: HEAD after this lane's commit.

084 is integrated on the C++ side (`628c158`, `9bc5924`). The Swift codec
already writes and reads `merged_into` (working tree). This lane made two
changes:

- `tools/sync_conformance/merge_model.py`: `merged_into` belongs to the
  Workspace `tombstone` field group, as in `sync_field_values.cc`.
- `fixtures/sync-conformance/merge_v3.json`: regenerated, now 134 cases (was
  131). Three new cases:
  - `workspace.merge_tombstone_with_rename` (`mergeFields`: renamed, tombstoned,
    `merged_into` set);
  - `workspace.merge_undo_clears_target` (`acceptIncoming`: revived, no
    `merged_into`);
  - `workspace.stale_revival_loses` (`keepExisting`).

The field-group drift gate stays at 0 findings. Every repository conformance
test is green; the overlay copy check is skipped as stale, as intended.

## For the owners

- Desktop: copy `fixtures/sync-conformance/merge_v3.json` to
  `overlay/chromium/src/ahoi/browser/sync/testdata/merge_v3.json`. It is
  identical to the copy under `handoffs/crest-hardening/009-…/files/`. Then run
  `ahoi_sync_unittests --gtest_filter='*MergeConformance*'`.
- Mobile: `SyncMergeConformanceTests` reads `fixtures/` directly; run it
  with the Swift `merged_into` codec.
- Expected result on both sides: 134 of 134. A mismatch in the three new cases
  means the codec does not treat `merged_into` as part of the tombstone group.
