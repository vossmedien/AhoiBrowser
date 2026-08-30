# Threat model

This document describes source-level security contracts. The 2026-08-30
development state adds bounded simulator/build/test evidence: 56 Mobile Core
test cases were reported with zero failures and two entitlement-dependent
skips, three UI tests passed, and 36 CloudKit/security package tests passed. A
signed development build was also installed on a physical iPhone 16 Pro Max
running iOS 26.6. After the first CLI launch was rejected while the device was
locked, the host's iPhone device-management/synchronization flow visibly
launched the app; this was unrelated to Ahoi CloudKit sync. The smoke proved
cold start, HTTPS `example.com`, Browser Actions, a new private tab and
restoration of the normal Example tab with the private tab excluded after
targeted Ahoi process termination. This bounded smoke supports those exact
observations but does not establish a complete physical security journey.
Mobile journeys in
`config/test-registry.json` remain `NOT_RUN` until evidence is bound to the
appropriate exact signed candidate.

## Protected assets

- browsing credentials and normal-profile WebKit/Chromium sessions;
- private-browsing website data and the boundary that keeps it out of normal
  persistence, history, session restore, search and sync;
- HTTP-auth secrets, password-manager data, Keychain keys and developer header
  secrets;
- history, workspace/tree, device-tab and encrypted sync metadata;
- remote-command authorization, replay state and approved-device identity;
- camera, microphone, motion, file-selection and external-app consent;
- downloaded files, upload selections and the origin/tab attribution of each
  browser-mediated action;
- update, App Store and signing provenance; and
- the integrity of browser navigation, native dialogs and displayed origins.

## Relevant adversaries

- malicious or compromised websites, frames, popups and cross-origin content;
- a site attempting prompt fatigue, permission confusion, tab spoofing, unsafe
  scheme launches or access to unintended local files;
- malicious downloads, misleading filenames, executable content and responses
  that change disposition or MIME type;
- malicious extensions or native-messaging hosts on desktop;
- network attackers, captive portals and malicious proxies;
- compromised, reordered or replayed sync records and remote commands;
- a local process attempting to read browser data, downloads or Keychain items,
  or to inject UI;
- compromised update, App Store Connect or TestFlight infrastructure; and
- accidental disclosure through logs, crash reports, screenshots or support
  data.

## Desktop trust boundaries

Chromium renderer processes are untrusted. Browser-process services validate
renderer messages and retain Chromium's sandbox, Site Isolation, network,
permission, download and storage boundaries. Extension processes are not trusted
with Ahoi secrets. Ahoi UI must not create a second credential, permission,
download or renderer authority.

## Mobile WebKit boundary

Web content is untrusted even though rendering, networking, TLS, cookies and
website storage are supplied by system WebKit. `WebPage`/`WebView` and
`WKWebsiteDataStore` remain the browser engine boundary; SwiftUI browser chrome
owns presentation and policy, not webpage trust.

Normal tabs use WebKit's persistent default data store. All private tabs in one
running private session use one `WKWebsiteDataStore.nonPersistent()` instance,
and the controller drops that instance when private tabs are cleared. Private
tab records are excluded from the session file, history, search, device-tab
publication and sync. These source properties do not prove process-death,
backgrounding or physical-device isolation; `MOB-USER-05`, `MOB-USER-11` and
`IOS-14` remain required runtime evidence.

Each WebKit page owns a native dialog presenter. JavaScript alert, confirm,
prompt and file-input requests carry the initiating security origin and are
cancelled when their page is discarded or closed. Camera, microphone, combined
capture and motion requests are fail-closed, origin-labelled and resolved one at
a time; Ahoi does not persist a parallel permission allowlist. External schemes
are cancelled in WebKit and require a native confirmation showing source origin
and target scheme before system handoff. Physical system prompts and hostile
frame/popup cases remain unexecuted gates.

## Download and file boundary

WebKit owns transfer execution through `WKDownload`; Ahoi retains the initiating
HTTP(S) request so response-triggered downloads use the same WebKit data store as
the source tab. Filenames are reduced to a leaf, control characters are removed,
and collisions receive a numeric suffix before writing below the app's
`Documents/Downloads` directory. The UI exposes origin, progress, cancellation,
Quick Look and sharing.

A private download may use the private in-memory WebKit session, but its
user-requested output file is intentionally persistent. Private browsing does
not erase or conceal downloaded files or items the user explicitly shares.
Upload selection is gated by a native origin-labelled confirmation and the
system file importer. Malware scanning, platform quarantine behavior, security
scoped file access, filename edge cases and real file-provider behavior require
candidate-bound device evidence; source code alone does not establish them.

## Sync and key boundary

CloudKit transports only encrypted allowlisted records and is not trusted with
plaintext payload contents. Local mutations are durable before transport and
enqueue CKSyncEngine changes; automatic transport and delegate events replace a
foreground polling loop, while manual sync and foreground reconciliation remain
explicit triggers. Every distinct fetched encrypted envelope is staged before
domain merge and acknowledged only after successful import.

CloudKit container identifiers, entitlements and AES/Ed25519 keys are external.
A missing account, container, entitlement or Keychain item leaves the app
local-only. The Mobile app may request only enumerated normal-profile tab
commands; the Mac validates approved device, signature, nonce, five-minute TTL,
replay state, target and scope. Entitled multi-device convergence, push delivery,
background behavior, key rotation/revocation and account/zone recovery remain
external `NOT_RUN` journeys.

For the current development state those dependencies are absent, not merely
unverified: no real CloudKit container or payload/command keys are configured,
no operational key-provisioning/rotation/revocation path has been demonstrated,
and the installed Mac development app is not CloudKit-entitled. Consequently
the Mac cannot serve as a real sync counterparty and the successful offline
CloudKit/security tests do not reduce the external roundtrip boundary.

## Privacy manifest boundary

The tracked app and package Privacy Manifests declare no tracking and no
tracking domains. They conservatively identify browsing history, search
history, other user content and device ID as unlinked, non-tracking data used
for app functionality, and declare the `CA92.1` reason for UserDefaults access.
These are source declarations pending review against Apple's App Store
definition of collection; they are not proof of the contents of a signed
archive or of correct App Store privacy answers. Release review must inspect
the merged archive manifest, third-party SDK manifests, runtime endpoints and
App Store Connect disclosures for the exact candidate.

## Explicit non-goals

AhoiBrowser cannot protect data after full compromise of the logged-in device
account or kernel, guarantee anonymity, bypass DRM restrictions, make unsafe
developer overrides harmless, or make a downloaded file safe. Private browsing
limits application persistence but does not hide traffic from sites, employers,
ISPs or network observers and does not remove downloads or explicit shares.

## Required abuse evidence

Release-critical evidence must cover cross-workspace session consistency,
normal/private data-store isolation, origin confusion across prompts/popups,
denial and cancellation of permissions/dialogs, malicious filenames and download
responses, unintended upload scope, unsafe external schemes, plaintext HTTP and
TLS errors, remote-command replay/expiry, poisoned sync ordering, revoked
devices, malicious extension attempts, hidden developer overrides, update or
archive signature failure and secret leakage into diagnostics. Registry entries
stay `NOT_RUN` until those cases are executed against the appropriate installed
candidate.
