# Active Crest-hardening lane checkpoint

Goal: [Crest-Konvergenz-Härtung](../outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md).
Lane: `crest-hardening`. Boundaries: [`config/agent-lanes.json`](../config/agent-lanes.json).
Boundary check for the orchestrator:
`python3 tools/check_lane_boundaries.py --all --since <base> --worktree`.

This lane never builds, installs, refreshes the overlay, writes under
`overlay/`, `patches/`, `apps/` or `scripts/`, or stops foreign processes.

## Current continuation — 27 September 2026, Codex handover

Owner of this lane: Codex thread `01a0e02e-7d70-7d51-afae-6e8732800811`,
succeeding stopped Claude session `588173fa-049f-4f4a-9e2e-713e961bab59`.
The private handover manifest verified all 37 archived files without a hash or
size mismatch. The original full H1–H6 goal (archive goal section 878) is now
an active native Codex goal; the user's later H7 extension remains included.
No other lane or worktree ownership transfers here. Former helper tasks are
historical evidence; none has been restarted.

Coordination confirmed at 2026-09-27 00:38 UTC: Codex
`01a0e047-9360-7122-ad28-f76ebc767c97` takes desktop/mobile/sync from stopped
source `4203bcfe`, first inspecting the leftover Rust workaround read-only.
096 is in that owner's intake. Crest holds no build, app, simulator, measurement
or coordination lock and has no background writer/watcher; its changes are
committed in `1c6d15a`. The historical `build.lock`, `e2e.lock` and `h3.lock`
were absent. A transient foreign xcodebuild seen in the first process snapshot
had exited by the immediate follow-up; this is no quiet-host certification.
The owner explicitly confirms that old H3/H5 `open` leases are stale. Await a
new candidate-bound lease in the owner's checkpoint before any resource use.
The desktop owner initially reported its inherited Master goal reached/inactive.
Its current checkpoint now corrects that as a handover parser error: Root found
`met=false` and an authentication-error sentinel, not completion. The owner
restored its full native Master goal; Crest's separate goal stays active.
Ready 098 (Mobile undo guard) and 100 (build-resumption evidence, including the
missing unit-failure stop before installation) remain for owner review.

Current user restriction: paid OpenAI API, E2E and judge tests remain paused
pending renewed authorization. This continuation uses local source review and
repository unit checks. No browser, simulator, build, install, API-key access
or runtime measurement has been started. Old `open` lease entries are not a
new exact-candidate quiet-host handoff.

Live read-only check at handover (repository HEAD `e9d25be`):

- Installed bundle still reports source `6acd207f19b5ba0357240b1b90f078beb4eec09c`
  (build 40, `dev`, Chromium `153.0.8010.53`).
- **Build 41 is stopped, not compiling.** The persistent owner queue records
  `00:50:12 start free=152GiB`, `00:50:13 commit 9bc5924`, then
  `00:50:47 overlay 1`. Its `41/overlay.log` says the Chromium checkout
  matches neither the recorded applied overlay tree nor the freshly composed
  tree, so refresh was refused to preserve foreign/partial edits. No active
  Ahoi/Chromium compiler, Ninja or xcodebuild process was found. Only the
  desktop owner may reconcile that protected state and resume the queue.
- Other lanes have uncommitted Mobile/Sync/product-contract changes. They stay
  untouched. In particular, the Swift `merged_into` work is not a frozen
  candidate and cannot substantiate an exact-revision pass.
- Last lane implementation: 096 (`f05eb75`), 134 merge vectors with three new
  Workspace merge/undo cases; desktop's testdata copy and both owner runners
  still require intake/rerun. H1's previous 131-vector pass remains historical
  evidence, not a pass of 096.
- H4 and H5's recorded exact-candidate DoD evidence stays valid; no repeat is
  needed solely because the provider changed. H3 still needs the owner's
  `upstream-release`/`ahoi-release` bundle pair, receipts and a current
  `host-quiet` window. H2/H6/H7's remaining visible journeys and real CloudKit
  cases remain open under the owner and test gates.

Continuation results:

