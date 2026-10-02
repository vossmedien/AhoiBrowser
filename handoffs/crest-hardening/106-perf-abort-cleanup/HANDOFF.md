# 106 – H3 exception cleanup and incomplete-run evidence

Status: ready (desktop owner reviewed 2 October for tab-cache/native-factory packet; no product patch applies; deferred to separate H3 lease; owner runtime acceptance pending)
Owner lane: crest-hardening (tools); desktop (future exact-candidate lease)
Base: `f141273`.

## Finding

`Browser.__init__` started the browser before waiting for DevTools. If that
wait failed, no object reached the caller and no code stopped the process.
All five measurement scenarios also called `quit()` only on their successful
path. A fixture/CDP/driver failure skipped it, while main's `finally` removed
the profile directory anyway. A timed-out trace driver killed its shell via
`subprocess.run`, without owning its foreground child process group.

## Lane-owned correction

- Browser construction stops its own session on failed startup, including an
  interrupt. Browser context scopes call cleanup on errors in every scenario.
- CDP sessions close in `finally` via a context scope. Browser shutdown requests
  normal close, waits for exit, then escalates its own session through TERM/KILL
  with five-second waits. Already exited children are reaped without signalling
  their old PID. No foreign process lookup or termination is used.
- Trace drivers start a separate owned session. Timeout/failure cleanup reaches
  their shell and foreground descendants through that owned group.
- An unreaped/unsignallable process raises `CleanupError`: main retains the
  temporary profile directory and records its location for owner cleanup.
- Any scenario/fixture failure or keyboard interrupt produces only
  `aborted-run.json` (`pass: false`, partial samples explicitly segregated),
  no evaluation or ordinary completed sample files. Arbitrary error/driver text
  is not copied into this evidence.
- Output directories must be new, preventing old PASS files from surviving
  beside a failed rerun and being mistaken for current evidence.

These are changes under `tools/perf/`, already in the lane's tracked source;
there is no product-code patch to apply. The method/checkpoint are updated.

## Local tests and limits

Nine focused regression methods use mocked process handles/signals/CDP:
startup failure, all five scenario failure paths, TERM/KILL escalation,
already-exited handles, failed reap/permission/interruption, trace timeout,
partial evidence, retained profile on cleanup failure, and existing-output
refusal. No real subprocess is spawned by these new tests and no signal is sent.
Final impacted validation: 56 local performance/network tests pass; lane and
diff checks pass. This required no new heavy build or runtime test.
The process group is owned through `Popen(start_new_session=True)`; arbitrary
background daemonization by a trace driver is outside its supported contract.

This does **not** grant a runtime lease or claim a real-browser cleanup pass.
The next authorized H3 candidate run must verify clean teardown. Continuous
owner-input/lease revocation monitoring remains a separate open harness item;
this step fixes exception cleanup, not that missing monitor. Heavy-host and
E2E/API/judge restrictions remain in force.

Follow-up: [110](../110-performance-runtime-lease/HANDOFF.md) now implements and
locally tests continuous lease/input cancellation. The original 106 runtime
acceptance remains open; that follow-up is not an owner resource grant.
