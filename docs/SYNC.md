# Sync architecture

## Current binding target — 12 September 2026

Ahoi sync is optional, local-first and disabled by default. The default path
does not construct `CKContainer`, request an iCloud account, read a sync key or
start network work. Enabling the profile/Companion preference creates transport
only when the signed app also supplies a non-placeholder container, the required
entitlements and an externally provisioned 32-byte payload key. Failure at any
of those gates leaves SQLite/JSON persistence, workspaces, tabs and history
usable locally.

`config/sync-policy.json` remains the normative machine-readable runtime
allow/deny contract and must be updated atomically with the new format maps
before these target additions can activate. Cookies,
passwords, autofill, HTTP-auth or header credentials, permissions, site data,
cache, unknown/raw extension storage, Keychain secrets, device-local runtime
handles, window geometry, live focus, cookies/login/form state and incognito data
never enter a sync record. The one active format-3 target additionally includes
normal logical split groups/arrangements, shared automatic-archive policy and
entries/restore state, saved-page Home URLs, typed link-routing preferences and
configurable shortcut preferences. These additions extend format 3; they do not
create another wire format or a second sync architecture. The original 13 entity
classes are a baseline rather than a product-scope ceiling.

The historical implementation notes below cover devices, sessions, workspaces,
tree nodes, tabs and history. Current source/build/runtime status is recorded in
`UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md`; the old slice is not a ceiling on
already implemented work. At this documentation revision the new split,
archive, Home URL, routing and shortcut maps/adapters are not implemented or
runtime-accepted. `config/sync-format.json`, `config/sync-policy.json`, both
language models/codecs, canonical fixtures and real consumers must agree before
any capability is advertised or any transport claim is made.

## Historical implementation note: wire v2

The following describes the superseded source implementation and its useful
merge invariants. It is not the current authoring target: format 3 is the only
target writer, and this section does not authorize continued v2 output or a
v2/v3 compatibility product.

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

## Format-3 extension contract

The format-3 schema extends the existing baseline with the following typed
state. Exact data-class IDs, field maps, bounds and canonical fixtures must be
published atomically in the machine-readable contract before implementation is
enabled:

- a normal logical split group with stable split UUID, workspace UUID, two to
  four ordered stable `TreeNode` IDs, canonical layout and normalized divider
  ratios;
- a shared archive entry with stable page identity, archive reason/time and the
  normal split structure required for restore, plus per-workspace automatic-
  archive policy values `12h`, `24h`, `7d`, `30d` or `never`;
- a saved-page Home URL distinct from its current runtime/navigation URL; and
- positively catalogued typed link-routing and shortcut preferences. Routing
  host/path patterns are encrypted user payload, not queryable plaintext.

The existing field-clock, atomic-group, tombstone, deletion-watermark, quarantine
and fail-closed validation rules apply. Home URL and current URL never overwrite
one another by implication. Split membership/order/layout/ratios and archive
restore transitions merge deterministically through stable logical identities.
Native tab/split IDs, `WebContents` handles, window coordinates, local active
focus and live browsing/account/form state remain outside the schema.

## Historical implementation note: local store v3 and lifecycle

This describes the earlier implemented profile-store slice. The unified target's
dedicated C++ sync-store schema is independently versioned as documented in ADR
0009; neither storage schema number is the wire-format number. The transaction,
quarantine, tombstone and retention invariants below remain requirements.

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

A saved page owns a stable Home URL while its currently resident tab owns the
live navigation URL. Ordinary navigation may update the existing current-URL metadata, but never
Home implicitly. Existing local one-URL saved pages initialize Home from their
existing value; this does not reopen legacy sync-store migration. Home follows
the same URL validation,
encryption, merge and export privacy rules as other saved-page structure.

## Logical splits and automatic archive

Normal split UUID, owning workspace, ordered stable TreeNode membership,
canonical layout and normalized ratios synchronize to all opted-in Desktop
installations. A local materializer resolves that logical state to native
Chromium split IDs and tab handles. Mobile preserves recognized split metadata
losslessly across read/edit/write roundtrips but is not required to present a
Split View UI; opening or editing one member must not flatten the shared group.

An incoming topology update may rearrange shared logical state but may not close
a currently active local page, change its workspace website-session/account
context, replace its `WebContents`, discard a form or navigate it without user
action. A remotely removed member remains locally open as an ordinary tab when
necessary. Missing device presence is not logical deletion. Desktop-pair
acceptance covers simultaneous membership/order/layout/ratio changes, removal,
offline convergence, restart and restore without focus stealing or eager loads.

Automatic archive is configured per workspace as `12h`, `24h`, `7d`, `30d` or
`never`. It archives eligible inactive normal work without deleting it. Active,
saved, pinned, Keep Loaded, media-playing, capturing, downloading or unsaved-form
tabs are excluded. Shared policy, archive entry, reason/time, retained normal
split structure and restore state synchronize. A received archive transition
does not force-close an active peer page; runtime presentation can remain local
while domain state converges. Restore preserves stable page/split identity and
recreates only local native handles.

Link-routing and shortcut values enter sync only through their positive typed
settings catalogue. Routing still selects the workspace/session context before
the first request; sync never carries the resulting cookies, login, form or live
navigation state.

## CloudKit transport

