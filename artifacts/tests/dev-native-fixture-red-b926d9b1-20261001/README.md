# Build 62 core native result

Frozen b926d9b1 builds/signs/verifies successfully. Native core execution with
one job/no retries reaches all 127 cases: 107 SUCCESS and 20 FAILURE. The
registration crashes are gone. The document Mojo fixture mistakes
RequiresFreshFactory (a target-network choice) for proxy installation, and its
QUEUED ThreadPool prevents TestBrowserContext's teardown tasks from completing.
These are real executed failure results, not an overall core pass.

The corrected fixture keeps Chromium cleanup asynchronous and gates only its
injected secret store with owned events. It checks a disabled/unknown empty
builder by the finished native terminal identity; configured behavior remains
verified by actual IPC request/response effects. Pending secret revocations,
priority and cancellation still run with a deterministic worker boundary.
No production override or assertion is removed to bypass these results.

An owner TERM was requested once the repeated known teardown timeouts were
identified. At that snapshot there was no descendant batch left; the runner
records direct exit 1 and the complete summary above. No new pass is inferred.
Installed Ahoi remains Build 59; corrected-source native execution is required.
