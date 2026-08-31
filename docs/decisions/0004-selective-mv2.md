# ADR 0004: No general MV2; one gated uBO Classic exception

Status: accepted for pinned dogfood bootstrap; public release still gated

## Decision

Do not restore arbitrary Manifest V2. Permit only the browser-pinned **Official
GitHub release** uBlock Origin Classic 1.74.0 package with key-derived ID
`fkgkibajhfbepljeaefdnfnegdcjomkh`, exact release commit, full-package hash,
and CRX public-key hash. The manual initial check uses a static entry compiled
into the signed browser and performs no catalog request. A separately signed
catalog remains the only path for later updates.

## Consequences

Ahoi can dogfood the exact upstream-signed package without freezing the entire
extension platform. General and unpacked MV2 remain blocked, download is bound
to the exact release URL and at most one credentialless release-asset redirect,
and Chromium's permission prompt plus atomic authorization remain mandatory.
Any changed package, key, ID, version, commit, asset path, or URL requires a new
product-security migration. Public redistribution and later catalog hosting/
signing remain independent release gates; uBlock Origin Lite/MV3 remains the
fallback if those gates fail.
