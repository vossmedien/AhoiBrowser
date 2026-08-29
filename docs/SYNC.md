# Sync architecture

Ahoi sync is optional, local-first and disabled by default. The default path
does not construct `CKContainer`, request an iCloud account, read a sync key or
start network work. Enabling the profile/Companion preference creates transport
only when the signed app also supplies a non-placeholder container, the required
entitlements and an externally provisioned 32-byte payload key. Failure at any
of those gates leaves SQLite/JSON persistence, workspaces, tabs and history
usable locally.

`config/sync-policy.json` is the normative allow/deny contract. Cookies,
passwords, autofill, HTTP-auth or header credentials, permissions, site data,
cache, extension storage, Keychain secrets, split topology and incognito data
never enter a sync record. The currently implemented record slice is devices,
device sessions, workspaces, tree nodes/order, tombstones, normal device tabs,
ordinary browser history and signed remote commands. Other allowlisted policy
domains remain future work and must not be reported as transported until they
have a concrete record adapter and tests.

## Wire v2

Chromium's `sync_serialization.cc` and the Companion's
`DesktopWirePayloadCodec.swift` emit the same sorted JSON. UUIDs are lowercase,
64-bit Windows-epoch microseconds are decimal strings, and every v2 payload has
an exact `field_versions` map. A field clock is an HLC tuple of physical
microseconds, logical counter and device UUID. V1 payloads remain readable;
new or modified values are written as v2.

Fields merge independently by HLC. The following groups are atomic because a
partial merge would create invalid state:

- tree `workspace_id + parent_id + sort_key` (`location`);
- device-session `last_seen + active` (`liveness`);
- remote-command immutable request and monotonic status/ack.

Creation identity, device/type ownership, node kind and command request are
immutable. An equal field clock with different bytes, an immutable mismatch or
an incomplete v2 field map is quarantined. Disjoint offline edits are unioned,
then the converged union receives a deterministic record clock strictly newer
than both inputs before it is put back in the outbox. That successor avoids an
equal-record-clock `serverRecordChanged` loop and makes later devices reach the
same value without a second authority.

Exact Mac/Swift golden tests pin the v2 remote-tab bytes and the command signing
canonicalization. Device kinds map to Mac/iPhone/iPad explicitly; unknown kinds
are not silently promoted.

## Local store v3 and lifecycle

The profile database is SQLite schema v3. Migration is transactional: existing
v1/v2 rows remain readable and are upgraded lazily, while v3 adds quarantine and
durable deletion-watermark tables. A local mutation and its outbox row commit in
one transaction. A downloaded provider page, inbox dedupe rows and the next
change token also commit together.

Invalid individual remote records are quarantined locally without logging their
payload. The valid remainder of the page is applied and the provider token can
advance, avoiding one poison record permanently blocking sync. Tombstones are
retained for 30 days and compact only after their outbox row is acknowledged.
Compaction leaves a durable version watermark, so a delayed pre-deletion record
cannot resurrect the identity. History retention defaults to 90 days and accepts
30, 90, 365 or unlimited (`-1`). Expiry creates normal sync tombstones before
compaction.

The desktop service publishes a device-session heartbeat every five minutes
while its sync work is active. Mobile does not run a foreground polling loop;
it reconciles its restored normal tabs at launch, when the scene becomes active
and in response to local browser mutations. A clean desktop shutdown first
persists tombstones for every open normal tab and an `active=false` session
update; network completion is not required during termination. On restart, any
stale local active session and surviving local tab rows are closed before
publishing the new session. Remote sessions remain visible for at most seven
days and are actionable for only 15 minutes. Retired devices,
inactive/tombstoned sessions, unsafe URLs and incognito tabs never reach
`DeviceTabsService` observers.

## Workspaces, tree and history authority

`ProfileSyncService` observes the normal `TabTreeStore` through the UI-free
`ProfileSyncUiBridge`. It exports local tree mutations into the sync outbox and
reconciles downloaded workspaces/nodes back into that same store. Initial merge
preserves the newer side; later callbacks use stable IDs, field clocks and
canonical order keys. Cycles, missing parents and concurrent delete/move cases
are repaired by `tab_tree_sync_adapter` into the normal recovery folder before
the UI is notified. The sync core has no dependency on the UI-owning session
target.

Ordinary Chromium history is observed through `HistoryService`. Stable visit
IDs derive from the profile device plus Chromium visit ID. Only browsed
`http`/`https` entries with a host and no URL userinfo are eligible; hidden,
404, non-browsed, file/data/custom-scheme and credential-bearing entries are
excluded. Remote changes are inserted/deleted through the regular history
service, with source/version guards preventing reflection loops and duplicates.

## CloudKit transport

The macOS and Companion providers use the private database and one custom zone.
URLs, titles, history, tab and tree payloads are sealed with AES-256-GCM before
the envelope is written exclusively through `CKRecord.encryptedValues`.
Queryable fields contain only conflict/routing metadata. The code does not
invent a KDF, recovery phrase, fallback key or fake credential.

`CKSyncEngine` opaque state is stored atomically. On Mobile, durable local
mutations enqueue pending record changes and CKSyncEngine automatic transport
plus delegate events form the event-driven path; manual sync and foreground
reconciliation use the same outbox and fetched-record inbox. There is no Mobile
foreground polling timer. Retryable CloudKit errors retain work and honor retry
hints. Account changes and zone loss persist separate fail-closed recovery
gates; the host must explicitly choose whether to requeue retained local data.

Every distinct fetched encrypted envelope is staged durably before the provider
selects its one transport snapshot. The domain bridge decrypts and merges each
staged candidate, then acknowledges that exact envelope only after successful
import. Failed authentication, key access or local persistence therefore keeps
the candidate pending instead of silently advancing past it. Equal-version
payload divergence is resolved only at the authenticated field/domain merge or
quarantined there; transport-level last-writer selection is not convergence
evidence.

## Signed remote control

Remote control is separately off by default. A Mac accepts only an approved
source-device UUID mapped to a raw Ed25519 public key. Signatures cover command
ID, source, target, nonce, issue time and exactly one open/focus/close action.
The target enforces a five-minute TTL, target binding, safe network URLs and a
persistent command-ID/source-nonce replay claim before dispatch on the UI
thread. Incognito, bulk close, scripts, file/data URLs and credential URLs are
not representable. Execution produces a delivered/executed/failed record ack.

## External Apple boundary

No Team ID, production container, signing identity, provisioning profile or
secret is tracked. `config/macos-entitlements.json` intentionally remains the
current exact no-CloudKit release allowlist. The external release owner must
materialize matching App IDs, container/environment entitlements, Keychain
access groups and provisioning profiles in the supplied templates, update the
exact release allowlist for that signed build, and then prove an entitled
Mac–iPhone/iPad roundtrip. The granular external contracts are
`ios-final-bundle-team-profile`, `ios-cloudkit-keychain-capabilities` and
`cloudkit-device-validation` in `config/external-gates.json`. Until those are
closed, real CloudKit mutation remains `BLOCKED_ENTITLEMENT`/`NOT_RUN`.
