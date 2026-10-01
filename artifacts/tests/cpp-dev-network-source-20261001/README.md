# Developer document network adapter: source evidence

Pinned M154/Clang source checks pass for six C++ translation units: factory
adapter, its 13 written Mojo regressions, secret materialization regressions,
existing request throttle, tab runtime and the composed content-browser client.
Ordered composition passes without changing the shared checkout or outputs.
The exact files, compiler/GN arguments, commands and logs are bound in receipt.
This is syntax/type analysis, not linking, executed tests or installed acceptance.

The adapter stays within the native document factory chain before webRequest.
It forwards loader/client/control calls, uses request-local existing throttles,
and rechecks profile enablement, initiator/origin, live frame token and exact
committed navigation ID. A precommit factory cannot use the previous document's
approval; a same-origin replacement cannot reuse its old factory. Redirect
changes cannot resurrect configured headers in either normal or exempt maps,
including when restoring a pre-rule header value. Native CORS still decides.

The exact successful primary navigation can adopt its validated materialized
header snapshot before pending state is cleared. Navigation ID, exact URL,
origin and unchanged source rule metadata must all match. Later documents,
changed references and mismatched IDs remain fail-closed; persisted profiles
still contain only references. Two regression methods cover adoption/lifetime
and four rejected mismatch variants.

Ready Crest 106/110 were reviewed and deferred: they require a future
performance candidate/lease and have no product patch for this package.

Open: native linking/test execution, actual first document subresources and
back/forward-cache lifecycle, installed cache/fetch/restart journeys, and
separate proven worker ownership/materialization. No worker exemption or full
Devtoolkit completion is claimed. Build 60 remains frozen d0688d88; this code
belongs to the next candidate and does not change its prior red UI gate.
