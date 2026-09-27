# 102 – Bind H3 performance to the actual release configuration and PGO profile

Status: ready
Owner lane: desktop/build (apply receipt integration in the next regular build)
Base: `e5175f8` for the tracked `tools/build_provenance.py` hunk.
Lane-owned harness/checker/helper changes accompany this handoff's commit.

## Finding and behavior change

The previous H3 evaluator compared Chromium version strings and host conditions,
but never consumed build receipts or release/PGO/ThinLTO evidence. Negative
controls against `e5175f8` gave PASS with no build receipt and with a mismatched
ThinLTO baseline. The previous app identity hashed only the launcher, so it did
not bind the Chromium framework. This contradicted PERFORMANCE_METHODOLOGY.

Now `tools/perf/build_evidence.py` verifies the receipt against the full bundle
tree, its source revision and GN/config/toolchain pins. Only Ahoi release or
full-release versus an unmodified upstream control can satisfy the budget
roles. Component/dev/dirty/mismatched source, configuration or bundles are
refused. Normal runs with missing evidence stop before any browser or fixture
server launch. Re-evaluation of unbound samples yields INSUFFICIENT.

`tools/perf/run_desktop_perf.py` gains `--build-receipt` and
`--baseline-build-receipt`. Run schema 2 retains the receipt hash, verified
build summary and engine input key. Validation/dry runs remain clearly marked.
No existing evidence artifact has been relabeled as a new acceptance.

## Owner patch

Apply `102-performance-build-receipt.patch` to `tools/build_provenance.py`.
The `files/` copy is for review; the patch is the integration delta. It imports
`perf.optimization_receipt.collect` (provided in this lane's tracked tools)
and records `build.effectiveOptimization` only for upstream/release/full-release.
It leaves development builds and the engine-input-key schema unchanged.

The helper, called only in the owner's normal build/provenance process:

1. Reads effective GN arguments (`gn args OUT --list --short`), including PGO
   phase and ThinLTO, instead of inferring defaults from `args.gn`.
2. Reads `gn desc OUT //build/config/compiler/pgo:pgo_optimization_flags cflags`
   to identify the actual `-fprofile-use` input; requires one profile.
3. Checks the pinned `chrome/build/mac-arm.pgo.txt` against the Chromium Git
   commit; requires the compiler input to be that pinned file under
   `chrome/build/pgo_profiles/`, hashes its bytes and stores no absolute path.

Command semantics: [official GN reference](https://gn.googlesource.com/gn/+/main/docs/reference.md#gn-args-command_line-tool)
and its `gn desc` section. The M153 source `build/config/compiler/pgo/BUILD.gn`
selects that compiler config/profile; the helper never invokes the downloader.
GN introspection can evaluate Chromium build scripts, so this helper remains
part of the **owner-controlled build**, not a Crest checkout command.

## Checks and remaining acceptance

- Local fixture-based tests cover missing/changed receipts, component builds,
  altered framework bytes, source-WIP isolation, toolchain/engine-key mismatch,
  baseline role, PGO pointer/profile mismatches and runner evidence persistence.
- Validation on 27 September: 64 focused local tests pass (33 performance/
  provenance, 31 network/engine-key/lane tests); Python parsing and patch
  applicability pass. Negative controls reproduce both old false PASS cases;
  the new evaluator refuses them.
- No GN command, Chromium binary, build, install, simulator or paid/E2E test was
  run by Crest. GN calls in helper tests are mocked. The real helper and schema
  are first exercised by the owner's next regular release builds; do not claim
  they have run against AhoiDev from the fixture tests.
- Existing receipts lack effective optimization evidence and intentionally
  remain insufficient for H3 budgets. Supply the two new receipts and exact
  unchanged bundle paths with a newly confirmed host-quiet lease; then the
  authorized H3 run can proceed. No extra build solely for this handoff.
- Signing/notarization/release gates remain separate. Build receipts are local
  provenance, not cryptographic publisher attestations.
