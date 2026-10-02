# Request/response override revocation source, 2 October 2026

The dispatched request proxy previously revalidated only asynchronous secret
materialization. Its later response/redirect callbacks retained the original
throttle, and ordinary non-secret requests could still use cached document
metadata after a saved-profile reset. Core140's passed pending-secret cases did
not cover these distinct phases; no failing native execution is fabricated.

Source now checks actual current owner/document/activation/saved metadata at
request admission and retains a repeatable reference-only predicate for later
response and redirect phases. Withdrawal drops response overrides and their
request-local materialized values. A redirect first restores original request
headers, removes conflicting pending normal/exempt modifications, then discards
the override. The native loader/client/control/CORS chain stays authoritative;
requests are not retroactively unsent. Explicit cache-only cross-origin scope
is compared to current cache-only eligibility, never to an unrelated tab.
There is no timer, scanner or background observer; the new lookups run only
inside an explicitly opted-in factory's actual request/protocol callbacks.

Five new actual proxy/Mojo methods cover disable after dispatch, temporary reset
with same-origin redirect and exempt injection, off/on, replacement owner, and
new plain requests after persistent reset on an existing factory. Test helpers
move into a named common fixture with out-of-line implementations, preserving
one GTest fixture identity across two test TUs and the source line budget.
The old24 methods remain present; the new five are written/syntax-checked,
**not executed**. The existing Build69 binary does not contain these changes.

Five final serial pinned-Clang syntax checks pass, including both test TUs and
the support implementation. Initial shared-header style failures remain raw
evidence; no compiler check is disabled. Sources and support header are hash-bound.
No real Keychain value, API, device, build/sign/install or release action occurs.

Next candidate combines this product correction with the already saved shelf
activation fixture. Actual core/Mojo execution, sidebar/five editor/remaining
native gates and installed visible acceptance must still run. Prior Core140/
editor5/baseline872 evidence remains separate and cannot be promoted across
changed runtime inputs merely because it was green.
