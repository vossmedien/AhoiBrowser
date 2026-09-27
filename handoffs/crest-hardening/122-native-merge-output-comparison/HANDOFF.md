# 122 — Export and compare actual C++/Swift merge results

Status: integrated by Desktop/Mobile/Sync owner after 980c713; Swift runner parses and source diff passes; C++ compilation, native exports and actual output comparison remain pending.
Owners: desktop/sync and mobile
Base: `cfd0127` plus integrated 120 (`91d6bd1`). Patch applicability checked
against current `ce24827`; the later owner code is preserved.
Patch SHA-256: `e0106bece7069f4902f06dbaada1316fe338e167cd689d1d73f917a26fa96ddc`.

## Problem and change

The native runners check fixture expectations independently but do not expose
their actual canonical outputs. H1.3 requires a direct differential comparison
and retention of failing seeds. This handoff supplies that missing path.

`122-native-merge-output-comparison.patch` changes only the two existing merge
test runners and adds their direct `//crypto` test dependency. Complete review
mirrors are under `files/`. They retain `cfd0127`'s 112 Swift projection reader;
apply the patch rather than replacing files that contain later owner work.
No product algorithm, codec, wire format or capability is changed.

- C++ exports `SerializeRecord` of the actual merged record; Swift exports
  `DesktopWirePayloadCodec.encode` of the actual typed merge result. Expected
  payloads never supply actual output. C++ decisions remain observable.
- Decode and merge rejection stages are separated. Swift fixture/expectation/
  output-encoding failures are harness errors and cannot satisfy invalid cases.
  All cases are processed; unsupported entities no longer disappear behind
  a filtered loop or a skip. Existing scope remains seven supported entities.
- Optional explicit input override permits the same saved pair corpus to run
  in both binaries. Existing standard/supplement tests retain their defaults.
- Exports hash the fixture bytes actually loaded and are written once into an
  owner-selected directory with a unique run ID. Interrupted/stale files cannot
  be silently reused. Export completeness does not claim a successful test exit.
- Lane tools compare full coverage, actual results, fixture oracles and exact
  run receipts, including recomputed binary hashes. Identical wrong outputs,
  Unicode byte differences, pre-merge refusal of known-valid inputs, skipped
  cases and stale artifacts cannot pass. Failed assertions still produce useful
  differences and regression candidates when their output is complete.

Protocol, commands and evidence limits:
[RUNNER_OUTPUTS.md](../../../fixtures/sync-conformance/RUNNER_OUTPUTS.md).
The receipt writer never launches tests or claims source-to-binary attestation;
the owner's exact build/test record remains necessary. It records the direct
process exit provided by that record, including failures.

## Validation and owner intake

All 67 local Python conformance checks pass. They cover typed JSON equality, Unicode, exact case
coverage, false-green counterexamples, hashes, run/source identities, failed
exits and seed/input retention. Synthetic test receipts remain temporary and
are not native acceptance artifacts. Final counts/checks are in the lane
checkpoint. The 140-vector, 8-vector and 112 projection fixture bytes are
unchanged. Swift exporter mirror passes `swiftc -frontend -parse`; `git apply
--check` succeeds on `ce24827`. No native typecheck or runtime is claimed.

No C++/Swift compiler, build, simulator, browser, shared lock or external API
was used by Crest. The owner's `cfd0127` Swift window is independent and does
not include these new exporters. This patch is source-ready, not native-green.

After 120 intake, include this small runner delta in the next authorized
affected-test package. Verify export enabled/disabled, exclusive creation,
known-valid conflict stage, harness failure handling, and actual codec outputs
on the exact candidate. Preserve the direct command exit, logs, source and
binary bindings. Run the two C++ merge tests and corresponding Swift tests with
standard 140 and supplemental 8 vectors; compare each corpus separately. Avoid
an extra full browser build solely for this handoff. No concurrent test slot
or additional lease is requested here.

H1 remains open for actual native export comparison, general seeded operation
sequences, eight uncovered entity classes/new domain values, and the separate
store/apply/runtime obligations. This does not replace or relabel the accepted
`a747c62` Swift result or the owner's changed R1 verification.
