# ADR 0004: No general MV2; one gated uBO Classic exception

Status: proposed, disabled pending Phase 0 proof

## Decision

Do not restore arbitrary Manifest V2. Prototype a package-identity-bound legacy
capability only for an allowlisted, signed, hash-verified uBlock Origin Classic
distribution with its own auditable update and kill-switch path.

## Consequences

Ahoi can potentially preserve the user's preferred blocker without freezing the
entire extension platform. If secure distribution, upstream compatibility, or
legal permission fails, the feature stays off and uBlock Origin Lite/MV3 remains
the supported fallback rather than shipping a broad legacy surface.
