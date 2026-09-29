# H3 Memory Saver evidence boundary — source review, 27 September 2026

This is a read-only review of the pinned Chromium 153 checkout. No browser,
performance run, compiler, simulator or owner lock was started.

## Exact source behavior

- `components/performance_manager/public/user_tuning/prefs.h` defines Local
  State `performance_tuning.high_efficiency_mode.state`: disabled `0`, enabled
  `2`. It also defines the aggressiveness values conservative `0`, medium `1`,
  aggressive `2`.
- `components/performance_manager/user_tuning/prefs.cc` registers Memory Saver
  state as **disabled** and aggressiveness as **medium** by default. The mode
  is a Local State preference, not a per-profile `Default/Preferences` key.
- `chrome/browser/performance_manager/user_tuning/user_performance_tuning_manager.cc`
  reports active mode only when the state is not disabled and forwards enabled
  state to `MemorySaverModePolicy`.
- `chrome/browser/performance_manager/policies/memory_saver_mode_policy.cc`
  uses background durations of **6 h conservative, 4 h medium, 2 h
  aggressive** before its proactive discard attempt. Eligibility and revisit
  exceptions still apply; the timer alone does not guarantee a discard.

The current `tools/perf/run_desktop_perf.py` memory scenario opens 1/20 local
tabs, settles for 20 seconds and records process-tree RSS. It neither enables
nor reads back Memory Saver, and it observes no actual discard. Its PERF-06
comparison remains a useful matched 20-tab RSS comparison, but it cannot
substantiate the H3 phrase “memory at N tabs with Memory Saver” or Master
PERF-11. An otherwise quiet 30-minute window also cannot demonstrate natural
discard under any of the pinned default timers.

## Evidence required before closing the two scopes

1. Keep PERF-06 as a same-pin, same-profile, same-20-tab workload with the
   existing build and host guards. Record the candidate and baseline Memory
   Saver state; require them to match. If the intended H3 run says “with Memory
   Saver,” explicitly enable and verify state `2` for both real processes,
   recording the exact Local State bytes/setting and mode in the run receipt.
   A guessed command-line switch or a file edit without runtime readback is
   not proof of the effective policy.
2. PERF-11 is a **separate** 100-tab policy/restore observation: confirm a
   real eligible background tab was discarded, RSS changed under the stated
   workload, and reopening preserves URL/history/session behavior within
   Chromium's guarantees. Record foreground tab, exemptions, mode and actual
   discard state before/after. A manually forced `chrome://discards` action
   does not prove the automatic timer; it may serve only as a labeled
   diagnostic. A deterministic native policy test can prove timing/exception
   logic, but cannot substitute for the real candidate resource observation.
3. The required natural-discard wait needs an owner-approved, longer current
   `host-quiet` lease and resource coordination. The old 30-minute handoff
   and the present Cockpit jobs2 load are not such a lease. No result from the
   20-second probe is promoted to PERF-11.

The next source task is a bounded harness plan for verified Local State
configuration/readback and actual discard observation. This review does not
choose an unverified CDP/DOM selector, change the product preference default,
or request an immediate long run.
