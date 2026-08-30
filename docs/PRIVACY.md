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

## Mobile WebKit and private browsing

The Mobile source uses system WebKit rather than a second engine. Normal tabs
use the persistent default website data store. All private tabs in the running
private session share one nonpersistent WebKit store so they can interact like
one private session while remaining separate from normal cookies and site data.
Private tab records are excluded from session restore, history, search,
device-tab publication and sync, and clearing the last private session releases
that store.

Private browsing is not anonymity and does not erase explicit user output. A
download started from a private tab uses that private WebKit session for the
request but writes the chosen result into the app's persistent Downloads
directory. Files selected for upload and items explicitly shared leave the
private browser boundary by user action.

Camera, microphone, combined capture and motion requests are origin-labelled,
default-denied and not copied into an Ahoi permission database. JavaScript
dialogs, file selection and external-app handoff show the initiating origin;
file access additionally requires the system importer. Whether the final signed
app receives, remembers or revokes OS/WebKit permissions as intended must be
verified on physical devices.

## Mobile Privacy Manifest

The app and package source include matching `PrivacyInfo.xcprivacy` manifests.
They currently declare:

- no tracking and no tracking domains;
- browsing history, search history, other user content and device ID as
  unlinked, non-tracking data used for app functionality; and
- UserDefaults accessed-API reason `CA92.1`.

This is a conservative source declaration intended to cover the browser's
local-first records and optional private-CloudKit functionality. It is not a
conclusion that every device-resident value necessarily counts as
"collected" under Apple's current App Store definition, nor is it proof that
the final archive or App Store Connect answers use the right classification.
Release review must reconcile the exact candidate's merged manifest, embedded
SDK manifests, runtime endpoints, retention and account behavior with Apple's
definitions and the submitted privacy answers.

The 2026-08-30 simulator builds and tests do not close that review. A signed
development build was installed and visibly smoke-tested on a physical iPhone
16 Pro Max running iOS 26.6. After an initial lock-related CLI launch rejection,
the host's iPhone device-management/synchronization flow launched the app; this
was unrelated to Ahoi CloudKit sync. The bounded smoke covered cold start,
HTTPS `example.com`, Browser Actions, a new private tab and restoration of the
normal Example tab with the private tab excluded after targeted process
termination. This is useful private-session surface evidence, but it does not
establish complete WebKit storage isolation, endpoint inventory, permission
behavior, archive-manifest correctness or App Store privacy classification.

## Diagnostics

Crash reports remain local unless a future explicitly opt-in, documented,
redacted path passes privacy review. Evidence collection uses synthetic accounts
and must redact URLs, page content, headers, credentials, and profile paths when
they can reveal private information.
