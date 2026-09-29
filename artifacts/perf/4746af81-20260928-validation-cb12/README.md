# H3 command-bar trace run cb12 — installed build 45 `4746af81` (validation, non-budget)

28 Sep 2026 19:25 CEST, user-authorized validation lease, guard completed
(21 checks, one logged driver key attributed). 20 keystroke samples:
`command_bar_ms` (Ahoi.CommandBar.RebuildSuggestions) median 1.9 ms;
`command_bar_presented_ms` (to the first presented browser-process frame
begun after the rebuild) median ≈ 419 ms. Dev build with DCHECKs on a loaded
host: not a PERF-03 verdict. Observation for Desktop: rebuilds are fast but
the next presented frame usually arrives ~420 ms later — worth checking for a
debounce/animation/invalidation delay between rebuild and paint.
