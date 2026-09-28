# H3 validation run ws9 — build 44 `8772fbc0` (non-budget)

28 Sep 2026 17:03:28 CEST, first run with the millisecond idle sample fix
(`ae755be`). The guard's `idleSamples` now show a constant last-input time
(≈ −345 s) for 42 s — no phantom drift — until idle dropped to 0.034 s at
49.8 s, a genuine input that matches the user returning (message at ~17:05).
Correct cancellation; the guard fix is confirmed on a real run.
