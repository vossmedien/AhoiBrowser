# Privacy contract

The default normal profile aims for strong tracking reduction while preserving
ordinary first-party logins, payments, media, extensions, and developer work.
The product ships no usage analytics, engagement pings, remote experiments,
advertising identifier, automatic crash upload, or AI service traffic.

## Default controls

- block third-party cookies with Chromium-compatible partitioning/CHIPS behavior
- enable Global Privacy Control
- use HTTPS-First behavior without suppressing meaningful certificate errors
- remove an auditable conservative set of known tracking query parameters
- reduce cross-site referrer detail
- disable browser advertising APIs not needed for core compatibility
- apply limited third-party fingerprinting defenses only where measurable and
  compatible; do not claim anonymity or broad anti-fingerprinting
- retain Chromium Standard Safe Browsing; Enhanced protection is off by default

Per-origin and global Chromium-compatible modes can relax Ahoi-specific privacy
changes without disabling sandbox, TLS validation, or site isolation. Overrides
are visible and easy to reset.

## Local and synced data

The exact sync allow/deny lists are machine-readable in
`config/sync-policy.json`. Incognito data is never persisted or synced. HTTP-auth
secrets, passwords, cookies, autofill, site storage, extension storage,
permissions, cache, Keychain values, and secret headers never sync.

History sync defaults to 90 days with 30, 90, 365, and unlimited choices.
Deleting local or synced permitted records creates tombstones so deleted items
do not silently reappear from an offline device.

## Diagnostics

Crash reports remain local unless a future explicitly opt-in, documented,
redacted path passes privacy review. Evidence collection uses synthetic accounts
and must redact URLs, page content, headers, credentials, and profile paths when
they can reveal private information.
