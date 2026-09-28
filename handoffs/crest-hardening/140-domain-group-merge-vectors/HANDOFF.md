# 140 — Shared merge vectors for domain-value field groups

Status: integrated by owner in `ef0179df` (Mobile runner) and `56761822`
(Sync runner/testdata); native C++/Swift execution and 122 actual-output
comparison pending
Owners: desktop/sync (C++ runner, BUILD.gn, testdata) and mobile (Swift runner)
Base: committed `bcccfb4`; independent of unintegrated 138 (both application
orders were checked in a disposable copy).

Fixture SHA-256:
`3c0292a72857567456598cdf0410f7492b488f2d2ecb7e55ff9035f65e3a9345`
(24 cases). Patch SHA-256:
`184ef8fd93ead0b51aa67ce58753b7308b0a2d791296f382a1263fff3c9cf67b`.

## Why

The completion audit's H1.1 row lists missing *domain-value* coverage. The 140
standard vectors never vary Workspace `archive_policy`, `accent_argb` or
`modified_at`; they vary no TreeNode page target, `home_target`,
`is_temporary` or `accent_argb`; and PermittedSetting is only exercised with the
boolean `ahoi.appearance.glass_enabled`. The published contract values in
`config/sync-format.json` (`workspaceStructure.home/archive`, `sharedTargets`,
`browserSetupSettings.extensionDesired/extensionStorage`) therefore had no
shared merge evidence.

Crest's own oracle also had a gap here: `merge_model.py` mapped the TreeNode
`url` group to the `url` key alone, while `sync_field_values.cc` compares and
copies `url`, `target_kind` and `local_scheme` together. Merging with the old
mapping turns a newer local-only target into `url ""` with web kind 0 (an
invalid page), and accepts a union of an explicit new-tab target with a
concurrent save. The model now maps the three keys and applies the
new-tab-requires-temporary rule of `ValidateRecord` to a union. All earlier
generated fixtures (140/8/6/36, 132 sequences, 112/138 projections,
catalogue) regenerate byte-identically.

## Cases

`fixtures/sync-conformance/merge_domain_groups_v3.json`, all `inputValid: true`:

- **Workspace (6):** newer archive policy; rename ∪ later policy; equal-clock
  policy conflict; accent ∪ icon; newer *absent* accent removes the custom
  accent; edits that also stamp `modified_at` keep each group plus the newer
  modification time.
- **TreeNode page (12):** Home replaced atomically (web → local-only file);
  rename ∪ Home; newer Home removal (three JSON nulls); stale removal loses;
  equal-clock Home conflict; **dormant Home retained** on a page concurrently
  made temporary (contract `home.temporaryPages`); page target replaced
  atomically; rename ∪ local-only target; equal-clock target conflict; saving a
  temporary page; **new-tab target ∪ concurrent save → invalid** although both
  inputs are valid; accent ∪ icon.
- **PermittedSetting (6):** extension desired state (disable newer; older
  uninstall loses; equal-clock disable/uninstall conflict) and extension
  storage boolean-or-null reset (newer reset; stale reset loses; equal-clock
  true/reset conflict). Values keep the canonical compact key order of the
  wire fixture; no tombstone (both setting kinds forbid it).

Decisions are hand-pinned in
`tests/repository/test_sync_conformance_domain_groups.py`, independently of
the model; the test also checks input validity against the contract, atomic
movement of both multi-key groups, and this handoff's mirrors.

## Source-read parity expectations

- C++ `MergeRecordFields` calls `ValidateRecord` on every union
  (`sync_field_merge.cc`), whose TreeNode branch rejects a new-tab target on a
  non-temporary page.
- Swift `CompanionFieldMerge.merge(TreeNode)` copies `url` with the incoming
  target kind/scheme and re-validates through `SharedTabURLGroup.of(result)` →
  `validatePage(isTemporary:)`. Neither side validates Home against
  `is_temporary`, matching the dormant-Home contract.
- Swift decodes `home_*` via `decodeHomeTarget` (three mandatory keys, all
  null or one valid kind 0/2 target) and the accent as signed int32.

These are source reads. A native disagreement must be pinned as a failing
vector and fixed in product code, not relabelled as a valid conflict.

## Patch

`140-domain-group-merge-vectors.patch` (complete mirrors under `files/`):

- C++: `TEST(SyncMergeConformanceTest, DomainGroupVectors)` plus the
  `testdata/merge_domain_groups_v3.json` copy and its `BUILD.gn` data entry.
- Swift: `testDomainGroupVectors()`; the existing runner already maps entity
  1, 2 and 8 to the product codec and merges.

Both runners export actual codec results through the integrated 122 exporter
under `AHOI_SYNC_CONFORMANCE_OUTPUT_DIR`; the lane comparator validates this
fixture (added to `test_all_committed_merge_cases_have_valid_oracles`).

## Validation by Crest

- 87 local Python conformance tests pass (all `test_sync_conformance_*`),
  including generator freshness for every committed fixture and the new
  hand-pinned decisions.
- Negative control: merging the committed inputs with the previous model
  mapping yields `('', 0, None)` instead of `('', 2, 'file')` and `mergeFields`
  instead of `invalid` for the new-tab union.
- `git apply --check` against the working tree at `bcccfb4`; 140→138 and
  138→140 both apply in a disposable copy of committed owner sources.
- **Not run:** Swift parse (deferred: host load 124, 0 % idle, foreign
  compilers active at 10:14 CEST), any native compilation/test, simulator,
  browser or shared lock.

## Owner steps

1. Apply the patch on the owner branch (Sync: C++/BUILD.gn/testdata; Mobile:
   Swift runner).
2. When the owner test gates allow it, on exact frozen source run C++
   `SyncMergeConformanceTest.DomainGroupVectors` and Swift
   `testDomainGroupVectors`, with export enabled; record exits and binary
   hashes, then run `tools/sync_conformance/compare_runner_outputs.py` for this
   fixture.
3. Store/apply side effects (Home navigation dormancy, extension install
   effects, archive timers) are outside a pair runner and stay separate gates.
   No wire version, capability or outbound authorization changes.
