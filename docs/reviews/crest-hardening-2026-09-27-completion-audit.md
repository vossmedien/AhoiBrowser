# Crest H1–H7 completion audit — 27 September 2026

Baseline: `73360d7`, plus the source-only catalogue work in handoff 116.
Normative scope: `outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md`,
the restored native goal, ADR 0011 and ADR 0012. This audit does not redefine
completion around already passing tests. The latest native goal requires the
full DoD; handing something off is not equivalent to integrated acceptance.

Previous turn classification: **progress** (retention source review and ready
follow-up 114). This turn inspected contract-to-code coverage and adds the
missing H1.4 source artifact. It is not a verified process wait. Root 1159's
no-further-runs instruction remains in force; no new test/build/runtime is
started for this audit.

## H1 — partially implemented and partially verified

| Requirement | Current evidence | Finding / remaining proof |
| --- | --- | --- |
| H1.1 complete active entity/domain coverage | Original 140, then integrated 128/130 supplements | All 15 IDs now have source-level pair vectors and registered runners. Native results for the 6+36 new cases and fuller Home URL/routing/shortcut value coverage remain open. Source integration is not behavioral acceptance. |
| Operation sequences, delayed batches, canonical final state | Existing pairs, 112 apply frames, draft 132 seeded 18-case/104-step corpus | 132 proposes runners that feed each actual prior merge result into the next step. Host-load hold deferred Swift parsing; native replay/direct final-output comparison and store sequence acceptance remain open. |
| Equal clocks, tombstones, quarantine, successor, atomic groups | Existing 140 vectors; 104 target conflicts; repository hand-derived assertions | Implemented for covered entities. The same-clock merge-target omission is fixed in Swift `4f2b154` and checked at `a747c62`. Preserve that accepted result. |
| New domains when field maps exist | Registry has split topology/ratios, archive snapshot/state and tree home_target; permitted-setting values are generic | Split has no shared vector; tree vectors chiefly modify title/location, not home-target mutations. Routing/shortcuts need their published value semantics covered, not merely a boolean permitted-setting sample. |
| H1.2 two real runners, per-case diagnostics | C++ `sync_merge_conformance_unittest.cc`; Swift `SyncMergeConformanceTests.swift`, integrated 128/130 | Source runner coverage now includes all 15 IDs. Swift's older 140-vector pass is exact (`443c452`, clean `a747c62`, fixture `da09b4…`); clean `6096e93` later passed 140+8 before 128/130. New 6+36 and C++ actual execution remain pending. Registry-wide coverage is source-visible, not yet native-proven. |
| H1.3 seeded differential operations and canonical output comparison | 122 native exporters integrated; lane comparator/receipt tool; draft 132 seeded sequence runner | Tooling can compare actual bound final outputs and preserve failing seeds, but neither C++/Swift export pair has run. Draft 132 adds actual-state sequential replay; native step results and comparator PASS are pending. Fixture-oracle tests alone are a different scope. |
| H1.4 generated maps/type tables | New 116 generator and `fixtures/sync-conformance/generated/` | Missing artifact now authored; new tests and native syntax/consumer verification are not run during the stop window. Product codec replacement remains optional and owner-gated. |
| Drift gate in repository regression | `field_groups.py`, `test_sync_conformance_field_groups.py`, unittest discovery in `scripts/test-repository.sh` | Existing 37-copy comparisons passed previously. New catalogue freshness tests join this entry point; full script also builds/tests owner code, so do not run it from this lane under current boundaries. |
| Apply/re-home extension | 112 shared raw fixture, 108 review, owner R2/retention source `61cab96`/`5a7d6b3`, 114 follow-up | Adapter runners, root order, Mobile retention and exact native acceptance pending. 112 is not a transport/CloudKit pass. |

Declared entity classes without shared merge cases at the original audit baseline: **0 device,
3 historyVisit, 4 deviceTab, 9 extensionInventory, 10 developerAsset,
11 bookmark, 12 deviceCapability, 13 splitGroup**. The source registry declares
all with Format 3 field maps. Existing field-map parity is not behavior coverage.

Follow-up: handoff 128 now supplies six supplementary vectors and a proposed
native patch for IDs **9 and 10**. It exposes the current Mobile whole-record
selection diverging from C++ field union. Owner integrated the source as
`8e96de7`/`81a26f3`; these two classes are **not** natively passed until
RED/GREEN, C++ execution and actual-output comparison. At that time the other
six classes still lacked vectors.

Further follow-up: handoff 130 supplies 36 separate valid pair vectors and
runner registration for the remaining IDs **0, 3, 4, 11, 12, 13** on top of 128.
The stack applied in a disposable owner-source copy; 74 local conformance
tests passed. Owner integrated both supplements as source in `8e96de7` and
`81a26f3`, so all 15 classes now have registered shared pair coverage. The
new cases remain **natively unverified** and cannot yet close H1.1/2/3 or the
operation-sequence and domain-value gaps.

