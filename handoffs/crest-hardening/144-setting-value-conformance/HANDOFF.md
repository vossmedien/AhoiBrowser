# 144 — Shared browser-setting value conformance (H1 domain values)

Status: ready (fixture + test-only runner patch; no native run)
Owners: desktop/sync (C++ runner, BUILD.gn, testdata) and mobile (Swift runner)
Base: committed `1ba0b14`.

Fixture SHA-256:
`4262159ecd85764bd799bf80b72bcb2080dcfdf7217c2d6c24068a41d41abd45`
(135 cases). Patch SHA-256:
`c8b4d0446c2aaadbeb85c23776e453e2166786d97824636d9fa8b4ce3b77e0ec`.

## Why

PermittedSetting merges `value_json` as opaque RFC JSON (`sync_merge.cc`);
the catalogue's value rules are enforced by the adapters: C++
`ValidateBrowserSettingValue` (`browser_setting_catalog.cc`) and Swift
`CompanionBrowserSettingCatalog.validatesValue`. The completion audit left
these navigation/routing values without shared evidence. A source read finds
both platforms equal (same 24 IDs, search-engine allowlist, auto-hide
100…10000 integer, font scale 0.5…4.5, reading color/spacing sets, network
prediction 0…3, charset `UTF-8`/`windows-1252`, reset `null` only for a known
ID). This fixture turns that read into executable evidence on both sides.

## Cases

`fixtures/sync-conformance/setting_values_v3.json`: `catalogueIds` (24) plus
135 hand-derived `(settingId, valueJson, valid)` cases, covering inclusive
bounds, off-by-one, integral doubles (`1500.0`, `1e3`, `1.0`), fractions,
type confusion (bool/number/string), case-sensitive engine names, charset
aliases, deprecated enum values (color 6, spacing 0), unknown IDs (including
`null`), invalid JSON, and a `null` reset for every catalogue ID.

The repository test compares `catalogueIds` with the IDs parsed from **both**
source files, so a catalogue change on one platform fails the drift gate
before any native run.

## Patch

- C++: `TEST(SyncMergeConformanceTest, BrowserSettingValueVectors)` in the
  existing `sync_merge_conformance_unittest.cc` (parses each `valueJson` with
  `JSON_PARSE_RFC`, then `ValidateBrowserSettingValue`; compares the catalogue
  ID set with `GetBrowserSettingCatalog()`), testdata copy and `BUILD.gn` data.
- Swift: `testBrowserSettingValueVectors()` in the existing
  `SyncMergeConformanceTests.swift` (record-ID set equality via the public
  `recordIDs`, then `validatesValue`). No new file, no `project.pbxproj` edit.

## Validation by Crest

Five repository tests pass (generator freshness, catalogue drift against both
sources, reset/rejection coverage, JSON validity, mirrors). Swift mirror
passes `swiftc -parse`; C++ API signatures checked against the pinned
checkout; `git apply --check` against the working tree. No compilation,
native test, simulator or browser.

## Owner steps

Apply; in the next bounded test slot run both new tests on exact frozen source
and record exits/binary hashes. A native disagreement is a product finding for
the platform that deviates from the published catalogue, not a fixture edit.
