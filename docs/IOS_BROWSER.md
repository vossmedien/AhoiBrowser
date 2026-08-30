# Native AhoiBrowser for iOS and iPadOS

Status: **DEVELOPMENT_EVIDENCE_ONLY — NOT A RELEASE CANDIDATE**. The former
local-first Companion has been migrated into the `AhoiBrowser Mobile` source
product and a native WebKit browser slice is present in the worktree. On
2026-08-30 the Debug iPhone simulator build and the universal (`arm64` and
`x86_64`) Release iPad simulator build succeeded. The run also completed 56
Mobile Core tests with zero failures and two entitlement-dependent skips, three
UI tests with zero failures, and 36 CloudKit/security package tests with zero
failures.

A development build was signed and installed on a physical iPhone 16 Pro Max
running iOS 26.6. The first CLI launch was rejected while the device was locked;
after unlock, launch through the host's iPhone device-management/synchronization
flow visibly proved Ahoi cold start, HTTPS `example.com`, Browser Actions, a new
private tab and, after targeted termination of the Ahoi process, restoration of
the normal Example tab with the private tab excluded. This launch path is
unrelated to Ahoi CloudKit sync. Screenshots are retained under
[`audit-evidence/2026-08-30-ios-device-preflight/`](audit-evidence/2026-08-30-ios-device-preflight/).
This is a bounded physical-development smoke, not completion of any full
registry journey. The available iPad (6th generation) runs iPadOS 17.7.10 and
cannot install this iOS/iPadOS 26 target. There is still no distribution
archive, default-browser entitlement, entitled CloudKit roundtrip or TestFlight
installation.
`MOB-USER-01` through `MOB-USER-15` and `IOS-01` through `IOS-15` are
release-critical and remain `NOT_RUN` in `config/test-registry.json`. The
evidence contract is documented in
[`IOS_BROWSER_E2E_EVIDENCE.md`](IOS_BROWSER_E2E_EVIDENCE.md).

The mobile product is a native SwiftUI browser for **iOS/iPadOS 26 and later**.
This resolves the previous contradiction where this document claimed 17+
while SwiftPM and Xcode required 26.0. It uses the system-provided WebKit
`WebView`/`WebPage` APIs rather than porting Chromium or shipping an alternate
engine. The checked-in implementation lives under `apps/AhoiMobile/` and can
also be opened as the generated `AhoiMobile.xcodeproj`. Its executable target
is intentionally usable
without iCloud: it starts with a file-backed local store and does not construct
`CKContainer` until the user enables the default-off sync preference. Transport
then still requires an Apple-configured target, a real development container,
matching entitlements and an externally provisioned payload key.

## Best-practice decision

The sync implementation uses Apple's `CKSyncEngine` (iOS 17+/macOS 14+) over
the user's private CloudKit database and one custom record zone. This is the
current Apple-supported local-first API for change tokens, subscriptions,
pending changes, batching, account transitions, and recoverable retry handling.
The companion does not implement a parallel change-token protocol or poll
CloudKit directly.

Relevant Apple contracts:

