# 120 – Opaque sort-key equality must match UTF-8 ordering and field clocks

Status: source integrated by Desktop/Mobile/Sync owner after c933764; three Swift files parse, all three shared/overlay fixture hashes match. Four new regressions and separate eight-vector readers await native RED/GREEN; prior cfd0127 evidence does not cover this fix.
Owner: mobile/sync, desktop for supplemental conformance runner intake
Base: `736207f`; no product files changed by Crest.
Supplement SHA-256: `df4833b8b0719d296ac8a935d9207624092a1ce0d787357c60af1c9209f42f16` (8 vectors).

## Finding (source-confirmed, not a new native runtime result)

736207f correctly compares order with UTF-8 bytes. The adjacent field-merge
paths still compare Workspace `sort_key` and `TreeLocation.order` through
Swift `String ==` (including synthesized `Equatable`). Swift treats canonical
Unicode equivalents as equal even when their underlying scalars differ:
[official Swift language guide source](https://raw.githubusercontent.com/swiftlang/swift-book/main/TSPL.docc/LanguageGuide/StringsAndCharacters.md),
"String and Character Equality". The new owner Unicode-order test already
illustrates the distinction.

Counterexample: decomposed `U+0065 U+0301` versus composed `U+00E9`.
UTF-8 is respectively `65 cc 81` and `c3 a9`; these are different opaque wire
positions, though the text compares equal in Swift. C++ `FieldEqual` in
`sync_field_values.cc` compares `std::string` bytes for both relevant groups.

Consequences in current Swift source:

1. Equal field clocks plus these different sort keys do not raise the required
   `equalClockConflict` for Workspace `sort_key` or TreeNode `location`.
2. `stampLocal` can keep the old field clock even after the sort-key bytes
   changed, making the local wire mutation inconsistent with that clock.
3. The conformance runner's leaf-string comparison uses `String ==` too, so
   byte-different successful results can be hidden by the same equivalence.

This is an existing adjacent equality gap exposed by the explicit opaque-byte
ordering rule, not a finding that 736207f's lexical position algorithm is wrong.
Stable tail projection and 118's real mutation/replay acceptance remain separate.

## Minimal patch

`120-utf8-sort-key-equality.patch` is a complete owner delta (including new C++
testdata); `files/` mirrors are for review, not replacing newer whole files.

- `CompanionFieldMerge`: a private `WireSortKey` byte-equality wrapper for
  Workspace incoming comparison/local stamping/projection equality and
  TreeLocation. This keeps conflict detection and changed-field clocks aligned.
- Four Swift regression methods in the existing reorder test file: equal-clock
  conflict in both directions for Workspace/TreeNode, plus local stamping and
  winning-byte preservation. Byte assertions use UTF-8 arrays, not `String ==`.
- Shared supplement `merge_utf8_sort_keys_v3.json`: 8 Workspace/TreeNode cases,
  equal-clock conflict in both directions and newer/older key selection.
  `inputValid: true` is fixture metadata, not a new wire field. Both proposed
  runners assert successful input decoding first, so an unrelated decoder
  refusal cannot fake a merge-conflict pass.
- C++ and Swift runners gain a separate supplemental test; the existing
  140-vector file/test is preserved. Swift result diagnostics compare leaf
  UTF-8 bytes and verify type coverage for each loaded document.
- BUILD.gn registers the supplemental data. No normalizer, key algorithm,
  wire version, capability or product text-equality policy is changed.

Other human-readable string fields and missing entity coverage remain part of
the full H1 audit; this patch is intentionally confined to opaque order keys.

## Validation and owner execution

All 46 local conformance tests pass, including four new UTF-8 fixture checks:
generation/mirror equality, valid complete input maps, NFC-equivalent but
byte-distinct conflict inputs, and exact winning bytes with newer clocks.
Patch applicability checked without applying it. The accepted 140-vector and
112 fixture hashes are unchanged. Python parsing/diff/lane checks pass.
No Swift/C++ compiler, XCTest, simulator, browser or runtime lease was used.
The official-language/source argument and Python fixture checks do not replace
native RED/GREEN evidence.

On the next owner-authorized candidate: first run the added Swift regressions
without the production equality hunk (expected RED), then apply it and run the
full reorder/conformance classes for GREEN. Run both C++ conformance tests with
the old 140 plus supplemental 8 vectors. Bind source and both fixture hashes.
Do not relabel the prior `a747c62` 140-vector pass as acceptance of this change.

Coordination note: the owner was adding 112 projection runners in parallel.
The BUILD.gn hunk uses one stable context line to preserve that separate data
registration. Patch applicability is checked against that observed WIP; the
full review mirrors stay at committed `736207f` and intentionally do not contain
or overwrite the owner's unpublished projection work. 114 was meanwhile
integrated as `2bd75cb`; this is separate from the Unicode finding.