Sequence follow-up: ready 132 adds a pinned seeded generator and proposed
actual-state C++/Swift replay runners (18 cases, 104 operations). Local model
tests and a later single Swift syntax parse pass. Host-load/resource gates still
defer native execution.
The proposed source does not become H1.3 evidence until both native runs and
122's exact-output comparison have been captured; store/CloudKit sequences
remain a separate requirement.

## H2 — corrections integrated; full acceptance not established

| Requirement | Current evidence | Remaining scope |
| --- | --- | --- |
| H2.1 write-transition audit | `crest-hardening-2026-09-25-single-writer-audit.md` | Audit covers the named families; some rows aggregate transitions and lack their own file/line reference. Complete the transition-to-source evidence map before claiming exhaustive coverage. |
| H2.2 one authority, async guard, operation identity | 011 rule intake `8a9fc91`, S1; 010/R6 and 028–040 source fixes | Confirm each repeated/late completion against its integrated operation guard; source handoffs are present, not a blanket runtime pass. |
| H2.3 per-finding RED and minimal fix | Individual handoffs/unit source, recorded restore and archive failures/fixes | Bind existing RED/GREEN evidence per finding. A final aggregate green unit log is not proof of the required negative control for every path. Do not rerun unchanged accepted tests merely for this audit. |
| Exact built/installed visible journeys | Restore accepted on build 37; split archive/restore accepted on 39; checkpoint references | Stress case 036 and remaining promotion/selection/Quick Window paths need their exact-candidate evidence mapped/completed. Empty-workspace fix stays with Desktop. |

## H3 — tools hardened; measurement semantics and full run remain open

| Requirement | Current evidence | Remaining scope |
| --- | --- | --- |
| Matched build/toolchain methodology | `PERFORMANCE_METHODOLOGY.md`, 022; 102 integrated `1c60f31` | Await actual release receipts/unchanged bundles with effective PGO/ThinLTO. Native goal explicitly selects `ahoi-release` versus `upstream-release`, both Xcode 27. No performance claim without that pair. |
| Host/AX/resource control | 110 (`08a7a8c`), local guard/cleanup tests | Source implemented; exact-candidate monitor/teardown/overhead still unmeasured. A current owner checkpoint lease is required. |
| Cold start to first paint | `START_PAGE` and `scenario_startup` in `run_desktop_perf.py`; scenario version 2 | Source now records the actual `first-paint` entry and refuses missing/old load-end samples. Real-candidate paint availability and pinned foreground/window state remain unverified. The entry measures local page paint, not the native Ahoi window's first frame. |
| Workspace switch / command bar | Trace collector and 002 events | Existing trace durations cover commit/ranking rather than presented frame. Supply the driver/candidate-bound run and retain this scope distinction for each budget. |
| Memory with Memory Saver | `scenario_memory`, RSS at 1/20 local tabs; pinned-source review of Local State/policy | The 20-second probe does not enable/read back Memory Saver. Pinned default is off; shortest natural discard timer is 2 h. PERF-06 remains matched RSS only. A separate verified-mode 100-tab discard/restore observation and longer owner lease are required for PERF-11; wakeups/GPU remain separate. |
| Speedometer / repetition / statistics | CDP scenario, warm/first launch, five-sample and spread/bootstrap evaluators | Selector still documented as unvalidated on a real candidate. Full matched measurement and raw evidence pending. |
| Bundle/CPU/RAM/network budget mapping | `perf_stats.py` covers PERF-01/02/03/04/06/07; H5 handles network endpoints separately | Aggregate `pass` now requires every listed budget, preventing focused partial runs from passing as a whole. Map all applicable existing budgets to evidence; this subset still does not prove bundle/network or omitted PERF rows. Keep unrelated Master metrics with their owners. |
| H3 DoD full run | Only build-24 validation artifact in `artifacts/perf/` | Validation/busy-host data is not a release-budget pass. Missing releases, current lease and the above semantics block completion. |

## H4 — preserve accepted scope; no new build

The deterministic key tool/tests, read-only lookup and receipt integration 001
are delivered. 001 records successful receipt lookups for `e9f4a99`, `4cb622a`,
`c986090` and `92694fe`; the checkpoint records the regular build-21 receipt
`f5e4c1f` carrying the matching key. No extra build or unchanged rerun is needed
for the provider restart. New H3 optimization evidence is a separate requirement
and does not erase this accepted H4 scope.

## H5 — base audit accepted; keyed policy variant remains open

Reference review `crest-hardening-2026-09-25-crest-chromium-reference.md`, the
network checklist, GCM handoff 004/integration and candidate audits exist.
`artifacts/network-audit/79e35f3-20260926/README.md` + `audit.json` record
NET-GCM-01 and fresh-profile silence PASS on that exact keyless candidate.
Earlier 31–33 results are retained. Do not restart completed base audits merely
because the provider changed.

