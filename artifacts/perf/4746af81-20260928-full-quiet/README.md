# H3 full run attempt 1 — installed build 45 `4746af81` (validation mode)

28 Sep 2026 19:59:50 CEST after 1022 s HID idle. Started although the 1-min
load was 13.4: the quiet check compared the German-locale "13,40" as a
string ("13,40" < "3.6"). Cancelled correctly at 32.5 s by genuine input
(idle fell to 0.022 s). Partial startup samples: first launch 6246.8 ms,
warm 4184.7 ms. The check now normalises the decimal comma
(`.work/crest-h3/run-45-full.sh`) and retries until one run completes.
