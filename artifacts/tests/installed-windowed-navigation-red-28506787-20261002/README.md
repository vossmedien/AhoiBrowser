# Visible-window navigation protocol RED, 2 October 2026

Exact installed source28506787, isolated temporary profile, windowed (not
headless), idle247/no other app or resource owner. Owned HTTP server independently
returns readiness200/PaneA. All four startup document samples remain about:blank.
Explicit Page.navigate cancels the initial loader (ERR_ABORTED), emits
Network.requestWillBeSent for PaneB, but never returns within its bounded command
deadline. No responseReceived or browser HTTP request reaches the ready fixture.
The server records only the independent readiness GET. This distinguishes the
finding from a Command Bar-only problem; it does not yet identify the loader/
network-service cause.

Driver exits4, no visible/native product acceptance; shutdown also exceeds the
diagnostic deadline. The runner terminates only its own process groups, retains
the isolated profile and records cleanup complete. Process inventory afterward
has no owned browser/driver/helper remaining. Actual protocol/source/fixture/
browser/state files are hash-bound in receipt.json. The declared phase complete
means the diagnostic terminated, not a passing result.

Next: bounded owned network-service/loader instrumentation in visible mode,
compare actual process state and blocked native call path with the earlier
headless observation. Keep all native/install GREEN receipts separately; do not
weaken or repeat the split assertions while their initial documents cannot load.
