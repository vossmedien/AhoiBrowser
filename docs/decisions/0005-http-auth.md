# ADR 0005: HTTP authentication stays in Chromium's auth stack

Status: accepted

## Decision

Enhance the native auth challenge UI and credential selection around Chromium's
existing `HttpAuthManager`, `LoginHandler`, password store, and Keychain path.
Credential identity includes scheme, host, port, realm, and protection space.

## Consequences

Basic/Digest behavior, connection/session caching, proxy separation, TLS, and
network isolation remain upstream-correct. Page injection, URL-embedded secrets,
or a parallel credentials database are rejected.
