# H3 full run — installed build 45 `4746af81` (Zielprompt DoD evidence)

28 Sep 2026 21:58–22:19 CEST, user-authorized lease bound to the exact bundle
(`.work/crest-h3/lease-45.md`), `h3.lock` held, no build/E2E lock, no user
input for the whole run (started after 7072 s idle, 1-min load 3.5 < 3.6).
Guard completed, 1159 checks, never cancelled; AC power, window 1440×900, no
accessibility client, scenario version 2 (first paint). Scenarios startup,
memory (1/20 local tabs), idle, Speedometer 3.1 — 5 runs each.

| Metric | Median (min–max) |
| --- | --- |
| startup_first_launch_ms (fresh profile, first paint) | 3995 (3965–4251) |
| startup_warm_ms | 3828 (3822–3885) |
| memory_1_tabs_kib | 1 551 872 |
| memory_20_tabs_kib | 6 183 024 |
| processes 1 / 20 tabs | 7 / 26 |
| idle_cpu_percent | 1.0 (0.6–1.8) |
| speedometer_score | 6.9 (6.8–7.0) |

Verdicts are INSUFFICIENT by design: this is the installed `dev` (component,
DCHECK) candidate in validation mode without an `upstream-release` baseline.
The Zielprompt DoD asks for one complete run on the exact installed candidate
and forbids "faster/lighter" claims without a same-configuration comparison;
no such claim is made. Absolute numbers (e.g. Speedometer 6.9) reflect the
dev configuration, not release performance. Trace budgets (PERF-03/04) were
validated separately in the cb12–cb14 / ws16–ws17 runs and await build 46
(command-bar paint fix 977853f9, workspace-switch trace 7bdc1f69).
