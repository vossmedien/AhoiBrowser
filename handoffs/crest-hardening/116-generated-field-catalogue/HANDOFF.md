# 116 – Generated C++/Swift field/type catalogue required by H1.4

Status: accepted (owner, 28 Sep): C++ (pinned Clang, 147 static_asserts) and Swift 6 tables compile and match the JSON, mutation-checked; evidence artifacts/tests/catalogue-116-*-20260928/. Product adoption stays the Sync owner's decision.
Owner: crest-hardening; Sync owner decides any later product-code adoption
Baseline audit: `73360d7`. This adds no product writer or wire change.

## Gap addressed

H1.4 explicitly asks for generated C++/Swift tables from `config/sync-format.json`.
The existing `tools/sync_conformance/field_groups.py` correctly compares 37
handwritten mappings but does not emit either language's catalogue. The new
`generate_field_catalogue.py` emits test/reference tables for all 15 currently
declared entities, including entity IDs, names, wire versions and field groups.
The three artifacts live under `fixtures/sync-conformance/generated/`, carry
a filtered-contract SHA-256, and are reproducible independent of entity order.

The config contains no payload value types per group, so none are guessed.
The tables represent the declared entity/type and field-group contract only.
No replacement of handwritten product codecs or version/capability activation
is performed. That optional step remains the Sync owner's decision.

## Source and checks

- Generator source and generated C++/Swift/JSON assets are written in lane paths.
- New repository test source checks freshness, field/type coverage in both
  outputs, added contract fields, manually changed generated code, invalid
  declarations and deterministic ordering. Existing test discovery includes it.
- The generation command was used to author the assets; Python sources parse.
  **No new unittest, compiler, XCTest, browser, simulator or build was run.**
  Test execution waits for the end of the current no-further-runs window.
- The 140 field-merge vectors and their accepted Swift evidence are unchanged.

This closes the missing source artifact, not all of H1. The separate full-scope
review is `docs/reviews/crest-hardening-2026-09-27-completion-audit.md`: the current
merge vectors still cover only seven of the fifteen declared entity classes;
seeded multi-operation sequences, runner-result export/comparison and remaining
exact C++ evidence still need completion. A generated type list does not prove
merge behavior or cross-device sync.

Update after `9671d0f` resource handback: the five focused Python catalogue
regressions passed in 0.007 s, and the CLI freshness check passed. This supersedes
the deferred repository-test note above only. No native compiler, app or runtime
was started; the generated C++/Swift tables are still reference assets.
