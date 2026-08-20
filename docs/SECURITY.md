# Security engineering

## Supported versions

There is no released or supported AhoiBrowser version during Phase 0. Do not
use the current checkout as a daily browser or assume that a local build
receives security updates. This section will become a per-channel support
matrix before the first public binary release.

## Upstream security

AhoiBrowser inherits Chromium's sandbox, process model, site isolation, network
service, TLS stack, Safe Browsing integration, permission model, password store,
and extension isolation. Ahoi patches must not weaken those controls. Every
release records Chromium version/commit, enabled feature gates, signing identity,
entitlements, notarization result, and sandbox/process smoke evidence.

Critical upstream fixes target 48 hours; routine Stable updates target seven
days. If a patch cannot rebase safely, features are removed or disabled before
shipping an exposed build.

## Secret handling

Passwords, HTTP-auth credentials, secret header values, signing material,
CloudKit encryption keys, remote-command keys, cookies, and tokens may only live
in the Chromium password store, macOS Keychain/Secure Enclave-backed facilities,
or ephemeral memory appropriate to their function. They never appear in logs,
sync records, crash annotations, screenshots, test fixtures, or Git history.

Tests use synthetic credentials. Saved password reveal requires user presence
through LocalAuthentication/Touch ID. Revealing a password currently entered in
a webpage is a distinct local UI action and must not bypass site or OS security.

## Developer-tool risk

JavaScript injection, main-world execution, request/response header rewriting,
CSP/CORS changes, and cookie mutation are powerful and potentially dangerous.
They are off by default, scoped by origin or explicit session, visibly active,
reversible, and excluded from secret-bearing origins unless a user explicitly
overrides a warning. Secret values are Keychain references, not serialized text.

## Release checks

- Hardened Runtime and least-privilege entitlements
- an exact, Chromium-pin-bound entitlement allowlist for every signed process
  role; unknown code paths and additional entitlements fail closed
- Developer ID signature verification for every nested executable/framework
- Apple notarization and stapling verification
- Chromium sandbox, renderer, GPU, utility, and network process verification
- site-isolation cross-origin process check
- update signature, rollback prevention, atomic replacement, and failure recovery
- endpoint and privacy audit on a fresh profile
- dependency/license inventory and secret scan
