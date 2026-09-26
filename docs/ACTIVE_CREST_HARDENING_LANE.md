# Active Crest-hardening lane checkpoint

Goal: [Crest-Konvergenz-Härtung](../outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md).
Lane: `crest-hardening`. Boundaries: [`config/agent-lanes.json`](../config/agent-lanes.json).
Boundary check for the orchestrator:
`python3 tools/check_lane_boundaries.py --all --since <base> --worktree`.

This lane never builds, installs, refreshes the overlay, writes under
`overlay/`, `patches/`, `apps/` or `scripts/`, or stops foreign processes.

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
| H2 | done: audit, rule; S1 and rule integrated (`8a9fc91`), R6 integrated (`17f5319`); S2–S8 all integrated and checked against the patches (028 `6f66a44`, 030 `047d739`, 032 `c64c358`, 034 `5f1ce2c`, 036 `aa1058f`, 038 `72109fc`, 040 `00381ba`; only owner-refreshed build contexts differ) | build 33 (`880217d`): all unit tests pass. 040 reproduced 5/5 on build 32; on 33 the surface is consistent, but restarts land in the first Workspace, a pre-existing loss of the saved window Workspace (save-side logging requested); 034 split archive PASS on build 33 (rerun, plus owner fix `76f6d94` for the asynchronous close completion, reviewed); 036 stress case pending; 040 root cause found with the save-side logging (build 35): the reused first window never got its window extra data; owner patch 0064 (`6755551`) reviewed and agreed, acceptance on build 36; split restore opens its partners on activation (`3dfeb17`, reviewed, no finding) | desktop |
| H3 | done: methodology, harness, trace events (`d425c3b`); harness validation run on build 24 (`artifacts/perf/f91e5b7-20260925-validation/`); Xcode 27 reference toolchain integrated (022, `6630a7f`) | **owner-gated by disk space**: 55 GB free, 64 GB per release build required (desktop checkpoint owner item "free ≥ 64 GB, better 130 GB"); then `upstream-release` and `ahoi-release` with Xcode 27 after build 33, bundle paths and a host-quiet window in the desktop checkpoint, then the budget run by this lane | desktop |
| H4 | done: tool, receipt integration (`2ab7909`) | **DoD met**: build 21 receipt (`f5e4c1f`, built 14:14 UTC) carries `engineInputKey ca277d91…`, identical to `tools/engine_input_key.py key` of the exported source tree `f5e4c1f` | desktop |
| H5 | done: review, checklist, audit tool. Audit on installed build 29 (`edced8d`, 21:30–21:43, `artifacts/network-audit/edced8d-20260925/`): **NET-GCM-01 PASS** (no GCM hosts, no GCM store); silence FAIL only because of `accounts.google.com` ×15 (ListAccounts, fixed by patch 0062 in build 31); otherwise only allowlisted component-update and Safe Browsing endpoints | **DoD met**: rerun on installed build 31 (`8b3336a`, 21:06–21:17 UTC, `artifacts/network-audit/8b3336a-20260925/`): NET-GCM-01 **PASS**, fresh-profile silence **PASS** (only allowlisted component-update and Safe Browsing hosts; ListAccounts gone); repeated on installed builds 32 (`5be0782`) and 33 (`880217d`, `artifacts/network-audit/880217d-20260926/`): both **PASS** each time | – |
| H6 | done: ADR 0011, catalogue, reviews 010–026 of every integrated step; step 2 written by this lane and integrated, each checked against its patch: 044/046/048/050/054 (`5be0782`), 052 conversion (`92f7583`, syntax-checked by this lane too); reviews 042/046 of the owner's deletion fix; owner fixes `78d3366` (fallback activation posted after the observer notification, WS-DEL-01 crash) and `2c4bd49` (main window after deleting the only presented separated Workspace) reviewed without findings; 058 (archive split token expires) integrated `739cea6`; `4a11c52` (HTTP auth resets the tab's partition) agreed, and the sweep for the same defect class found the developer toolkit acting on the default partition: 060 integrated `33d7cd0`; 062 (upstream session-writer DCHECK on a failed file creation, analysis and fix) and 064 (developer toolkit bubbles blur before destruction, sweep after `cc26c51`) ready; 066 design for strict privacy on subresources (factory proxy plus GPC renderer preference) ready | visible step 2 journeys: WS-ISO 15/15 and ws-convert (WS-ISO-09, 052) 8/8 on build 33; switcher, Quick Window and command bar journeys still pending; 026 ready; step 3 mobile side already integrated (`fd6c3b2`, `9347aae`, `57b6ee1`, reviewed in 024); its last finding 026 written by this lane as patch 056, integrated `a007aa3` (identical to the patch), `SeparatedWorkspaceSyncTests` 20/0 on A168; WS-ISO-21/22 need real CloudKit (owner-gated) | desktop, mobile |

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
