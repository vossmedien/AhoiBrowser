# Split journey focus yield, 1 October 2026

The actual shared-library `key` path is called with strict focus-yield enabled,
an already acquired fixture PID and a fake AX helper reporting another app
frontmost. It exits 8, writes cancelled/pass:false, and invokes only
`focused 290001`. It invokes neither activate nor HID. The mock helper path
contains the workspace's real spaces; the new helper invocations are quoted.
See receipt, call log and fixture output; the receipt hashes the executed library.
No real app, user input, GUI/CDP action or product acceptance occurs here.

The first fixture under `../split-focus-yield-20261001/` did not invoke its
unquoted fake helper path; its failure is preserved, not a guard PASS. A second
fixture using a temporary helper path passed before the helper calls were
quoted. This final space-path check executes the corrected source.

Strict mode yields after a previously acquired frontmost app loses focus; it
does not claim to distinguish human and generated input inside that same app.
Initial launch/relaunch retain their explicit owned acquisition. The corrected
installed lifecycle runner enables strict mode, waits once up to 120 seconds
for real input idle/absence of app/compiler/resource owners, and binds its
exact shared library and journey inputs. Its current state and PID must be
checked separately; this mock result is not its installed verdict.
