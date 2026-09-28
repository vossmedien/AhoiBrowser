# H3 validation run ws7 — installed build 44 `8772fbc0` (non-budget)

28 Sep 2026 ~16:56 CEST after 11 min HID idle. Cancelled as "owner input
detected" within seconds with `driverInputs` 0, yet the next run started
at a *higher* idle (666 s → 680 s), so no real input happened. A manual
diagnosis on the same build (`.work/crest-h3/diag`) ran the Workspace
setup and four switches end to end: the drivers work on the real UI and no
AX action resets HIDIdleTime. Cause still open; the guard now records its
last idle samples (`idleSamples`) for the next run.