The macOS and Companion providers use the private database and one custom zone.
URLs, titles, history, tab and tree payloads are sealed with AES-256-GCM before
the envelope is written exclusively through `CKRecord.encryptedValues`.
Queryable fields contain only conflict/routing metadata. The code does not
invent a KDF, recovery phrase, fallback key or fake credential.

Here `routing metadata` means minimal record transport/type/version routing. It
does not authorize plaintext user link-routing hosts or paths; those are encrypted
payload fields under the positive settings catalogue.

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

## Mac CloudKit signing boundary

`config/macos-entitlements.json` is now the single exact Mac signing policy. Its
default remains `provider-free`: the ordinary build has the upstream browser
entitlements only, embeds no provisioning profile and must contain none of the
CloudKit runtime keys. Two entitled profiles are separate and cannot be
silently interchanged:

| Profile | Identity and required readback |
| --- | --- |
| `cloudkit-development` | Apple Development, concrete Mac development profile, CloudKit `Development`, macOS APNs `development` and at least one provisioned Mac. |
| `cloudkit-production` | Developer ID Application, concrete Developer ID provisioning profile, CloudKit `Production`, macOS APNs `production`, no development-device list and `ProvisionsAllDevices=true`. |

Both profiles use bundle `app.ahoibrowser.AhoiBrowser`, container
`iCloud.app.ahoibrowser.AhoiBrowser`, the exact sync-payload group
`248AJ5BN47.app.ahoibrowser.sync` and the separate command-key group
`248AJ5BN47.app.ahoibrowser.commands`. Wildcards, unresolved placeholders,
additional groups and the DisplayPilot container fail closed in signed app
claims. An Apple-signed profile is an authorization allowlist, not a second
copy of those claims: the exact Team prefix wildcard may authorize those two
groups, services may allow CloudKit through `*`, and environment lists may
authorize Development. App ID, dedicated container, APNs mode, certificate,
dates/device shape and actual signed claims remain checked. Apple documents
this distinction in [TN3125](https://developer.apple.com/documentation/technotes/tn3125-inside-code-signing-provisioning-profiles).
On macOS the push
key is `com.apple.developer.aps-environment`; the iOS-only `aps-environment`
spelling is rejected.

The public identifiers and runtime names are mirrored without secrets in
`AhoiBrowserCloudKit.xcconfig.template`. The existing entitlement template is
the exact Development fragment; `AhoiBrowserCloudKit.Production.entitlements.template`
is the distinct Production fragment. The release tooling derives the final
browser entitlement set from the policy so the normal Chromium device/privacy
permissions remain exact as well.

For a development candidate, prepare the built app before Apple Development
signing, then verify the signed readback:

```sh
python3 scripts/release/ahoi-release.py prepare-macos-cloudkit \
  --app /path/AhoiBrowser.app \
  --signing-profile cloudkit-development \
  --provisioning-profile /private/path/AhoiBrowser-Development.provisionprofile \
  --entitlements-output /private/evidence/AhoiBrowser-Development.entitlements \
  --output /private/evidence/macos-cloudkit-development-preparation.json

AHOI_CODESIGN_IDENTITY='Apple Development: EXACT DISPLAYED IDENTITY'
codesign --force --sign "$AHOI_CODESIGN_IDENTITY" --timestamp=none \
  --entitlements /private/evidence/AhoiBrowser-Development.entitlements \
  /path/AhoiBrowser.app

AHOI_TEAM_ID=248AJ5BN47 \
AHOI_CODESIGN_IDENTITY='Apple Development: EXACT DISPLAYED IDENTITY' \
python3 scripts/release/ahoi-release.py verify-macos-cloudkit \
  --app /path/AhoiBrowser.app \
  --signing-profile cloudkit-development \
  --output /private/evidence/macos-cloudkit-development-verification.json
```

Preparation decodes the Apple-signed profile, stamps the exact runtime values,
embeds the same bytes at `Contents/embedded.provisionprofile`, writes the exact
entitlements and re-reads profile plus Info.plist. It does not generate a key,
certificate or profile and does not perform Apple-portal mutations.

Use a separate exact copy of the completed development bundle and keep the
original build/install receipts. Component development retains the existing
linker-signed libraries: do not apply a partial hardened-runtime signature to
its outer app. The Development verifier uses the existing portable component
manifest/deep-signature checks plus exact top-level entitlement, profile,
certificate and runtime readback; it is explicitly NOT release evidence.
Production's complete hardened-runtime signing/verifier remains separate.

The production `sign` command is not profile-selectable: it is bound to
`cloudkit-production`, requires `--provisioning-profile`, embeds and re-reads
that profile, binds the actual signing leaf certificate to the profile's
certificate inventory, signs leaf-to-root and records profile SHA-256, UUID,
expiry, container, groups, environments, prepared-bundle identity and signed-bundle
identity in `signed-package-provenance.json`. Later notarization, installation
and live-chain validation repeat the Production profile/readback check.

On 2026-09-08 the user authorized the required Development account setup. A
regular Xcode automatic-provisioning build created the native Mac profile
`8f149b92-89cc-4d34-a0db-1b305d4e545c` in Xcode's standard profile cache: OSX,
Team248AJ5BN47, exact Ahoi App ID/container, development APNs, one provisioned
Mac, expiry2027-09-08. The provisioning-only executable was not run or installed.
This supersedes the earlier zero-container/missing-Mac-profile inventory; it
does not prove transport, key bootstrap, Production signing or a roundtrip.
Keep those actual runtime gates separate from this successful profile creation.