- [`CKSyncEngine`](https://developer.apple.com/documentation/cloudkit/cksyncengine)
  owns scheduled/manual fetch and send operations;
- [`CKSyncEngine.State`](https://developer.apple.com/documentation/cloudkit/cksyncenginestate)
  serializes server tokens, subscriptions, and pending changes across launches;
- [`CKSyncEngineDelegate`](https://developer.apple.com/documentation/cloudkit/cksyncenginedelegate)
  supplies bounded record batches and receives fetched/sent/account/error events.

`CloudKitSyncProvider` follows those contracts. It persists
`CKSyncEngine.State.Serialization` through `FileSyncEngineStateStore`, queues
zone and record changes through the engine state, uses
`RecordZoneChangeBatch(pendingChanges:recordProvider:)` for Apple's 250-record
batch limit, scopes fetch/send operations to the Ahoi custom zone, and never
calls delegate-triggering engine methods from inside a delegate event.

This choice keeps the sync seam close to Apple's supported implementation while
leaving a provider-independent `InMemoryCloudSyncBackend` for deterministic
tests. The backend is not a claim of CloudKit compatibility; it exercises the
same boundary and conflict order without a network or account.

## Shared schema and desktop wire contract

The shared Swift package under `spikes/cloudkit` defines the CloudKit envelope.
The companion's `DesktopWirePayloadCodec` and Chromium's
`sync_serialization.cc` implement the same canonical plaintext JSON carried by
that envelope. New writes use wire v2 with an exact field-level HLC map; legacy
v1 rows remain readable. A disjoint-field union is re-enqueued with the same
deterministic successor record clock on both platforms, so CloudKit sees a
strictly newer converged value rather than an equal-clock payload conflict.
Golden tests on both sides pin exact bytes, including
sorted snake-case keys, lowercase UUIDs, decimal-string Windows-epoch
microseconds, model version, field clocks and tombstone fields. Every syncable
entity has a stable UUID, schema version, HLC, originating device, and tombstone
state where deletion is meaningful.

| Entity | Stable identity | Important fields |
| --- | --- | --- |
| `Device` | `DeviceID` | `deviceName`, `deviceKind` (`mac`, `iPhone`, `iPad`), `lastSeenAt`, online/revoked state |
| `Workspace` | `WorkspaceID` | name, icon, ARGB accent, canonical sort key, `SyncVersion`, tombstone |
| `TreeNode` | `TreeNodeID` | workspace/parent IDs, unlimited nesting, folder/page kind, title, URL, icon, ARGB accent, canonical `OrderKey`, tombstone |
| `DeviceSession` | `DeviceSessionID` | device ID/name/kind, optional workspace ID, `lastActiveAt`, online state, tombstone |
| `RemoteTab` | `TabID` | device/session IDs, `deviceKind`, `deviceName`, workspace ID/name, title, URL, `lastActiveAt` HLC, normal context only, tombstone/open state |
| `HistoryVisit` | `HistoryVisitID` | device ID, URL/title, visit HLC, transition, tombstone |
| `RemoteCommand` | `CommandID` | signed source/target, nonce, issued/expiry time, one open/focus/close action, delivery status/result |
| `SyncVersion` | value object | schema version, top-level HLC/device and exact `fieldVersions` map |
| `Tombstone` | entity UUID | deletion HLC/device, original placement, purge-after time |

`RemoteTab` and `HistoryVisit` validate URLs at construction: only `http` and
`https` with a host are accepted, and URL userinfo is rejected. An incognito
tab is rejected before it can enter the local store, search index, outbox, or
CloudKit payload. The UI therefore has no secondary filtering path that could
accidentally display private tabs.

The canonical remote-tab fields are deliberately compatible with the Chromium
sidebar contract: `tabID`, `deviceID`, `deviceKind`, `deviceName`,
`workspaceID`, `workspaceName`, `title`, `url`, `lastActiveAt` (HLC including
physical milliseconds), `context`, `isOpen`, `version`, and `tombstone`.
The stable Swift raw values are `mac`, `iPhone`, and `iPad`. The canonical
desktop integers are `0` (`kMacDesktop`), `1` (`kIPhone`), `2` (`kIPad`), and
`3` (`kOther`). Unknown devices are not promoted to a remote-tab kind.

## Local-first persistence and search

`LocalFirstRepository` is the app-facing actor. It loads a `LocalCompanionStore`,
mutates local state immediately, writes it back, and rebuilds the local
`LocalSearchIndex`. `InMemoryCompanionStore` makes previews and tests fully
reproducible. `FileCompanionStore` is an atomic JSON persistence seam that can
be replaced by SwiftData/SQLite without changing the UI or CloudKit provider.
It does not claim application-layer encryption; the source relies on the app
container and platform Data Protection, whose exact signed-target behavior must
still be verified on a locked physical device.

`FileSyncRecordStore` is the corresponding durable outbox-payload seam. This
matters because `CKSyncEngine.State.Serialization` persists pending record IDs
and change tokens, while the provider still needs the encrypted `SyncRecord`
body to recreate a `CKRecord` after an app restart.

`FileSyncQuarantineStore` persists only record UUIDs and bounded reason codes;
it never copies plaintext or sealed payload bytes into diagnostics. A corrupt
quarantine file prevents provider construction, leaving the app local-only.

Search is intentionally local. URLs, titles, tree payloads, history, and tabs
are opaque to CloudKit queries because their sealed representation is written
to `CKRecord.encryptedValues` by the shared codec. The index covers workspace
names, folders, saved pages, normal device tabs, and history; it never indexes
incognito or secret classes.

## CloudKit provider behavior

`CloudKitSyncProvider` uses:

- `CKContainer.privateCloudDatabase`;
- one custom zone (`AhoiBrowserSyncZone` by default);
- `AppleCloudKitRecordCodec` for normal conflict metadata and
  `encryptedValues` for opaque payloads;
- the existing exhaustive `SyncBoundary` before enqueue and after fetch;
- persisted engine state, pending record-zone changes, and a local record store;
- deterministic HLC/tombstone/device/value conflict resolution;
- a quarantine seam for corrupt, unknown, physically deleted, or disallowed
  records;
- explicit status values for idle, offline, account-required, retry-scheduled,
  conflict-resolved, quarantined, and failed states.

CloudKit's recoverable network, rate-limit, service, token-expiry, and zone
errors are surfaced as status with `retryAfterSeconds` where supplied by
CloudKit. Account changes clear only the engine's persisted token state; local
records are retained and are never silently deleted. A physical CloudKit
record deletion without a validated tombstone is quarantined rather than
turning into local data loss. The public provider has no raw delete method.
Account-transition and deleted-zone confirmation gates are stored in a
separate atomic safety-state file, so clearing or replacing Apple's opaque
engine serialization cannot clear the privacy decision. A corrupt safety file
fails provider construction; the app remains local-only. Zone preparation
suppresses record batches until the custom-zone save completes, so a first run
cannot upload records before the zone exists.

The macOS implementation under `ahoi/browser/sync` uses the same private
database, custom zone, record type, metadata fields, encrypted-value envelope,
and canonical plaintext. `ProfileSyncService` owns a `SyncPump` when and only
when a valid container configuration and externally provisioned key are
available. It requests sync after initialization, every five minutes, after a
local tab mutation, and through its public manual `SyncNow()` seam. Missing
CloudKit configuration, account, entitlement, or key disables only transport;
the profile SQLite store and local tab publishing remain usable.

`ProfileSyncService` deliberately has no dependency on the UI-owning session
target. The narrow, UI-free `ProfileSyncUiBridge` contract is implemented by
`SessionBridge` and attached by the browser sidebar, which already owns both
objects. Attachments are reference-counted across windows and held through a
weak pointer invalidated during SessionBridge shutdown. This keeps the sync
core below `chrome/browser/ui` without introducing a second tree authority.

The desktop provider atomically persists both Apple's opaque
`CKSyncEngineStateSerialization` and a local fetched-change inbox. The inbox
uses an acknowledge-on-next-token handshake, so a crash before or after SQLite
commit causes at most a safe mutation-ID replay, not lost remote data. Physical
CloudKit deletes never become local deletes. `serverRecordChanged` keeps the
newer HLC record, acknowledges an exactly identical version/payload, and never
silently picks one of two different payloads with an equal HLC. Account changes
and zone loss enter a restart-persistent fail-closed recovery state rather than
silently uploading the old local dataset.

The browser host must present the account decision explicitly and then call
`ProfileSyncService::ConfirmCloudKitAccountTransition(allow_local_upload)` or
`ConfirmCloudKitZoneRecovery()`. The account path either republishes every
retained canonical record or atomically discards only the old transport outbox;
it never deletes the local records. The zone path recreates the custom zone and
requeues every retained record, including tombstones. Both calls first verify
that the corresponding provider gate is actually pending, so an accidental
confirmation cannot clear unrelated unsent work. A successful confirmation
triggers the regular `SyncPump`; a failed or missing provider remains local-only.

On clean profile shutdown, the browser writes normal-tab tombstones and a
`DeviceSession(active=false)` record to the durable local outbox. Network work
is deliberately not required during termination; the next enabled sync sends
those records. `DeviceTabsService` excludes incognito rows, invalid IDs,
inactive/tombstoned sessions, unsafe URLs, and sessions whose last-seen time is
more than seven days old. The separate remote-action freshness check is 15
minutes, so a visible archival session is not automatically controllable.

Both provider initializers reject malformed container IDs and blank zone
names. The verified public Team/bundle values and intended dedicated container
ID are versioned in the Mobile public xcconfig; they are not secrets. The live
portal currently verifies the App ID capability switches but not the intended
container's existence or assignment: it shows zero assignments, and the only
Team container belongs to DisplayPilot and is explicitly invalid for Ahoi. Signing keys,
payload/command key bytes, certificates and provisioning profiles remain outside
the repository. The iOS and macOS entitlement templates contain build-setting
references and are not attached to provider-free local targets. The macOS
template configuration is
`ahoi/browser/sync/AhoiBrowserCloudKit.xcconfig.template`; the signed fork must
inject its values into the product Info.plist and matching entitlements.
`CompanionCloudKitBootstrap` reads the equivalent iOS settings. Empty or
unresolved values leave the provider disabled, so a placeholder cannot
accidentally contact CloudKit. Turning the UI preference off cancels and drops
the provider without removing local state; turning it on invokes the guarded
runtime factory immediately, with no restart-only side path.

### Release and entitlement seam

Mobile has five explicit Xcode configurations: `DebugLocal`,
`CloudKitDevelopment`, `TestFlightBootstrap`, `DefaultBrowserDevelopment` and
`ReleasePostGrant`. `DebugLocal` has no CloudKit, Push, Keychain-group or
default-browser source entitlement. The two pre-grant entitled modes use
`AhoiMobile.entitlements.template`; only the two post-grant modes use
`AhoiMobile.DefaultBrowser.entitlements.template`. Automatic/Cloud-Managed
Signing is primary, while a manual profile is an explicit fail-closed fallback.
`TestFlightBootstrap` is Production-CloudKit capable but intentionally lacks
`com.apple.developer.web-browser` and remains eligible for external/public
TestFlight. A fresh `ReleasePostGrant` archive is mandatory after Apple grants
the managed entitlement.

The tracked [`config/macos-entitlements.json`](../config/macos-entitlements.json)
remains an exact desktop allowlist used by signing and installed-app
verification. Its `browser-app` role intentionally contains no CloudKit
entitlement today. Consequently the current ordinary desktop release cannot
accidentally gain iCloud access, and a binary with unreviewed extra CloudKit
entitlements fails exact verification.

The external Apple/release owner must materialize both sides of the same gate:

1. bind the Browser app to the same verified Team-owned multi-platform App ID;
2. stamp the Browser app's `Info.plist` with the non-placeholder
   `AHOI_CLOUDKIT_*` and `AHOI_SYNC_KEYCHAIN_*` values described by
   `AhoiBrowserCloudKit.xcconfig.template`;
3. merge the concrete container, CloudKit service, Keychain access group and
   environment-appropriate push entitlement from
   `AhoiBrowserCloudKit.entitlements.template` into the exact `browser-app`
   rule used for that signed release;
4. sign with a matching App ID/provisioning capability, then run the normal
   exact entitlement verifier.

The repository does not interpolate placeholders into desktop release policy
and does not ship a permissive wildcard. Until the Apple Team assigns the
dedicated container and supplies matching access groups/profiles, the only valid
tracked desktop policy is the current no-CloudKit policy and desktop transport
remains disabled. Mobile keeps the synchronizable payload group and per-device
command-signing group explicitly separate and rejects a foreign container or
additional group in its mode-aware preflight.

The current source only reads the configured AES and Ed25519 items from
Keychain. No real payload/command keys or audited operational path for their
provisioning, rotation, recovery and revocation has been supplied. The installed
Mac development app is not CloudKit-entitled and has no concrete CloudKit
configuration, so it cannot serve as the counterparty for a real roundtrip.

Private payloads are first sealed with AES-256-GCM and then stored only in
`CKRecord.encryptedValues`. The 32-byte key must already exist in the shared,
data-protected, synchronizable Keychain item named by the configuration. The
code never generates a replacement, derives one from a password, or invents a
recovery protocol. A missing or malformed key fails closed. Cookies, passwords,
autofill data, HTTP-auth/header secrets, site data, cache, permissions,
incognito data, and extension storage are outside the allowed record classes.

## Signed single-tab remote control

Remote control is a separate encrypted sync entity, not an implicit privilege
of seeing a remote tab. Browser execution is off by default. The Mac requires
both the opt-in pref and a source-device UUID mapped to an approved raw 32-byte
Ed25519 public key. The Companion reads a raw Ed25519 private key only from the
externally provisioned Keychain item named by `AHOI_COMMAND_KEYCHAIN_*`; it
never generates or replaces that key. If any container, payload key, command
key or entitlement is missing, the send path is unavailable.

Every signature covers canonical JSON shared by the Swift and C++ golden
tests: command ID, source and target devices, random nonce, issue time, and the
single action. The target enforces a five-minute TTL with bounded future skew,
target binding, Ed25519 verification and approved-device policy before an
atomic SQLite replay claim. Both command IDs and source-scoped nonces survive
restart. Only one normal-tab `open`, `focus` or `close` is representable;
incognito, mass close, credentials in URLs, and `javascript:`, `data:`,
`file:` or custom schemes are rejected.

Execution returns to the regular UI-thread `SessionBridge`: open uses a normal
foreground tab, while focus/close resolve the already published stable tab ID.
The target writes a signed-payload-preserving delivered/executed/failed ack
with a new HLC, and the Companion renders that status. No cookies, page state,
arbitrary script, bulk operation or privileged browser command crosses the
seam.

## Native browser UI slice

`AhoiMobileBrowserView` owns the browser surface and adapts from an iPhone
bottom-toolbar layout to an iPad split presentation. `CompanionRootView`
remains the reusable workspace/device library inside that browser. The current
slice includes:

- system WebKit page rendering with persistent normal and nonpersistent private
  website data stores;
- address/search routing, HTTPS upgrade, Back, Forward, Reload/Stop and native
  back/forward gestures;
- normal/private tabs, tab switching, close/undo and normal-session restore;
- a distinct private surface; private tabs are never written to the session
  file;
- in-app opening of saved pages and normal remote tabs;
- local history and normal mobile-tab publication through the existing
  encrypted sync boundary;
- workspace save/move integration and the inherited workspace/tree UI;
- an idempotent first-launch migration that copies known Companion files into
  `AhoiMobile`, preserves a backup and never overwrites an existing Mobile file;

- canonically ordered workspaces and a nested tree with icons and accents;
- normal remote tabs directly in the regular tab list, with device kind badge,
  device name, workspace, title, URL, and last activity;
- local full-text search over the index;
- a default-off CloudKit toggle plus a manual action disabled until every local
  configuration/key gate is satisfied;
- errors/status without requiring a browser engine;
- a compact menu on Mac tab rows for signed open/focus/close plus the
  source-device ID, public-key fingerprint and selectable public key needed for
  explicit Mac approval.

The source has one persistent `WKWebsiteDataStore.default()` for normal
browsing and one shared `WKWebsiteDataStore.nonPersistent()` for all private
tabs in the current private session. Dropping all private tabs also releases
the private store. Private tab metadata is excluded from browser-session JSON,
history, local search, device-tab publication and CloudKit. This is a source
invariant, not a device isolation result.

Downloads stay inside WebKit's `WKDownload` path and preserve the initiating
request and source tab's normal/private data store. Ahoi sanitizes the suggested
leaf name, prevents overwrite by suffixing collisions, writes the selected
output below the app's `Documents/Downloads` directory and exposes progress,
cancel, Quick Look and share actions. A user-requested private download still
creates a persistent file; private mode does not promise to erase downloads.

Camera, microphone, combined capture and motion requests are shown with their
WebKit security origin and default to denial until the user decides. JavaScript
dialogs and file input have a presenter owned by the initiating `WebPage`; file
selection requires an origin-labelled native confirmation before the system
importer. External schemes are cancelled in WebKit and require native
origin/target confirmation before system handoff. No custom persistent
permission database or alternative browser engine is introduced.

Saved links and the primary remote-tab row are routed into the active Mobile
browser tab. The app does not instantiate a Chromium view or alternate
cookie/session store. Local mutations enqueue durable CKSyncEngine changes;
automatic transport and delegate callbacks are the normal event-driven path.
The prior foreground polling loop has been removed. Initial load, foreground
activation and the toolbar still trigger explicit reconciliation/manual sync.
Entitled push delivery, background scheduling and multi-device convergence are
not inferred from these source seams.

## Privacy Manifest source contract

`Sources/AhoiMobileApp/PrivacyInfo.xcprivacy` and the package resource manifest
declare no tracking and no tracking domains. They conservatively declare
browsing history, search history, other user content and device ID as unlinked,
non-tracking data used for app functionality, plus
`NSPrivacyAccessedAPICategoryUserDefaults` reason `CA92.1`. This source
declaration does not itself decide whether each local or private-CloudKit use is
"collected" under Apple's current App Store definition; that classification and
the corresponding App Store Connect answers require explicit review. The Xcode
project includes the app manifest as a target resource. Release engineering
must inspect the final archive's merged Privacy Manifest, every embedded SDK
manifest and actual runtime endpoints. A tracked manifest is not archive or
review evidence.

SwiftPM's `.build` directories are build products, not source artifacts. The
repository-wide `**/.build/` ignore rule excludes both Companion and spike
artifacts from Git and source packaging; Xcode/SwiftPM may recreate them
locally at any time.

## Repeatable verification commands

From the repository root:

```bash
cd spikes/cloudkit
swift test --jobs 1 --disable-index-store

cd ../../apps/AhoiMobile
swift test --jobs 1 --disable-index-store
swift build --target AhoiMobileCore --jobs 1 --disable-index-store \
  --sdk "$(xcrun --sdk iphoneos --show-sdk-path)" --triple arm64-apple-ios26.5
swift build --target AhoiMobileApp --jobs 1 --disable-index-store \
  --sdk "$(xcrun --sdk iphoneos --show-sdk-path)" --triple arm64-apple-ios26.5
```

The 2026-08-30 development run established the bounded simulator/build/test
results listed at the top of this document. The commands above remain a
repeatable source-level subset; they do not by themselves reproduce the full
Xcode simulator matrix or physical installation evidence.

The package tests exercise browser input
routing, normal/private session persistence, Companion-to-Mobile migration,
model validation, local search,
boundary enforcement, tombstone-safe in-memory sync, and conflict ordering.
The Chromium `ahoi_sync_unittests` target additionally pins the same
remote-tab and signed-command golden JSON, AES-GCM roundtrip/tamper behavior,
durable pump/replay semantics, history filtering, tree-cycle recovery, and
inactive-session filtering. Objective-C++ files can be
compile-checked without constructing a CloudKit container; no test in this
repository performs a real cloud mutation by default.
CKSyncEngine construction tests are compile-checked but skipped unless an
explicit entitled test target sets `AHOI_CLOUDKIT_TEST_ENTITLED=1`; this keeps
the default test run offline and prevents a placeholder container from making
an account/network claim. The two entitlement-dependent skips in the current
56-test Core run remain skips; they are not CloudKit passes.

## External Apple and distribution gates

The authoritative machine-readable gates live in
`config/external-gates.json`. Mobile release requires at least:

1. Apple Developer Team ownership, final Mobile bundle ID/App ID and matching
   development/distribution profiles; Automatic/Cloud-Managed Signing is the
   primary certificate path;
2. a newly registered private Ahoi CloudKit container assigned to the verified
   App ID, matching environment/iCloud/push entitlements and controlled schema
   promotion; the live portal currently shows zero assignments and the existing
   DisplayPilot container is not a fallback;
3. reviewed synchronizable Keychain groups and production key lifecycle
   provisioning (approval, cross-device availability, rotation, recovery and
   revocation) for the AES payload key and each Mobile Ed25519 command key; the
   repository intentionally supplies no KDF, fake credential or bootstrap
   secret;
4. signed, installed Mac, iPhone and iPad candidates using one controlled
   iCloud test account;
5. all pre-grant-applicable `MOB-USER-*` Computer Use and `IOS-*` assisted
   physical-device journeys,
   including permissions, file provider, downloads, accessibility, pointer,
   keyboard, backgrounding and memory pressure;
6. entitled E2E proof for offline changes, account switch, token expiry,
   conflict, tombstone/recovery, quota and device revocation; and
7. resolved trader status, App Store Connect app record/agreements/disclosures
   plus a processed
   `TestFlightBootstrap` installed internally and externally with an active
   public TestFlight link;
8. Apple's managed default-browser entitlement granted for that exact bundle,
   followed by a new profile/build/archive and post-grant TestFlight system-
   default E2E on real iPhone and iPad.

Until candidate-bound artifacts exist, this document records source coverage
plus bounded simulator/build/test and physical-development-smoke evidence only.
No processed distribution archive, managed default-browser entitlement, real
CloudKit container/key lifecycle, entitled Mac counterparty or TestFlight
candidate is available. The live Apple snapshot shows both managed browser
capabilities at `No Requests`, no App Store Connect apps, an unresolved trader
warning and zero containers assigned to the otherwise CloudKit/Push-enabled
Ahoi App ID. A local Apple Distribution identity and App Store Connect API key
are not asserted as blockers because authenticated Xcode Automatic
Signing/Organizer is the intended path. The registry status for every Mobile
user/device journey remains `NOT_RUN`.
