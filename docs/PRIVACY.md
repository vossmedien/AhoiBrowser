# Privacy contract

The default normal profile aims for strong tracking reduction while preserving
ordinary first-party logins, payments, media, extensions, and developer work.
The product ships no usage analytics, engagement pings, remote experiments,
advertising identifier, automatic crash upload, or AI service traffic.

## Default controls

- keep Ahoi's website-compatibility mode enabled by default; stronger
  third-party-cookie enforcement is an explicit global or per-site opt-in
- enable Global Privacy Control in the explicit `Mehr Schutz` mode; the
  compatibility default does not rewrite website requests
- use HTTPS-First behavior without suppressing meaningful certificate errors
- remove an auditable conservative set of known tracking query parameters
- reduce cross-site referrer detail
- disable browser advertising APIs not needed for core compatibility
- in `Mehr Schutz`, remove high-entropy architecture, bitness, model, full
  version and platform-version User-Agent client hints from third-party
  subresource requests while retaining low-entropy and first-party hints;
  do not spoof Web APIs or claim anonymity or broad anti-fingerprinting
- retain Chromium Standard Safe Browsing; Enhanced protection is off by default

The two user-facing modes are `Maximale Website-Kompatibilität` and `Mehr
Schutz`. `Mehr Schutz` blocks unpartitioned third-party cookies while preserving
Chromium's CHIPS, Storage Access, explicit exception and enterprise-policy
ordering. A per-origin compatibility override can repair a site without
disabling the sandbox, TLS validation or site isolation. Overrides are visible
and easy to reset.

Ahoi does not block advertising or third-party resource hosts in either mode.
Broad request and cosmetic filtering belongs to an independently installable
extension such as uBlock Origin, so a broken site can be diagnosed and repaired
without an opaque second ad blocker in the browser core.

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