- Local lane checks: 24 Sync conformance tests (23 pass, one expected skip:
  desktop's stale vector copy), plus 47 lane-boundary/engine-key/performance/
  network-audit tests, all pass. The network and CDP tests use local fixtures
  and mocked browser processes; no browser or public API was launched. Drift
  gate: 37 field-map copies, zero findings; generated vectors current.
- H7 source review found an unsafe mobile undo after a child is added under a
  merged folder. [098](../handoffs/crest-hardening/098-mobile-merge-undo-descendants/HANDOFF.md)
  supplies the minimal guard and four regression methods. Patch applicability
  and Swift parsing pass; owner XCTest RED/GREEN and integration remain open.
- Mobile 086/088/090 are already integrated at `e9ba41a` (315 core tests, no
  failures according to the Mobile checkpoint); the old H7 row below that
  queues them after H3 is superseded. Their visible checks remain open.
  Handoff 088 explicitly leaves the ADR 0012 neighbor-page drag preview
  unimplemented; do not count the entire flick contract as complete.

- [100](../handoffs/crest-hardening/100-build41-resumption-evidence/HANDOFF.md)
  records the stopped build, queue file hashes, a missing unit-test failure
  gate before installation, and the concrete owner recovery/evidence steps.
- Read-only extraction of `.work/agent-queue/core-084.xcresult`: 315 total,
  313 passed, 2 skipped, no failures; H7 core and Sync conformance suites pass.
  The 134-vector file hash and frozen Swift revision are not bound in this
  result, so 096 remains pending exact-candidate evidence.

H3 continuation (local, no measurement): the previous evaluator accepted both
missing build receipts and mismatched ThinLTO configurations as PASS (negative
controls against `e5175f8`). `tools/perf/build_evidence.py` now binds a run to
its exact bundle tree, immutable source GN profile, Chromium pin, Xcode/SDK,
compiler hashes and engine input key. Budget results require effective PGO/
ThinLTO evidence; old unbound runs are INSUFFICIENT. The runner checks this
before starting a browser. Handoff 102 supplies the build-owner integration
for collecting those effective defaults and the pinned compiler PGO profile
hash during the next regular release builds.
[102 is ready](../handoffs/crest-hardening/102-performance-build-receipt/HANDOFF.md):
64 focused local tests pass (33 performance/provenance plus 31 network-audit,
engine-key and lane tests); separate negative controls reproduce the old
evaluator's two false PASS results.
Patch applicability, Python parsing and lane-boundary checks pass. No GN or
browser process was run; the owner must exercise the collector during the
regular release pair. The prior goal turn established resource/ownership
handoff (progress); this continuation adds executable H3 evidence enforcement.

Latest owner intake checked after `9a12f51`:

- 096 integrated in `de52766`: canonical and Desktop testdata files are both
  134 vectors with SHA-256
  `c851cd9ec3091ba864b5f78613a53a4f52758e40adfe0a5830fa1cb2328a153d`.
  Exact C++/Swift execution evidence remains pending.
- 098's complete guard/test patch is present in Mobile commit `4f2b154`
  (`git apply --reverse --check` succeeds). This also freezes the inherited
  Swift `merged_into` work and additional owner regression coverage. It is
  source integration, not an executed XCTest/visible acceptance claim; the
  owner still updates 098's status/evidence.
- 100 now records verified recovery: owner restored only the pinned temporary
  Rust/V8 workaround bytes with mtimes, verified the old overlay and fixed the
  queue's unit-failure stop. Build/candidate/runtime follow-up remains open.
- Desktop's latest checkpoint reports heavy host load and explicitly allows
  only lightweight source work pending capacity recovery. No Crest runtime or
  resource lease is active.

084/098 owner source review (`4f2b154`, checkpoint `c4cd938`): equal-clock
Workspace deletion now includes `mergedInto` in equality, projection and local
stamping; 098 status is source-integrated with XCTest/visible checks pending.
Mobile incoming-node re-homing remains explicitly with the owner and incomplete.

[104](../handoffs/crest-hardening/104-merge-target-conformance/HANDOFF.md) adds six
common merge-target cases (equal-clock destination conflicts/presence, duplicate,
newer/older target selection, plain deletion clears target), taking H1 to **140
vectors**, SHA-256 `da09b4c1feb6247c30975376642893eb95a56a4ce0a3f3bcf059327121efacf8`.
26 local conformance checks: 25 pass, one expected skip until Desktop refreshes
its 134-vector copy; generator current, 37 field maps with zero drift. A local
scalar-tombstone negative control distinguishes the previously missed conflicts.
No new product runner, simulator or runtime was started. 104 needs owner intake
and candidate-bound results; 096's successful 134-vector copy remains recorded
as its own completed source step.

H3 cleanup review (lightweight source work under the renewed heavy-host gate):
[106](../handoffs/crest-hardening/106-perf-abort-cleanup/HANDOFF.md) fixes leaked
browser/trace-driver processes on constructor or scenario failure. All scenarios
now own cleanup scopes; failed reaping preserves the profile. Incomplete runs
write only explicit aborted evidence, never a budget verdict, and cannot reuse
a previous evidence directory. Nine new mock-only regression methods pass.
No browser, GN, simulator, signal to a real process or runtime lease was used.
Final impacted checks: 56 local performance/network tests pass; lane and diff
checks are clean. No live runtime acceptance is claimed.

Restart handoff requested by Root (Terminal Cockpit build 1158, two jobs):
this lane's local checks have finished and all source progress is being
committed before reporting restart readiness. Crest has no critical process,
shared-resource lock, browser/simulator or background worker to resume. No new
expensive action will start for the restart window. Preserve the native Crest
goal and ownership boundaries; resume at the next allowed source step below.

084 design feedback to the owner: [108](../handoffs/crest-hardening/108-review-merge-presentation-projection/HANDOFF.md)
confirms a pure wire-neutral presentation projection is contract-compatible.
Concrete remaining apply gaps: root-end ordering and C++ recovery-folder versus
Swift root fallback for an invalid parent after successful target resolution.
Shared apply cases must also cover undo/delivery-order convergence, passive
capture without Page-clock changes, explicit mutations and retention of the
merge destination through tombstone compaction. This was a read-only review of
owner WIP; no product change, test or shared-resource action was started.

Post-restart continuation: the live process-name snapshot showed no Ahoi,
Ninja, xcodebuild or measurement process; no old background task was restarted.
Owner intake progressed: 102 source integration `1c60f31`; Mobile projection
freeze `6f7fafc`; 104's 140-vector Desktop copy `61b9e93` / checkpoint `56951bd`.
These are source steps, not new runtime leases or acceptance.

H3 runtime guard is implemented and locally verified in lane source:
one current owner-checkpoint lease marker binds candidate/baseline bundle
hashes, resources, mode and expiry. The runner atomically claims its own H3 lock
and checks revocation, owner locks, HID input, compiler activity, power and AX
state during the run. Only registered harness sessions can be stopped; failed
cleanup retains the lock/profile. Waits and CDP reads are cancellable; cancelled
or unmonitored runs cannot satisfy a budget.
[110](../handoffs/crest-hardening/110-performance-runtime-lease/HANDOFF.md) is ready:
91 local performance/network/engine-key/lane tests pass, including 17 guard
regressions and real guard scopes around simulated runner success/revocation.
All OS probes/process creation/signals for guard tests are mocked; monitor
threads terminate within the test scope. Diff/parse/lane checks pass. 102's
owner integration was checked by reversing its patch in check-only mode.
No actual lease marker was written into the owner checkpoint and no runtime
measurement was launched. This continuation made source/test progress rather
than waiting on a background process.

Owner source `61cab96` (108 R2) reviewed: successful merge re-homing now
puts invalid-parent nodes at the target root and preserves valid parents,
matching the shared apply cases. No compiled/runtime pass inferred.

[112](../handoffs/crest-hardening/112-workspace-merge-projection-fixture/HANDOFF.md)
is ready at the owner-selected
`fixtures/sync-conformance/workspace_merge_projection_v3.json`: 23 cases,
31 full raw-v3 frames and 9 independent Workspace/Node array-order combinations
per frame (279 projections). Hand-derived expected membership, parent and
sibling order cover R1/R2, late subtrees/known moved parents, delivery ordering,
undo and explicit local movement. Raw before/after byte/clock preservation is
required; unresolved classifications do not normalize platform recovery.
SHA-256: `4dd5370f84742aa022a6f690d075f5313db9aa25be37254780eefafa0a317dc8`.
The 140 field-merge vectors remain unchanged. All **37 local conformance
checks pass without skips**, both generator freshness checks pass, and the
37-copy field-map gate reports zero drift. Fixture/mirror are byte-identical;
Python parsing, diff and lane checks pass. These checks validate fixture data
and hand-derived expectations, not 279 executed product projections. Owner
C++/Swift adapter runners and retention remain open; no concurrent product
writer or shared-resource run was started.

Latest owner runtime evidence `443c452` reviewed read-only:
[mobile merge receipt](../artifacts/tests/mobile-merge-a747c62-20260927/README.md)
binds clean `a747c62a74219c807b0bcf232bab29e5586248fc`, Xcode 27, framework/test
binary hashes and 104's 140-vector hash
`da09b4c1feb6247c30975376642893eb95a56a4ce0a3f3bcf059327121efacf8`.
Final GREEN: **27 XCTest, 0 failures, 0 skips** (merge 19, reorder 2, save 4,
conformance 2, all 140 vectors). 098 RED removed only the guard in the isolated
worktree: three critical refusal methods failed, unrelated-root control passed;
byte-exact restoration then passed 27/27 again. This closes the Swift 140-vector
and 098 RED/GREEN evidence gaps for that freeze; it does not prove the new 112
apply fixture, C++ execution, R1 ordering, retention, visible E2E or CloudKit.
C++ R2 `61cab96` and the 140 native vectors remain NOT_RUN.

Root 1159 restart coordination: owner reports all Ahoi locks/tests ended and
requests **no further runs now**. Crest only read the saved receipt/summaries
and updated this checkpoint; no suite, build, GUI or runtime was started.
No critical Crest background task or resource lock is active. Preserve the
original active goal and next steps through the restart.

Retention source `5a7d6b3` / owner checkpoint `31f592f` reviewed:
[114](../handoffs/crest-hardening/114-review-compacted-merge-routing/HANDOFF.md)
records the atomic local route/watermark/payload lifecycle and mixed-chain
resolution as consistent in the normal source path. One adapter edge remains:
when no authoritative live Workspace exists, stale native fallback A can be
inserted as live before resolving retained A→B, shadowing the compacted route.
114 supplies a minimal skip condition and two regression sources, plus full
stored-Page/field-map/serialized-byte comparison for the compaction test.
Patch applicability checked only; no build, test, GUI or runtime run occurred.
All native execution, 108 root ordering, Mobile compaction and 112 apply
acceptance remain open. Delayed Root 1159 restart readiness is preserved.

Full requirement-level audit is now recorded in
[the completion audit](reviews/crest-hardening-2026-09-27-completion-audit.md).
It supersedes old broad "all lane work/H1 done" wording without invalidating
scoped accepted evidence. Major remaining **source** gaps: only 7/15 declared
entities have shared merge vectors; no direct runner-output differential tool
or general seeded operation sequences; H3 startup uses loadEventEnd instead of
first paint and lacks full requested scenario/budget evidence; H7 drag preview,
Files-based spike input and parts of Desktop merge/undo remain incomplete.
Thus the goal is not merely waiting on owner runtime/credentials.

[116](../handoffs/crest-hardening/116-generated-field-catalogue/HANDOFF.md) supplies
the H1.4 generator and test/reference C++/Swift/JSON tables for all 15 declared
entity types/field maps. Generated assets are authored, Python sources parse,
and new repository test source is prepared. **No new test suite/compiler or
runtime was run** under the continuing Root 1159 no-further-runs instruction.
The existing product codecs, wire default, capabilities and 140-vector fixture
remain unchanged. Catalogue tests/consumer compilation are still pending.

Current coordination, owner report at 03:54 CEST: Root granted Desktop a
bounded window of at most two jobs, with no active compiler at its preflight
and 59–65% idle / 49% free memory. Desktop claims `build.lock` for a finite
read-only Ninja flag query and direct Clang `-fsyntax-only` phases (maximum two
compilers), rechecking capacity between phases. No GN, overlay refresh, link,
browser or full Chromium build is authorized by that window. The owner avoids
the old syncheck.sh pipeline that masked compiler exit status and records direct
argv/exits plus a negative source control. These are the owner's reported scope
and preflight, not Crest measurement evidence.

That bounded slot is now returned (`9671d0f`): the saved receipt binds five
successful direct Clang syntax checks on `73360d7`, a `#error` control with exit
1, at most two processes and phase measurements. No objects, link, tests or
app execution occurred. The owner shut down only CE3513BF on Root's instruction.
Root lifts the readiness waiting phase for lightweight source work; there is
no immediate restart and no general Chromium build slot. The old hold above
is historical. No current Crest H3/H5 runtime lease is open.

Crest holds no shared lock and starts no compiler, simulator or runtime.
Light local catalogue checks can resume after the explicit slot return; paid
API/E2E/judge restrictions and ownership stay unchanged. Native syntax evidence
is not native behavioral execution or acceptance of unintegrated 114.

R1 operational follow-up [118](../handoffs/crest-hardening/118-projected-workspace-order-mutations/HANDOFF.md)
adds nine production-writer reorder/insertion cases on 112's unchanged seeds,
including a native Page moved after a projected one, insertion between them,
projected folders, deleted anchors, rename stability and received undo. Repeated
projection/reload must preserve the user's order without passive Page-clock
writes. No derived-key spelling is imposed; the owner continues the algorithm
and opaque-key writer. The frozen 112 JSON/hash remains unchanged.
After the explicit slot handback, the five focused local Python catalogue
tests from 116 pass (0.007 s), as does generated-table freshness. Native consumer
compilation remains pending. No compiler, simulator, browser, H3/H5 measurement
or paid test/API was started by Crest.

Owner lexical-position prerequisite `736207f` / checkpoint `8812712` reviewed:
UTF-8 comparisons and production create/move/merge/save/capture/reorder routing
are source progress, not the remaining stable-tail R1 or runtime acceptance.
Review found adjacent opaque-key equality still using Swift canonical String
equivalence in the Workspace/TreeNode merge and local-stamp paths.
[120](../handoffs/crest-hardening/120-utf8-sort-key-equality/HANDOFF.md) supplies
the minimal byte-equality patch, four Swift regression sources, and both runner
extensions for a separate **8-vector** UTF-8 supplement, SHA-256
`df4833b8b0719d296ac8a935d9207624092a1ce0d787357c60af1c9209f42f16`. Both proposed runners require valid decoding before
accepting an expected merge conflict. All 46 local conformance tests pass,
including four supplemental fixture checks; patch applicability passes against
the observed owner WIP with minimal stable BUILD.gn context. Mirrors deliberately
exclude unpublished owner code. No native compilation/runtime was performed.
Existing 140-vector and 112 fixture hashes are unchanged; no parallel product
writer. The owner meanwhile integrated 114 as `2bd75cb` (source only; no newer
native behavior result inferred).

Owner source `cfd0127` is frozen for the new R1 tail projection and both 112
runners (23 cases / 31 frames / 279 projections); integrated 114 is included.
The owner reserves build.lock and owned CE3513BF for bounded Swift verification,
one job and no parallel tests. Crest holds no resource and starts no compiler,
C++ run, browser or simulator; no new runtime lease is inferred. The older
`a747c62` acceptance is not relabelled for this changed source.

H1.3 continuation: lane-owned strict output comparator and negative-control
sources are authored. Actual canonical C++/Swift payloads, complete case sets,
fixture/output/binary hashes and successful frozen-source receipts are required;
native exporters are delivered as ready 122 on top of integrated 120. This is source
work; no new native result or test pass is asserted yet.

Owner `cfd0127` now has exact Swift **34/34** for 140 merge vectors, 112's
23 cases / 31 frames / 279 projections, and direct move/insert/reload/undo.
Three affected C++ units passed syntax/type analysis only. Owner released
build.lock and shut down CE3513BF; 120 integrated as `91d6bd1` with source
checks, while its native RED/GREEN remains pending. This changed R1 source
supersedes the earlier pending-runtime wording for that Swift scope, but does
not satisfy 118's full Desktop allocator/capture/replay or cross-platform
actual-output obligations. 122 does not alter those evidence boundaries.

[122](../handoffs/crest-hardening/122-native-merge-output-comparison/HANDOFF.md)
is now **ready with frozen patch** SHA-256
`e0106bece7069f4902f06dbaada1316fe338e167cd689d1d73f917a26fa96ddc`,
applicable to current `ce24827` after integrated 120. It exports observed
product-codec merge results without copying fixture expectations, and adds a
strict lane comparator plus receipt writer and protocol. **67** local Python
conformance tests pass, Swift mirror parses, and patch applicability succeeds.
No native exporter compilation or actual C++/Swift output comparison has run.
The 140/8/112 fixture hashes remain unchanged; no shared resource is held.

Owner source `cbcf81c` has since integrated 122's exact patch after 120;
`git apply --reverse --check` succeeds. Swift parsing and source diff checks
are owner-reported; actual native exports, direct comparison and C++ runtime
remain pending. Concurrent owner Mobile compaction source is outside Crest's
write scope and is not acceptance evidence yet.

118 follow-up source review at `ce24827` found two independent edge gaps:
[124](../handoffs/crest-hardening/124-desktop-utf8-order-allocation/HANDOFF.md)
shows the production Desktop midpoint allocator turns valid bounds `é`/`€`
into the invalid single byte `d2`; the existing C++ R1 move test writes its
own `+ "@"` key and cannot cover this. [126](../handoffs/crest-hardening/126-merge-root-marker-collision/HANDOFF.md)
shows a normal raw root key containing the merge-root marker is treated as an
authored segment key by both platforms, changing visible order without a
merge. Both findings include exact source paths, inputs and required native
capture/replay checks. They are source findings, not new runtime failures.
Owner product paths and resource locks remain untouched.

Root's new account/model handback removes the old Cockpit restart wait. Current
Root builds Cockpit with two jobs, so Crest starts no heavy compiler, simulator,
browser or measurement action. The installed-app/host-quiet H3 lease and the
external H5/CloudKit/decision gates are unchanged. Crest's own paths were
clean at `8081174`; no concurrent product writer was started.

H3 source continuation: `startup_first_launch_ms` and `startup_warm_ms` now
measure Chromium's actual `first-paint` Performance Entry for the local start
page, bounded by process spawn and the fixture server's receipt time. Missing
paint aborts that sample; `loadEventEnd` is diagnostic only. Scenario version
2 and a negative evaluator control make all old version-1 load-end startup
results `INSUFFICIENT` for PERF-02 even when both apps used version 1. The
methodology distinguishes page paint from the first native Ahoi window frame.
All **62** focused local performance tests pass, including missing/invalid
paint marks and legacy false-PASS prevention; Python parsing/diff checks pass.
This is source/evaluator evidence, not a real-candidate paint measurement or
full H3 budget result. Foreground/window, Memory Saver, presentation frame,
release pair and current lease work remain open.

H3 aggregate verdict correction: a focused run with one budget `PASS` and
other budgets `NOT_MEASURED` previously emitted `evaluation.pass=true`, despite
the methodology's full-run rule. The evaluator now requires every listed
budget to pass for its aggregate `pass`, while preserving each focused budget
verdict. Negative partial-run and positive complete-run controls are added;
**64** focused local performance tests pass. This does not
extend the harness to Master budgets it has not implemented.

H1 source continuation found an actual field-merge divergence in the two
previously uncovered product entities 9 and 10. C++ merges inventory and
developer-asset field clocks; Mobile's `selectRecord` picks one complete
record, dropping an offline name edit when a later enable edit arrives.
[128](../handoffs/crest-hardening/128-inventory-asset-field-merge/HANDOFF.md)
contains a six-vector valid Format-3 supplement (both orders and equal-clock
conflicts), SHA-256
`e02a1de79e8d14a3b56bd690728d525b652e9b081d2727a91c3ccc6d58e80ea1`,
and a complete owner patch for typed Mobile merges, caller paths, C++/Swift
runners and two real upsert regressions. The existing 140/8/112 fixtures stay
unchanged. **70** local Python conformance tests pass; five Swift mirrors
parse and patch applicability succeeds. Owner native RED/GREEN, C++ execution,
122 actual differential comparison and outbound authorization review remain
pending. Six of the original eight uncovered entity classes are still open.
Root's Cockpit jobs2 build does not grant an Ahoi heavy slot; Crest uses no
shared resource.

Owner coordination after 128: 122 is integrated as source (`cbcf81c`), but
native C++ execution/direct output comparison remain pending. Mobile route
watermark and resurrection guards (`980c713`, `9c6c38e`) are source work;
exact upload acknowledgement and two-file purge remain open. The owner reports
an 11:24 CEST load sample of 155 with 0% CPU idle, concurrent Cockpit build
1162 and a foreign iOS test. Crest starts no native test/compiler/simulator or
H3/H5 action from this state, and no Crest runtime lease is open.

[130](../handoffs/crest-hardening/130-remaining-entity-merge-vectors/HANDOFF.md)
adds **36** valid shared pair vectors for the six classes still absent after
128, SHA-256
`516d5bfa2665bc28e387b055e305896c717a87e3e119460d0f6360f6473d9f38`.
The incremental test-runner patch SHA-256 is
`202a75b9080e9500d5b7d691378d68cb691ab390194c7d6c38e7487c49b06cb4`;
it depends on 128. Both patches apply in sequence to disposable current
committed owner source. The Swift runner mirror parses and **74** local Python
conformance tests pass. This is fixture/runner source, not a native pass. If
128 and 130 are integrated and verified, every declared EntityType has at
least pair-merge coverage; H1's general operation sequences, domain values,
actual differential results and store/cloud behavior stay open.

Next allowed lane work: finish H1's eight missing entity classes and seeded
operation sequences, and the remaining H3/H7 source gaps. Owner integration
and actual 122 differential runs remain open. Runtime work still requires its
own newly open owner lease. Actual H3/H5 runs need matching candidates and a
newly open owner lease; test/capacity gates remain.
No polling/background resource watcher was installed. The older dated sections
below are retained as history and do not override this continuation.

## Current state — 25 September 2026

- Lane established; goal, lane config, boundary checker and review committed.
- Base commit for boundary checks: recorded in the lane's first commit message.
- H4: `tools/engine_input_key.py` (key, components, receipt, lookup) with
  `tests/repository/test_engine_input_key.py`; receipt integration handed off
  as 001. Remaining H4 DoD: desktop integration and one regular build receipt
  carrying the key.
- H3: `docs/PERFORMANCE_METHODOLOGY.md`, `tools/perf/` and
  `tests/repository/test_perf_harness.py`; trace events handed off as 002.
  Remaining H3 DoD: one harness run on the installed candidate (leases below).
  Budget verdicts are additionally blocked on the owner-gated reference
  toolchain: `ahoi-dev` is a component build and no `upstream-release`
  control exists yet.
- H6 (new, user request 25 Sep): optional fully isolated Workspaces.
  Analysis `docs/reviews/crest-hardening-2026-09-25-workspace-isolation.md`,
  decision `docs/decisions/0011-optional-isolated-workspace-profiles.md`,
  Master pointer section. Implementation belongs to desktop.
- Handoffs 001 and 002 were integrated by desktop (`2ab7909`, `d425c3b`).
- Rule clarification: `.work/chromium/src` may be read for source analysis
  (a research helper did so on 25 Sep); never written, built or run.
- H5: Crest host reference review and network-silence checklist committed.
  Handoffs 003 (Workspace deletion keeps foreign partition sessions), 004
  (GCM check-in root cause: user policy invalidations), 005 (field-trial
  testing config) and 006 (group before-unload) are ready.
- Pre-existing red test reported in 005: `test_lean_chromium.py` fails at
  `f811604` because `75e20c1` appended `enable_ahoi_ubo_classic` after the
  lean delta and changed the pinned full profiles.
- H6: acceptance catalogue WS-ISO-01..13 handed off as 007 (Master matrix
  lines + registry entries; both files hold uncommitted desktop work).
- User decision 25 Sep: level `website-sessions` keeps permissions and
  `chrome.cookies` profile-wide (documented, disclosed); per-partition scoping
  stays an open later option. Recorded in ADR 0011; text changes as 008.
- H1: drift gate `tools/sync_conformance/field_groups.py`; merge model,
  vector generator and `fixtures/sync-conformance/merge_v3.json`; runners
  handed off as 009. Swift tests need the Mobile simulator, and C++ tests need
  the desktop build, so neither runs in this lane.
- Handoff 005 was integrated by desktop (`0981913`).
- Desktop integrated 003, 004, 006, 007 and 008. Review of 003/006 as handoff
  010 (R1 high: synchronous overlap rejection in `GroupPageClose::Ask`).
- H2 audit done; fixes handed off as 011, deletion re-homing added to 010 as
  R6 (high). 009 runners integrated by sync (`095b959`), not yet run.
- 009 results: implementations agree on every valid vector; generator fixed
  (rev 2) and a Sync invariant finding handed off as 012. H3 lease confirmed
  with conditions (window opens on the next installed package candidate).
  ADR 0011 step 1 implemented by desktop (`671796d`), reviewed as 013.
- Waiting for owner lanes: 010, 011 integration; 009 run results; NET-GCM-01/02
  and fresh-profile audit; H3 lease; each ADR 0011 implementation step for review.

- Ownership clarified with the desktop/mobile/sync owner session: the whole
  lane, including H6, stays here. Handoffs 015 and 016 (commits `1e7ad0b`,
  `2633818`) were written there by mistake and are adopted by this lane. The
  owner writes only `Status:` lines under `handoffs/crest-hardening/` and
  builds only from `/private/tmp/ahoi-m153-ws.x4fmXk/repo`. The next build (21)
  starts after it has checked the ready handoffs.

- User decision 25 Sep (evening): this lane also writes the implementation of
  011 S2–S8 and the open parts of ADR 0011 step 2 (Quick Window, export/import,
  process-wide Workspace order) as complete patch handoffs with unit tests
  (even numbers from 028, order S5, S7, S6, S2, S3, S8, S4, then step 2). The
  desktop owner applies, builds, tests visibly and sets the status; the lane
  still never writes owned paths directly or builds. Coordinated by message.

## Goal status — 25 September 2026, after the 010–013 intake

Every lane-owned deliverable of H1–H6 is done and committed. Desktop and Sync
integrated 010–013 (`990c7bb`, `17f5319`, `8a9fc91`, `d2debaa`, `88b4875`);
the lane reviewed that intake (014). Each remaining DoD item waits only on
an owner integration, a lease window or an owner implementation step.

| Package | Lane work | Waits only on | Owner |
| --- | --- | --- | --- |
| H1 | **DoD met**: drift gate, merge model with union validation, 131 vectors (rev 3); C++ green on builds 24/25, Swift green on A168 (all seven entity types, 0 failures) | – | – |
| H2 | done: audit, rule; S1 and rule integrated (`8a9fc91`), R6 integrated (`17f5319`); S2–S8 all integrated and checked against the patches (028 `6f66a44`, 030 `047d739`, 032 `c64c358`, 034 `5f1ce2c`, 036 `aa1058f`, 038 `72109fc`, 040 `00381ba`; only owner-refreshed build contexts differ) | build 33 (`880217d`): all unit tests pass. 040 reproduced 5/5 on build 32; on 33 the surface is consistent, but restarts land in the first Workspace, a pre-existing loss of the saved window Workspace (save-side logging requested); 034 split archive PASS on build 33 (rerun, plus owner fix `76f6d94` for the asynchronous close completion, reviewed); 036 stress case pending; 040 plus owner patch 0064 **accepted on build 37** (restore-surface 5/5 restarts: right Workspace, front page, no empty state); split restore on build 37: partners open (`3dfeb17`), but the passive materialization defers splits with an active pane: 072 integrated `4705dd8`; focus DCHECK root cause (FocusManager's stored view restored asynchronously after ClearFocus, fix `SetStoredFocusView(nullptr)`) sent to desktop; split restore opens its partners on activation (`3dfeb17`, reviewed, no finding) | desktop |
| H3 | done: methodology, harness, trace events (`d425c3b`); harness validation run on build 24 (`artifacts/perf/f91e5b7-20260925-validation/`); Xcode 27 reference toolchain integrated (022, `6630a7f`) | **owner-gated by disk space**: 55 GB free, 64 GB per release build required (desktop checkpoint owner item "free ≥ 64 GB, better 130 GB"); then `upstream-release` and `ahoi-release` with Xcode 27 after build 33, bundle paths and a host-quiet window in the desktop checkpoint, then the budget run by this lane | desktop |
| H4 | done: tool, receipt integration (`2ab7909`) | **DoD met**: build 21 receipt (`f5e4c1f`, built 14:14 UTC) carries `engineInputKey ca277d91…`, identical to `tools/engine_input_key.py key` of the exported source tree `f5e4c1f` | desktop |
| H5 | done: review, checklist, audit tool. Audit on installed build 29 (`edced8d`, 21:30–21:43, `artifacts/network-audit/edced8d-20260925/`): **NET-GCM-01 PASS** (no GCM hosts, no GCM store); silence FAIL only because of `accounts.google.com` ×15 (ListAccounts, fixed by patch 0062 in build 31); otherwise only allowlisted component-update and Safe Browsing endpoints | **DoD met**: rerun on installed build 31 (`8b3336a`, 21:06–21:17 UTC, `artifacts/network-audit/8b3336a-20260925/`): NET-GCM-01 **PASS**, fresh-profile silence **PASS** (only allowlisted component-update and Safe Browsing hosts; ListAccounts gone); repeated on installed builds 32 (`5be0782`), 33 (`880217d`) and 39 (`79e35f3`, first with the strict-privacy proxy and GPC preference, `artifacts/network-audit/79e35f3-20260926/`): both **PASS** each time | – |
| H5+ (owner request, PRIV rows) | audit tool extended: navigation (PRIV-12) and crash (PRIV-16) phases, NetLog kept (`63aef5e`, `7e39f1e`, `34bf714`); PRIV-14 evidence on 79e35f3: the only Safe Browsing list request (`v4/threatListUpdates:fetch`) carries the placeholder key (10 chars, `dummytoken`) and gets **HTTP 400**, so Standard Safe Browsing loads no lists (build/release decision: real API key) | PRIV-12 **PASS** and PRIV-16 **PASS** on 79e35f3 (`artifacts/network-audit/79e35f3-20260926-priv12-16/`); PRIV-14: owner's keyed `safe-browsing-journey.sh` PASS on build 40 (malware and phishing interstitials, key in no artifact); this lane's keyed NetLog audit PASS for the lists (`threatListUpdates` 200, `artifacts/network-audit/build40-keyed-20260926-1115/`), but the key also enables Translate: `translate_a/l` 200 on the first navigation (N3), handoff 094 (owner decision); first keyed run hung on shutdown, tool now escalates (b9bf54e). 26 Sep: the user decided on a key; Codex job `9d261769` ended without a key; job `96349df7` delivered the key and the user's r/CrestBrowser post (`8b2fdaa`); comparison and plan in [074](../handoffs/crest-hardening/074-reddit-crest-post/HANDOFF.md) (history swipe and folder moves exist; recent-tabs flick contracted but missing; Workspace merge and mobile Web Extensions need decisions); key present since 08:57 (40 chars, `threatListUpdates:fetch` HTTP 200 by this lane; owner notified), in the login Keychain (`ahoi-google-api-key`/`safe-browsing`, never in the repo); desktop passes it only as GOOGLE_API_KEY in the launched browser's environment (not in args.gn), then this lane reruns the NetLog audit as its own keyed variant (`d9b813c`: `--google-api-key-from-keychain`, key only in the browser's environment, redacted from the kept NetLog). Build 40 runs since 08:14. The user also allowed build 40 at 62 GB (`AHOI_ALLOW_LOW_DISK=1`), relayed to desktop | desktop (API key: owner-gated) |
| H6 | done: ADR 0011, catalogue, reviews 010–026 of every integrated step; step 2 written by this lane and integrated, each checked against its patch: 044/046/048/050/054 (`5be0782`), 052 conversion (`92f7583`, syntax-checked by this lane too); reviews 042/046 of the owner's deletion fix; owner fixes `78d3366` (fallback activation posted after the observer notification, WS-DEL-01 crash) and `2c4bd49` (main window after deleting the only presented separated Workspace) reviewed without findings; 058 (archive split token expires) integrated `739cea6`; `4a11c52` (HTTP auth resets the tab's partition) agreed, and the sweep for the same defect class found the developer toolkit acting on the default partition: 060 integrated `33d7cd0`; 062 (upstream session-writer DCHECK, now patch 0065) and 064 (developer toolkit bubbles blur before destruction) integrated `6456b1a` for build 38; 066 design accepted; part 1 (068) integrated `fca2c38` plus patch 0066; parts 1 and 2 (068, 070) **accepted on build 39** (privacy journey 13/13: Sec-GPC on a cross-site subresource, navigator.globalPrivacyControl true in strict and undefined in default, referrer origin-only, default unchanged); split restore (034, 072) **accepted on build 39** (5/5); owner fix `e10d3f7` (compatibility site keeps no Sec-GPC inherited from a strict page) reviewed without finding: only Ahoi's strict preference sets the header, the reverse direction sets it through `ApplyStrictRequestRules`, redirects already removed it | visible step 2 journeys: WS-ISO 15/15 and ws-convert (WS-ISO-09, 052) 8/8 on build 33; switcher, Quick Window and command bar journeys still pending; 026 ready; step 3 mobile side already integrated (`fd6c3b2`, `9347aae`, `57b6ee1`, reviewed in 024); its last finding 026 written by this lane as patch 056, integrated `a007aa3` (identical to the patch), `SeparatedWorkspaceSyncTests` 20/0 on A168; WS-ISO-21/22 need real CloudKit (owner-gated) | desktop, mobile |
| H7 (user, 26 Sep: 074 into the main goal) | ADR 0012 (`fcc1c84`, merge rules with own website sessions `56b5a36`), lane goal H7, master pointer; handoffs: 076 mobile folder move leaves children behind (data loss); 078 desktop store `MergeWorkspace` (one transaction, undo revives the source, open tabs stay out of the folder); 080 desktop bridge and sidebar (same context: tabs follow, undo; own sessions: before-unload, retired, no undo); 082 command-bar move; 084 sync `merged_into` design; 086 mobile merge (after 076); 088 mobile recent-tab flick; 090 mobile Web Extension spike (`WebPage.Configuration.webExtensionController` exists). Every patch applies at HEAD and is syntax- or type-checked; none is built. Found on the way: 092, the build 40 dialog SEGV (`GetContentsView()` is the NonClientView), sent to desktop and integrated (`6230a31`, build 41) | all H7 handoffs written; owner (26 Sep): 076 **integrated** (`0ad015a`, AhoiMobileCoreTests 301/0 failures), a follow-up core run with 315 tests (+14 = the 086/088/090 tests) green, 078/080/082 applied for build 41, 084/086/088/090 after the H3 pair; then, builds and the visible cases `WS-MERGE-*`, `CMD-MOVE-01`, `MOB-FLICK-*`, `MOB-EXT-01` | desktop, mobile, sync |

**27 Sep, 00:47:** the Mac rebooted during the night. Build 41 (`cc14296`, with H7 078/080/082 and 092) was not installed; the installed candidate is still build 40 (`6acd207`). The owner's locks and scratch state are gone, and 151 GiB are free, so the disk gate for H3 is lifted. Build 41 restarted at 00:50 on `9bc5924` (adds 084 on the C++ side: `merged_into` in the sync model, codec, merge and adapter, tab tree schema 5); the owner's queue now lives in `.work/agent-queue/`. This lane added the H1 vectors for `merged_into` (096, `f05eb75`, 134 cases). Next: build 41 journeys, then the H3 release pair.

## Packages

| Package | State | Handoff | Integrated in |
| --- | --- | --- | --- |
| H1 Sync conformance | drift gate; merge model; runners integrated (`095b959`, Swift fix `11bf324`). Run results: C++ 118/132 and Swift 130/132 agree with the expectations; all 16 mismatches were invalid generator inputs, fixed in vector rev 2 (130 vectors) plus an input-invariant test. Sync finding: unvalidated unions can break the appearance invariant. Remaining DoD: rerun rev 2 on both sides | [009](../handoffs/crest-hardening/009-sync-merge-conformance/HANDOFF.md) integrated, [012](../handoffs/crest-hardening/012-merge-vectors-rev2-and-merged-validation/HANDOFF.md) ready | `095b959`, `11bf324` |
| H2 Single writer | audit `docs/reviews/crest-hardening-2026-09-25-single-writer-audit.md` done (rule, transition table, 5 findings; 3 checked in code); remaining DoD: fixes S1–S8 integrated with tests and the visible journeys | [011](../handoffs/crest-hardening/011-single-writer-fixes/HANDOFF.md) ready; deletion re-homing as 010 R6 | – |
| H3 Performance methodology | methodology, runner, statistics, CDP client and 16 tests done; refusal gate verified on the busy host | [002](../handoffs/crest-hardening/002-perf-trace-events/HANDOFF.md) integrated | `d425c3b` |
| H4 Engine input key | tool + tests done; lookup verified on e9f4a99, 4cb622a, c986090, 92694fe receipts | [001](../handoffs/crest-hardening/001-engine-input-key-receipt/HANDOFF.md) integrated | `2ab7909` |
| H5 Crest reference / network silence | review and checklist done; 003–006 integrated (GCM as patch 0056 `caf6f1e`, field trials `0981913`, group close `87a6b89`); remaining DoD: NET-GCM-01/02 and the fresh-profile audit on the next candidate | 004–006 integrated | `caf6f1e`, `0981913`, `87a6b89` |
| H6 Isolated Workspaces (contract) | ADR, catalogue done; 003/007/008 integrated; reviews: 010 (group close and deletion, R1 and R6 high), 013 (step 1 `671796d`: I1 high, no way to reopen a closed separated Workspace until step 2; I2 crash while creating). Remaining DoD: review of steps 2 and 3 | [010](../handoffs/crest-hardening/010-review-group-close-and-deletion/HANDOFF.md), [013](../handoffs/crest-hardening/013-review-isolated-step1/HANDOFF.md) ready | 003 `52bfd3c`, 007 `7401b8e`, 008 `b0b7f6c` |

## Lease requests

A lease is valid only after the resource owner confirms it in their own
checkpoint.

| Resource | Purpose | Requested | Confirmed by owner |
| --- | --- | --- | --- |
| `installed-app` + `host-quiet` | H3 harness validation run: `startup`, `memory`, `idle` scenarios, 5 runs, disposable profiles, on the installed candidate; about 30 minutes while no build runs and the owner is away | 2026-09-25 | confirmed with conditions (desktop checkpoint); window not open yet |
| `installed-app` | H5 fresh-profile network audit (NET-GCM-01): `tools/network_audit/fresh_profile_audit.py`, one window with a disposable profile, 10 min idle plus one local page, no UI input, ~12 min; holds `h3.lock` | 2026-09-25 | confirmed (`6d40b5b`), open from 21:10, only without e2e/build lock |
| `simulator` (or owner run) | H1 Swift rerun of merge vectors rev 3 (`SyncMergeConformanceTests`) | 2026-09-25 | done by the owner (0 failures on A168) |

## Open questions to other lanes

None.
