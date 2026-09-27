# 100 – Build 41 stopped at overlay validation; owner resumption evidence

Status: ready (desktop-owner recovery and evidence reconciliation)
Owner lane: desktop; Mobile/Sync for 096 intake and candidate binding
Observed repository HEAD: `e9d25be`, 27 September 2026.

## Verified state, superseding the old running-build report

Installed bundle metadata still identifies build 40 source
`6acd207f19b5ba0357240b1b90f078beb4eec09c`, development profile, Chromium
`153.0.8010.53`. No active Ahoi, compiler, Ninja or xcodebuild process was found
in the read-only process inventory during this handover.

`.work/agent-queue/41/progress` ends:

```text
00:50:12 start free=152GiB
00:50:13 commit 9bc5924
00:50:47 overlay 1
```

`41/overlay.log` contains:

```text
error: overlay refresh failed: Chromium checkout does not match the previously recorded applied overlay tree or the freshly composed overlay tree; refusing to refresh foreign or partial edits
```

The queue exits on that overlay result. It has not produced a build-41
candidate, installation or runtime acceptance. `evidence.json` pins the three
reviewed queue files by SHA-256. This lane neither inspected nor changed the
protected Chromium tree to diagnose away the safety refusal.

## Owner's next bounded actions

1. Reconcile the recorded overlay state with the actual source and the intended
   candidate, preserving all foreign/partial changes, under the build owner's
   current ownership and `docs/BUILDING.md`. Do not bypass the guard, reset the
   tree or simply restart the stale command. The former `/private/tmp/` worktree
   is now listed as prunable; it is not a valid current ownership handoff.
2. Include 096's refreshed C++ vector copy and freeze the Swift 084 work before
   reporting exact-candidate conformance. `core-084.xcresult` already exists:
   read-only `xcresulttool` reports 315 tests, 313 passed, 2 skipped, no failures;
   merge, recent-tab cycler, extension runtime and sync-conformance suites pass.
   The run finished at `2026-09-26T22:52:27.332Z`, just before 096's commit at
   `22:52:28Z`. The runner reads the live fixture path; this result alone cannot
   establish which vector bytes it consumed, nor freeze the uncommitted Swift
   source. Do not infer 134/134 from its suite name or timing.
3. Before installation, ensure every unit-test process succeeded. The reviewed
   `.work/agent-queue/chain.sh` loop logs each test's exit code but does not abort
   on failure (`set -u`, no failure accumulator/guard). As written, it proceeds
   to installation after a failing unit binary. Add an explicit nonzero exit
   guard and return the build lock on failure before resuming this queue.
4. The current user pauses E2E/API/judge tests pending renewed authorization.
   The old queue's journey list and its `AHOI_E2E_MIN_IDLE=0` override do not
   authorize those runs. Keep the visible journey phase off until cleared.
5. H3 still needs sequential regular `upstream-release` and `ahoi-release`
   builds under Xcode 27, exact bundle paths and receipts, and a freshly recorded
   `host-quiet` / candidate lease. The old 25 September `open` row is not proof
   that the missing pair or a current quiet window exists.

## Remaining lane handoffs and decisions

- 098: mobile merge undo can orphan a child added after the merge; minimal guard
  and regression tests ready for Mobile. It is independent of 084's WIP.
- 094: keyed Translate still needs the user's product decision; no choice was
  inferred during the provider migration.
- H2/H6/H7 remaining visible acceptance and real CloudKit peers remain open;
  source checks and previous candidate results do not satisfy those gates.
- Mobile 086/088/090 are integrated (`e9ba41a`), as their status lines and the
  Mobile checkpoint confirm. Handoff 088 explicitly leaves the neighbor-page
  drag preview from ADR 0012 unimplemented.

No build, install, simulator, browser or external API was started by this lane.
