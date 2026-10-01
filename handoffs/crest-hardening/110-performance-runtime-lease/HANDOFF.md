# 110 – H3 active-run lease, owner-input cancellation and resource ownership

Status: ready (desktop owner reviewed 1 October; no product patch for header candidate; deferred to separate H3 lease; owner lease and real-candidate acceptance pending)
Owner: crest-hardening tools; desktop confirms the next measurement window
Base: `56951bd` / Crest runtime cleanup `dcb513b`.

## Contract gap closed in source

Preflight alone did not stop a long scenario if the user returned or a lease
was revoked. `runtime_guard.LeaseGuard` now checks the owner grant and current
conditions throughout the run. The runner binds exact bundle identities, claims
its own H3 lock, registers only its own newly created process sessions, and
cancels/reaps those sessions when permission or host conditions change.

Cancellable waits include fixture marks, DevTools endpoint startup, silent CDP
sockets, scenario sleeps and trace-driver waits. No late sample is added after
a cancellation. Only a completed and uncancelled guard can support a budget;
failed runs retain the explicit aborted evidence from 106. Process cleanup
failure retains the profile **and** this invocation's H3 lock for owner recovery.
The check thread exists only inside the run, never as an idle resource watcher.

## Owner's next lease (only when actually granting it)

Place exactly one current line starting `Crest-H3-Lease: ` in your own current
Desktop checkpoint, with a JSON object on that same line. The following is a
**closed template**, not permission and not a request to run during the current
resource/test gates:

```text
Crest-H3-Lease: {"id":"<unique-window-id>","status":"closed","mode":"budget","expiresAt":"<future UTC ISO-8601 time with Z or offset>","resources":["host-quiet","installed-app"],"lockDirectory":"<existing absolute coordination directory>","bundles":[{"binarySha256":"<candidate launcher SHA-256>","bundleTreeSha256":"<candidate bundle tree SHA-256>"},{"binarySha256":"<upstream launcher SHA-256>","bundleTreeSha256":"<upstream bundle tree SHA-256>"}]}
```

- Use hashes from 102's exact unchanged bundles/receipts. Candidate comes first.
  A validation-only grant uses `mode: validation` and the actual candidate
  hashes; it cannot authorize budget results. `installed-app` is required when
  a bundle resolves to the installed Ahoi app. Budget mode requires `host-quiet`.
- Only after the actual owner confirmation set `status: open`. Keep older
  markers out of the current marker namespace (one line only). The runner
  refuses missing, expired, ambiguous, wrong-mode or wrong-bundle grants.
- `--lease-checkpoint` must point to the **live owner checkpoint**, not an
  exported copy. The existing `--lease` flag still records that the installed
  app's grant was checked. Use a bundle alias/copy in the runner command so no
  harness command line names the installed bundle (original Crest goal rule).
- The coordination directory must already exist and be the one used by your
  `build.lock`/`e2e.lock` queue. The runner does not invent one or remove stale
  locks. Updating this checkpoint atomically avoids partial-read cancellation.
- To revoke: close/remove the current grant or change its ID. Wait for the
  run-owned `h3.lock` to be released before starting another resource user.
  If it is retained after a cleanup error, inspect the recorded process/profile
  state first. This protocol still requires all resource owners to cooperate.

No actual owner checkpoint/grant was written by Crest. No new runtime lease
was inferred from the old `open` rows or the provider restart.

## Evidence and limits

Tests use temporary checkpoint/lock files, fake process handles, mocked host
probes, and local protocol fixtures. One short-lived monitor-thread test proves
it stops a registered mock process without waiting for the main thread to poll;
no real process or browser receives a signal. Full mocked runner tests cover
both completed validation metadata and a revoked lease after a partial sample.
Other cases cover expiry/replacement, resources/candidate hashes, existing and
replaced locks, failed reaping, owner input, compiler/power/AX changes, malformed
probes and cancellation during silent WebSocket reads/handshake.

Final local validation on 27 September: 91 tests pass (60 performance, including
17 guard tests; 31 network/engine-key/lane tests). Python parsing, diff and lane
checks pass. No app, compiler or simulator was launched for this validation.

Poll interval is one second plus bounded probe/scheduling latency; own process
termination may need TERM/KILL escalation. No timing claim on a real candidate,
H3 budget pass, unattended lease or E2E acceptance is implied by these tests.
The next owner-granted real run must verify teardown and measurement overhead.
Heavy-host and paid API/E2E/judge restrictions remain in force.
