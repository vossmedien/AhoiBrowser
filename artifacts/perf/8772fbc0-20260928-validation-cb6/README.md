# H3 validation run cb6 — build 44 `8772fbc0` (non-budget)

28 Sep 2026 ~17:00 CEST. Cancelled as "owner input detected" without input.
The recorded `idleSamples` show the cause: HID idle rose 166 → 174 s while
~10 s passed between guard samples, because the guard timestamped each
whole-second idle value only after the full host probe (ioreg, then ps and
pmset, slow at load ~100), shifting the apparent last input by more than the
2 s tolerance. Fixed: idle in milliseconds with its own sample clocks
(`hid_idle_sample`), guard compares against that sample time.
