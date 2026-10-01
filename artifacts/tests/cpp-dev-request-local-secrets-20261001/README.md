# Request-local document header secrets

This corrects 7b27d411's unbuilt document-secret adoption design. The existing
DeveloperSecretStore contract permits resolved values only for the live request,
so the per-document plaintext adoption and its two regression methods are
removed. Primary navigation handling and persistent reference-only storage
retain their prior boundaries. The document factory still binds frame token,
committed navigation ID, origin, normal context and explicit enablement.

Secret-backed requests clone and own the native factory chain while an
independent MayBlock store resolves both header directions atomically. Before
using values the reply rechecks live document/master state and exact current
saved rules. Failure keeps headers dormant; revocation removes all overrides.
The materialized profile belongs only to the request-local throttle until
completion/cancellation. The tab network snapshot contains only references;
no plaintext cache, preference write, Sync payload or real Keychain test exists.
Early priority is forwarded after native startup, and premature redirect calls
fail without dereferencing an unbound loader. Closing the renderer cancels
pending work through a weak reply.

Eighteen written Mojo regression methods now include fresh resolution for each
request, atomic failure, three deferred revocations, pre-start cancellation and
early priority. All factories in those tests inject synthetic stores, including
the unresolved-reference case; none accesses the user's Keychain. The ordinary
request path retains the cheap metadata lookup; saved-rule deserialization is
only part of the deferred secret reply revalidation.

Five pinned-Clang checks and ordered composition pass. These are type/syntax
checks, not native execution or installed-browser acceptance. The old evidence
remains historical and does not approve the discarded document plaintext
lifetime. Link/run the exact next package; first-subresource/BFCache behavior,
worker ownership and complete master-disable behavior remain runtime/product
gates. Build 60's native UI failure and installation hold remain unchanged.
