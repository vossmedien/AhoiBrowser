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

## Goal status — 25 September 2026, after the 010–013 intake

Every lane-owned deliverable of H1–H6 is done and committed. Desktop and Sync
integrated 010–013 (`990c7bb`, `17f5319`, `8a9fc91`, `d2debaa`, `88b4875`);
the lane reviewed that intake (014). Each remaining DoD item waits only on
an owner integration, a lease window or an owner implementation step.

| Package | Lane work | Waits only on | Owner |
| --- | --- | --- | --- |
| H1 | done: drift gate, merge model (now with union validation as decided by Sync), 131 vectors (rev 3); runners integrated; first run agrees on every valid vector | integration of 014 (copy rev 3) and reruns on the next build and simulator | desktop/sync, mobile |
| H2 | done: audit, rule; S1 and rule integrated (`8a9fc91`), R6 integrated (`17f5319`) | owner's deferred S2–S8 (planned for the next desktop package) and the visible journeys | desktop |
| H3 | done: methodology, harness, trace events (`d425c3b`) | lease window "open" on the next installed candidate; budget verdicts also wait for the owner-gated reference toolchain | desktop, owner |
| H4 | done: tool, receipt integration (`2ab7909`) | the next regular build receipt carrying `engineInputKey` | desktop |
| H5 | done: review, checklist; GCM, field trials, group close integrated | NET-GCM-01/02 and the fresh-profile audit on the next installed candidate | desktop |
| H6 | done: ADR 0011, catalogue, reviews 010, 013, 014 and journey coverage 015 | visible WS-ISO/WS-DEL acceptance on the next candidate; ADR 0011 steps 2 and 3, then their review | desktop, sync, mobile |

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

## Open questions to other lanes

None.
