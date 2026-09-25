# Desktop performance methodology

Binding for Master package 5 and the `PERF-*` / `LEAN-01` cases. Tooling:
`tools/perf/run_desktop_perf.py` (runner), `tools/perf/perf_stats.py`
(statistics and budgets), `tools/perf/cdp.py` (DevTools client). Origin:
[Crest-Konvergenz-Härtung H3](../outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md).

A performance claim ("faster", "lighter", "within budget") is valid only for the
exact candidate binary, against a baseline measured by this method in the same
session. Anything else is a note, not evidence.

## 1. What is compared

| Comparison | Candidate | Baseline | Answers |
| --- | --- | --- | --- |
| Ahoi overhead | Ahoi build of profile P | unmodified Chromium of the same pin, built with the matching upstream GN arguments of profile P | Does the Ahoi layer cost more than the budget? |
| Release reality | Ahoi `release`/`full-release` | the same unmodified Chromium in release configuration | Release budgets `PERF-01/02/06/07`, `LEAN-01` |
| Regression | Ahoi candidate N | Ahoi candidate N−1, same profile | Did this package regress? |

Rules:

- **Same engine configuration on both sides.** A development build (no PGO,
  no ThinLTO, `is_official_build=false`) is only compared with a development
  build of unmodified Chromium. Development numbers never support a release
  budget claim.
- **PGO/ThinLTO only with the pinned profile.** A release comparison uses the
  mac-arm PGO profile named by the Chromium pin on both sides; a candidate
  with a different or missing profile is not comparable.
- **Component builds are never measured for budgets.** `config/build/ahoi-dev.gn`
  sets `is_component_build = true`; its start time and memory are dominated
  by dynamic loading of hundreds of libraries. Dev candidates are used only
  for harness validation and within-profile trend checks. Budget verdicts
  need `ahoi-release`/`ahoi-full-release` against the unmodified
  `upstream-release` control (`scripts/build-upstream.sh`,
  `config/build/upstream-release.gn`), both built by the desktop owner with
  the reference toolchain. Since the user's decision of 25 September 2026 the
  reference toolchain is Xcode 27 (handoff 022); budget verdicts wait only for
  those two release builds and a quiet host.
- **Same pin.** `chromiumVersion` must match (the runner reads it from each
  bundle and refuses a mismatch as `INSUFFICIENT`).
- The candidate is identified by `binarySha256` and, once handoff 001 is
  integrated, by its receipt's `engineInputKey`.

## 2. Host conditions

The runner refuses to measure (exit 7) unless all hold, and records them in
every run file:

- no build, compiler or linker activity (`ninja`, `siso`, `clang`, `lld`,
  `swift-frontend`, `xcodebuild`, `rustc`) and a 1-minute load average below
  30 % of the core count;
- AC power; thermal state recorded before and after;
- the owner is away (no HID input for at least 300 s), because runs open
  windows;
- the DevTools port is free;
- `/Applications/AhoiBrowser.app` only with a confirmed `installed-app` lease
  (`--lease`); a copy elsewhere needs none. Performance runs also need the
  `host-quiet` lease from the desktop owner.

**Accessibility state is part of the conditions.** An attached accessibility
client (VoiceOver, an AX-driving E2E tool, Accessibility Inspector) switches
Chromium into full accessibility mode and measurably lowers Speedometer and
raises memory. Running AX clients are recorded; a candidate/baseline pair with
different AX state is `INSUFFICIENT`. Never disable renderer accessibility in a
product configuration to improve a score; a diagnostic run with
`--disable-renderer-accessibility` is labelled as such and never used for a
budget verdict.

Fixed per run: window size 1440×900 at the origin, disposable profile per
scenario, identical flags on both sides (recorded), local fixture server on
loopback for all non-benchmark pages.

## 3. Run protocol

1. Dry run (`--dry-run`) to record both bundles' identity.
2. Candidate and baseline are measured in one invocation and **interleaved**
   (A, B, A, B, …) so host drift affects both.
3. At least 5 samples per metric per app (`--runs`, default 5). A metric with
   fewer samples, or whose relative spread (MAD / median) exceeds 10 %, is
   `INSUFFICIENT` and must be re-run, never averaged into a pass.
4. Results are written to `artifacts/perf/<source>-<date>/` as
   `candidate-run.json`, `baseline-run.json` and `evaluation.json`.

## 4. Metrics

| Metric | Scenario | Definition |
| --- | --- | --- |
| `startup_first_launch_ms` | `startup` | process spawn → `loadEventEnd` of a local start page, fresh profile (first launch) |
| `startup_warm_ms` | `startup` | same, second launch of that profile |
| `devtools_ready_*_ms` | `startup` | spawn → DevTools endpoint answers (diagnostic) |
| `memory_1_tabs_kib`, `memory_20_tabs_kib` | `memory` | sum of RSS over the browser process tree after 20 s settle; local content pages |
| `processes_*_tabs` | `memory` | process count of the tree |
| `idle_cpu_percent` | `idle` | CPU time of the process tree over 60 s after 30 s settle, one static tab, in % of one core |
| `speedometer_score` | `speedometer` | Speedometer 3.1 score (`#result-number`) from browserbench.org |
| `command_bar_ms` | `trace` | duration of `Ahoi.CommandBar.RebuildSuggestions` per keystroke (handoff 002) |
| `workspace_switch_ms` | `trace` | duration of `Ahoi.Workspace.Switch` (handoff 002) |

Known limits, stated in every report:

- RSS double-counts shared pages. It is used only for like-for-like
  comparison; physical-footprint accounting is future work.
- `command_bar_ms` covers ranking and row construction, not the following
  paint; `workspace_switch_ms` covers the commit, not the first presented
  frame. PERF-04's separate "first feedback" and "animation end" still need
  frame timing.
- The Speedometer result selector is not yet validated on a real run; a run
  without a score fails loudly.
- Not covered by the runner: PERF-05 (10,000-node scroll), PERF-08 to PERF-15,
  GPU use, wakeups, and the 100-tab Memory Saver session (PERF-11). They keep
  their own evidence requirements.

## 5. Budgets and verdicts

`perf_stats.evaluate` applies:

| Budget | Rule |
| --- | --- |
| PERF-01 | Speedometer: worse side of the 95 % bootstrap interval of the median ratio at most 3 % below baseline |
| PERF-02 | warm start (and first launch as `PERF-02-first`): at most 10 % slower, same interval rule |
| PERF-03 | `command_bar_ms` p95 < 50 ms (absolute) |
| PERF-04 | `workspace_switch_ms` p95 < 100 ms (absolute) |
| PERF-06 | 20-tab memory: at most 5 % above baseline, same interval rule |
| PERF-07 | idle CPU: candidate median at most max(3 × baseline MAD, 0.1 percentage points) above baseline median |

Verdicts are `PASS`, `FAIL`, `INSUFFICIENT` (too few or noisy samples,
missing baseline, mismatched conditions) and `NOT_MEASURED`. An evaluation
passes only if at least one budget passed and none failed or was insufficient.
`INSUFFICIENT` and `NOT_MEASURED` are never reported as success.

## 6. Example

```sh
python3 tools/perf/run_desktop_perf.py \
  --app /path/to/AhoiBrowser.app --baseline-app /path/to/Chromium.app \
  --scenario startup --scenario memory --scenario idle --runs 5 \
  --output artifacts/perf/<source>-<date>
```
