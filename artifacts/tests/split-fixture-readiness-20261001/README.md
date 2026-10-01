# Split fixture readiness, 1 October 2026

The real shared `split_journey_lib.sh` is sourced once and serves its generated
PaneA file through its owned loopback HTTP server. The new readiness gate
returns true with HTTP success and exact `<title>PaneA</title>` bytes. The shell
exits 0 and its EXIT trap stops only its own server. See `fixture-only.log`,
`site-readiness.html`, `site.log` and `receipt.json`.

This invocation overrides the input-idle threshold solely to load library
definitions and exercise the fixture server; it calls no app launch, AX/HID or
CDP function. It is not installed navigation, split or product acceptance.
The new Python/Node diagnostic and shared bash library also pass syntax checks.
Its separate installed invocation correctly defers at idle 0 before any launch:
`artifacts/computer-use/m154/navigation-probe-9acb43c8-20261001T192815Z/state.json`.
