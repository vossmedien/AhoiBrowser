# Native merge output comparison (protocol 1)

This protocol compares the actual final wire payloads from the C++ and Swift
merge runners. It does not run a browser, test, compiler, simulator or service.
The native integrations are supplied in Crest handoff 122, after 120. Their
compilation and actual exports remain pending owner acceptance.

## Runner inputs and output

Normally the existing tests load `merge_v3.json` and the separate UTF-8
supplement. An optional absolute `AHOI_SYNC_CONFORMANCE_FIXTURE` path overrides
only the main merge corpus (and Swift's coverage test). It must use the same
schema as the pair vectors. Both implementations must receive the same bytes.
This allows a saved random corpus to run without rebuilding testdata. It does
not implement multi-operation sequences or add missing entity support.

To request export, the owner supplies the following environment variables to
the actual test process, in its existing authorized test window:

| Variable | Value |
| --- | --- |
| `AHOI_SYNC_CONFORMANCE_OUTPUT_DIR` | Existing absolute directory writable by the test process; new directory per attempt |
| `AHOI_SYNC_CONFORMANCE_RUN_ID` | Unique owner run identifier, 1–120 ASCII letters/digits/`.`/`_`/`-` |
| `AHOI_SYNC_CONFORMANCE_FIXTURE` | Optional absolute main-corpus path; leave unset for standard evidence |
| `AHOI_SYNC_CONFORMANCE_SEQUENCE_FIXTURE` | Optional absolute sequence-corpus path; leave unset for the pinned seed-153 corpus |

For XCTest, pass these variables through the owner's actual test environment
(e.g. `SIMCTL_CHILD_…` forwarding when applicable), and confirm the export
exists at the intended simulator/host path. A variable set only on a parent
shell is not export evidence. Keep the owner's current job/resource limits.
No authorization to start a test follows from this document.

Each fixture creates `cpp-<fixture basename>` or `swift-<fixture basename>`.
Creation is exclusive; existing files, including interrupted ones, are never
overwritten. Store retries in a new directory. Without an output directory,
the existing tests continue with no artifact write.

Each output is a JSON object:

| Field | Meaning |
| --- | --- |
| `schemaVersion`, `kind` | Integer `1`, string `ahoi-sync-merge-output` |
| `implementation` | `cpp` or `swift` |
| `runId` | Supplied owner run ID |
| `fixtureName`, `fixtureSha256` | Basename and SHA-256 of the bytes actually read |
| `complete` | Boolean true only after processing the corpus; does **not** mean all assertions passed |
| `cases` | Exactly one entry per fixture name, including rejection/error cases |

Each case includes `name`, integer `entityType`, `outcome`, `rejectionStage`
and `payload`. `accepted` carries the actual product-codec JSON object and a
null rejection stage. `invalid` carries null payload and the actual `decode`
or `merge` stage. Harness, dependency-fixture I/O, expectation decoding and
output encoding failures cannot masquerade as product rejection; Swift emits
`error` and fails the test. A C++ fatal assertion before export leaves no
complete output. Neither situation can pass the comparison.

C++ also emits its actual `decision` enum spelling. Swift has no corresponding
decision enum, so it exports only the actual outcome and payload. The exporter
never fills an actual output from an expectation or reflected model dump.
Fixture metadata `inputValid: true` requires successful decode: rejection
before merge fails even if the oracle also expects `invalid`.

## Exact-run receipt

Capture the direct test process exit, logs and binary identity in the owner's
ordinary frozen-source workflow. After it ends, use
`tools/sync_conformance/record_runner_receipt.py` for each output. Example
arguments below are placeholders for actual paths, not a test result:

```sh
python3 tools/sync_conformance/record_runner_receipt.py \
  --output /evidence/cpp-merge_v3.json \
  --fixture fixtures/sync-conformance/merge_v3.json \
  --source-tree /frozen-tested-checkout \
  --binary /tested-binary \
  --exit-code 0 \
  --receipt /evidence/cpp-receipt.json
```

Supply the captured exit instead of the example `0`. Never use a pipeline's
`tee`/`grep` status. Repeat `--binary` for the executable and relevant loaded
product/test frameworks or libraries; do not hash only an unrelated launcher.
The writer reads Git's full commit and clean worktree status, checks the loaded
fixture binding, hashes the output and binaries, and refuses an existing
receipt. Nonzero exits are recorded faithfully. Paths to binaries are stored
relative to the receipt; preserve that layout or regenerate from retained,
verified immutable artifacts. All source/build/test logs remain required.

Receipt fields are `schemaVersion: 1`, `kind: ahoi-sync-merge-run`,
`implementation`, `runId`, `fixtureSha256`, `outputSha256`, 40-character
`sourceCommit`, `sourceTreeClean: true`, `completed: true`, integer `exitCode`,
and nonempty `binaryArtifacts` (path → lowercase SHA-256).

This is local owner evidence. Hashes detect stale or changed artifacts; they
do not prove a test ran or independently establish that a binary was built
from a commit. The owner must supply that relationship through the frozen
build/test record. Compare only runs intended for the candidate under review;
a comparison of older receipts stays evidence of those older revisions.

## Comparison and failure preservation

```sh
python3 tools/sync_conformance/compare_runner_outputs.py \
  --fixture fixtures/sync-conformance/merge_v3.json \
  --cpp /evidence/cpp-merge_v3.json \
  --swift /evidence/swift-merge_v3.json \
  --cpp-receipt /evidence/cpp-receipt.json \
  --swift-receipt /evidence/swift-receipt.json \
  --preserve-failures fixtures/sync-conformance/regressions/unique-attempt
```

Run separately for the UTF-8 supplement or any saved random corpus. The tool
recomputes output/fixture/binary hashes, checks run and source bindings, requires
the complete exact case set, and compares both actual results to each other
and to the fixture oracle. A pair of identical wrong results fails too.

Comparison treats object member order as irrelevant, arrays as ordered, booleans
as different from numbers, and equivalent finite JSON numbers (`1`, `1.0`) as
equal without binary-float rounding. Strings retain exact UTF-8 bytes; there is
no Unicode normalization. Missing and null remain different. The output is the
canonical wire object, so optional-key handling belongs to the product codec,
not a blanket comparator normalization. Required explicit null fields, such as
empty home targets, remain observable. Duplicate keys, NaN/Infinity and lone
surrogates are rejected. `value_json` remains a wire string; its contents are
not silently parsed/reformatted by this comparator.

| Exit | Verdict | Scope |
| --- | --- | --- |
| 0 | PASS | Complete bound outputs, successful test exits, identical canonical results matching the fixture |
| 1 | FAIL | Concrete behavioral difference in complete bound outputs; includes failed assertion runs |
| 2 | INSUFFICIENT | Missing/stale/malformed artifacts, incomplete/error/skipped cases, or failed test exit without a comparable difference |

A nonzero test exit can never produce PASS. Complete outputs from failed
assertion runs still expose differences and preserve counterexamples. On FAIL,
the optional new directory receives the exact original fixture bytes, a
`regression-candidate.json` containing the unchanged failing cases (including
seed-bearing names and inputs), and the comparison report. Existing directories
are refused. Review and commit these cases as fixed regressions; preservation
does not automatically approve an oracle or modify the standard 140 vectors.

The separate `merge_sequences_v3.json` corpus holds 18 cases and 104
time-ordered operations. Its proposed C++ and Swift tests feed each accepted
operation the **actual** prior native merge result; an invalid step leaves
that actual state intact. Each native test checks every step's decision/result
against its oracle and exports the actual final codec payload per sequence
case in the same output format. The comparator then checks the final C++/Swift
states against each other and the fixture. A failed intermediate assertion
cannot yield a comparator PASS because the run receipt carries its nonzero
direct test exit. The pinned default seed is 153; a different seed requires a
separately generated fixture, the sequence override on both native runners,
and matching exact fixture hashes in both receipts.

The report names differing fields without dumping payloads. It lists the
actual entity coverage and exact source/run identities. A PASS here applies
only to the supplied corpus: it does not by itself complete all entity or
operation-sequence coverage, store/projection comparisons, CloudKit or
installed-product acceptance. Handoff 112's projection runners
are preserved but do not emit this merge-output protocol.
