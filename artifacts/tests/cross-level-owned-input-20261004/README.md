# Cross-level move driver preparation, 4 October 2026

Installed candidate e86c929f/M154 remains unchanged. This packet changes only the
existing WS-ISO-05 journey: reuse PID/profile-scoped focus-yield handling; capture
and recheck the installed source at every launch; require native confirmation
focus and enabled buttons before real clicks; verify native quit status and fail
the command when the result is false. All 25 existing behavioral assertions stay.
No new browser build or test matrix.

Shell syntax and embedded Python parsing pass. Actual target prelaunch refusal
for a deliberately mismatched candidate exits8 and records pass:false. This is
only a guard proof; the profile-move journey has NOT RUN on e86. Earlier M154
acceptance is history, not this changed dialog lifetime's runtime proof.

Target at 20:30 CEST: 0.38/0.55% CPU idle, Load67.28,47GB used/11GB compressed.
Foreign BetterIPTV native-reset-v3 test PID77215 owns its simulator phase. Do not
start Ahoi GUI/heavy work against that load or interrupt its processes. Current
Ahoi app/build/signing slot is released. Next: fresh target capacity and UI-owner
handoff, run this frozen driver on installed e86, inspect Cancel/Confirm/Profile
move/undo/restart, then fix actual failures. Existing original Master stays active.
