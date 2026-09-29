# H3 validation run cb — installed `6acd207` (non-budget)

28 Sep 2026 12:59:40 CEST. The command-bar driver's AXPress on the
"Adresse öffnen…" menu item did not open the Ahoi command bar ("command bar
did not open", driver exit 4 → `CalledProcessError`). Source confirms the
design: patch 0001 routes only keyboard shortcuts (⌘L/⌘T) to the Ahoi bar;
menu items keep Chromium semantics. The driver now posts ⌘T to the browser
process only (`axtool key`, CGEventPostToPid) with retries.