The distinct keyed build-40 variant in
`artifacts/network-audit/build40-keyed-20260926-1115/` records Safe Browsing list
HTTP 200 and NET-GCM-01 PASS but **Translate/fresh-profile silence FAIL**.
094's product decision remains owner-gated; do not silently allow Translate or
turn it off without that decision. No current H5 runtime lease exists. Safe
Browsing, Widevine supply and updater remain preserved contract boundaries.

## H6 — contract/reviews delivered; retain owner acceptance limits

ADR 0011, isolation analysis, WS-ISO catalogue 007, deletion finding 003/010,
and stage reviews 013–026 plus later fixes are delivered. The contract-level
DoD is distinct from product acceptance, which remains with Desktop/Mobile.
Recorded WS-ISO/convert results and separated-workspace Swift tests apply only
to their exact candidates. Shared switcher/Quick Window/command bar visible
checks and real CloudKit WS-ISO-21/22 remain owner evidence gates. Do not count
a source review or a simulator-only result as a cross-device/privacy pass.

## H7 — source and acceptance still incomplete

| Requirement | Current evidence | Remaining scope |
| --- | --- | --- |
| H7.1 ADR and Master pointer | ADR 0012; current Master pointer | Delivered; preserve later user decisions and ownership. |
| H7.2 Desktop transactional merge/undo | 078/080 in source, store/session tests; source finding 134 | Desktop store explicitly skips undo for an empty source (`record_undo && !roots.empty()`). The existing UndoSnapshot format cannot represent Workspace-only revival; guard removal alone is insufficient. ADR 0012 promises undo and waives only confirmation. Route/settings restoration through undo also needs concrete evidence. |
| Desktop entry points and move command | Sidebar merge UI and 082 move-to-Workspace command present | ADR lists merge in context menu and command bar; no explicit merge command was found in the inspected command-bar source. Verify/complete that entry point. Installed WS-MERGE-01–05 and CMD-MOVE-01 evidence pending. |
| H7.3 sync merge target and concurrent additions | 084/096/104, exact Swift result, 112 apply fixture, retention 108/114 | Root order, full apply/compaction parity, native C++ and real sync evidence remain open. |
| Mobile merge | 086 + 098; clean `a747c62` 27/27 suite and focused RED/GREEN | Accepted local scope stays valid; visible WS-MERGE-07/cross-device result not yet proved. |
| Mobile flick | 088 integrated, MRU/control-bar source and core tests | ADR's neighbor-page preview during dragging is explicitly not implemented. Gesture/VoiceOver visible cases remain open. |
| Web-Extension spike | 090 integrated, bundled runtime loads MV3; 136 Files source handoff ready | 136 proposes a DEBUG-only Files import of an exact copy of the bundled fixture. Native picker/load and full content-script/DNR/action/storage/permission/private/update effects are unverified. Load/attach tests cannot prove those effects, and guideline 2.5.2 remains a Step-2 decision gate. |
| H7.5 integrated-stage review and exact acceptance | Existing handoffs/reviews plus current audit | Source-ready/integrated status is not installed acceptance. Keep all named cases and remaining product gaps, not only the ones already green. |

R1 follow-up: Desktop source `20467e5` addresses 124's UTF-8 allocator finding
with bounded candidates and controller/SQLite regression source; C++ execution,
actual capture/replay and installed acceptance remain pending. Handoff 126's
opaque-marker collision is still an independent unfixed source finding.

## Next work, ownership and stopping condition

1. Finish H1 coverage/protocol work in lane paths; foreign runner changes only
   as ready handoffs. Validate 116 after the no-further-runs window closes.
2. Correct H3 first-paint/scenario gaps and finish H7 missing source features
   through handoffs. Bundle owner intakes; never trigger extra builds merely
   to turn an audit row green.
3. Review new owner implementations and bind required native/runtime results
   to exact source, fixture and binary hashes when a current lease is open.
4. Keep 094, real peers, signing/publication and the extension step-2 decision
   explicitly gated. These do not authorize substituting a smaller goal.

The overall goal is **not achieved and not yet at an impasse**: meaningful
independent source work remains. No active-process wait or new runtime lease
was inferred during this audit. Historical checkpoint prose saying all lane
work/H1 was done is superseded by this requirement-level scope distinction;
its actual scoped green evidence remains valid.

## Scoped update after the bounded owner slot returned

Owner `9671d0f` records five actual C++ syntax/type checks and a failing negative
control, without link or native test execution. Do not promote that to a C++
conformance/runtime pass. Light source work is resumed; no blanket build or
H3/H5 runtime lease is granted.

116's five local Python catalogue tests and CLI freshness check now pass; native
consumer compilation remains pending. 118 adds operational R1 acceptance cases
for real key writers and replay/reload, without changing 112's frozen JSON or
claiming those product behaviors are already proven. All other gaps above stay
open until their own evidence is available.
