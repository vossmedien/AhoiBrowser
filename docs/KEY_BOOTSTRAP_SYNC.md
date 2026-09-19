# Shared first-use key setup

Common C++ and matching Swift source, 2026-09-08. This packet closes the previous
Mac-only load-an-already-provisioned-key path. It does not claim a signed build,
actual Keychain mutation, successful CloudKit request or cross-device E2E.
Signing tooling, native UI and the only Chromium build/install remain Desktop-owned.

## Current API review — 19 September 2026

The user requested a primary-source review before any more speculative builds.
The current Swift direct-bootstrap source is7520228+d098e06. This review is not
runtime acceptance: Mobile37 still exposes an unclassified activation error;
its actual dynamic error type/domain/code must be established on that candidate.

### Supported approach and invariants

- Keep one persistent `CKSyncEngine` for normal private-database replication,
  save its state serialization, process actual send results and server conflicts,
  and distinguish sign-in from sign-out/account switching. Apple's
  [engine documentation](https://developer.apple.com/documentation/cloudkit/cksyncengine-5sie5)
  and [reference implementation](https://github.com/apple/sample-cloudkit-sync-engine/blob/main/SyncEngine/SyncedDatabase.swift)
  support this pattern. The sample's deliberate reset/data-loss shortcuts are
  not Ahoi's preservation policy and must not be copied.
- A bounded one-time bootstrap may use direct public CloudKit APIs. Explicitly
  verify/save the exact zone, scan from a nil change token through every page,
  and handle each item result. No callback/default value proves an empty zone.
  See [record-zone changes](https://developer.apple.com/documentation/cloudkit/ckdatabase/recordzonechanges(inzonewith:since:desiredkeys:resultslimit:)).
- Claim creation uses the exact record ID, `.ifServerRecordUnchanged` and an
  atomic operation; handle both thrown errors and per-item results. A conflict
  is an existing claim, never permission to overwrite it. Partial-error details
  must be read at their actual item boundary, retaining only safe diagnostic
  codes. See [modifyRecords](https://developer.apple.com/documentation/cloudkit/ckdatabase/modifyrecords(saving:deleting:savepolicy:atomically:))
  and [partialFailure](https://developer.apple.com/documentation/cloudkit/ckerror/code/partialfailure).
- Keep original authorization/account continuity and durable receipt/readback
  before key promotion. CloudKit and Keychain are separate systems; a server
  acknowledgement is not proof that the local journal or a peer's key exists.
- Canonical synchronizable keys must not use `ThisDeviceOnly`; local pending
  keys/journals do. Access-group authorization is separate from synchronization.
  Existing Ahoi attributes follow these rules. Do not replace the Keychain model
  without an evidenced defect. See [synchronizable items](https://developer.apple.com/documentation/security/ksecattrsynchronizable)
  and [data-protection Keychain](https://developer.apple.com/documentation/security/ksecusedataprotectionkeychain).

### Concrete unresolved activation seam

`AppEntry`'s `@MainActor` runtime factory synchronously calls
`KeychainRemoteCommandSigner.ensureIdentity()` before creating the payload Sync
runtime. Only `identityRevoked` is handled locally. Other signer errors can
therefore abort all Sync and fall into the generic setup error classification.
This is an observed source coupling, NOT yet the proven Mobile37 error.
Apple explicitly warns that [SecItemCopyMatching blocks its calling thread](https://developer.apple.com/documentation/security/secitemcopymatching(_:_:))
and should not block the main UI thread. Optional remote-control identity work
must not be mistaken for the required payload-key/CloudKit bootstrap. Diagnose
the actual error before changing this seam; do not weaken its signing/revocation
checks or simply swallow an error into a false Ready state.

### Simulator evidence boundary

An authenticated Simulator can exercise private Development CloudKit requests;
see [CKContainer](https://developer.apple.com/documentation/cloudkit/ckcontainer).
Do not infer that all Simulator push is unsupported from the sample README:
Apple's [Xcode14 release notes](https://developer.apple.com/de/documentation/xcode-release-notes/xcode-14-release-notes)
document Sandbox remote notifications on supported Apple-Silicon/T2 Macs. The
present M2 host meets that hardware premise, but actual registration/delivery
still requires evidence. No official source reviewed establishes a delivery
deadline or guaranteed macOS-to-Simulator iCloud-Keychain propagation. Local
Keychain readback, actual CloudKit traffic, peer key arrival and physical-device
acceptance remain distinct proofs. Do not invent an external hardware blocker
for the currently unidentified Mobile37 activation error.

## Same existing lifecycle, verified key identity

The existing private-zone control record remains `AhoiKeyBootstrapClaim` with
fixed name `payload-key-bootstrap-v1`. That name is an opaque stable identifier,
not a second active domain wire version. All domain records remain format3.
The control fields are now `keyVersion` (strict positive UInt32) and `keySHA256`
(64 lowercase hex characters). A key version alone cannot establish that a
canonical key is the one selected by the server; both clients now compare the
actual random 32-byte key's SHA256 commitment before allowing encrypted traffic.
The claim contains no key bytes, passwords, device credentials or profile paths.
`config/sync-format.json:keyBootstrapControl` is the common contract.

Older claims without that proof and existing canonical keys without a claim stay
in recovery, untouched. No automatic migration, replacement, key deletion or
assumption that `AhoiBrowserSyncV3` is empty. Acceptance must bind a genuinely
fresh isolated zone and key family on both signed candidates. Existing public
configuration is not evidence that those names have never been used.

## Native Mac start and concurrency

After explicit global opt-in, `CloudKitSyncKeyBootstrapMac` checks the account,
scans only the chosen zone's control metadata and checks the local key state.
An empty zone with no previously associated key may be created. It then generates
one device-local candidate, persists its journal and conditionally creates the
fixed claim using Apple's
[ifServerRecordUnchanged policy](https://developer.apple.com/documentation/cloudkit/ckmodifyrecordsoperation/recordsavepolicy/ifserverrecordunchanged).
It never updates or overwrites a competing claim. Losing candidates do not reach
the synchronizable Keychain. Indeterminate ownership stays recovery, not success.

Promotion requires the locally persisted accepted server tag, a new complete
control scan and matching actual candidate hash, plus account readback and the
original authorization. The loaded cryptor also checks its actual loaded bytes'
commitment, rather than trusting a preceding Keychain read. Joining peers wait
for the matching canonical key to arrive via Apple's synchronizable Keychain.
Existing sync cadence may check local key arrival; it does not repeatedly scan
CloudKit or recreate a failed claim. An explicit Retry rechecks setup without
granting account/zone recovery or replacing key material.

A process-local registry plus an OS file lock serialize the same Keychain family
across profiles/processes on one Mac for the entire asynchronous claim workflow.
Only a hash of public configuration appears in the local cache lock filename.
The OS releases the lock on a crash; no polling process, lock TTL or second sync
database. Keychain primitives recheck original authorization immediately before
each mutating Security call. Canonical items are Add-only and read back with a
constant-time comparison; there is no canonical Update/Delete route here.

The short control operations finish before creating the existing domain
CKSyncEngine. There is no second running domain engine or copied profile.
The bootstrap account monitor remains alive and permanently revokes its original
key authorization on an account change. The provider separately retains its
account/recovery observer so a legitimate confirmation cannot revive an old lease.

## Swift parity and cancellation

Swift uses the same key-item accounts, accessibility, non-synchronizable pending
journal and matching server commitment. Its control snapshot now uses a fresh
non-overlapping bootstrap engine: reusing an incremental token after clearing the
snapshot could otherwise falsely report an empty zone. Each activation gets its
own permanently revocable lease, captured explicitly by the factory, transport,
coordinator and Keychain store. Off/on cannot revive a delayed old key write.
The existing domain-provider cancellation/merge-drain gates remain in place.

Both providers recognize the verified control record instead of importing it as
a domain UUID/quarantine row. A changed/deleted key claim blocks domain traffic
and token advancement. Unrelated zones are filtered; Native fetch scope is now
explicitly restricted to the configured zone. This is important for isolated
development collections sharing the same private database.

## Native UI handoff and acceptance

`SyncTransportStatus.key_setup_issue` provides fixed, non-secret local status
codes (`key_setup_in_progress`, `key_setup_waiting_for_key`, `key_setup_busy`,
or concrete recovery/unavailable reasons). The empty value means no current key
setup issue; it is NOT itself a transport pass. `ProfileSyncService::RetrySyncKeySetup`
is the explicit retry entry; account/zone confirmation remains separate.
Desktop should expose these in its existing Sync settings/status, not a new wizard.

Required next result: build matching candidates, perform the short visible
install/link/shared-data journey on the real entitled devices, then run the
minimal relevant key/consent/claim regressions. No old Build18/19, Swift relay,
provisioning profile, conditional source test or successful signer substitutes
for that result. Existing test constructors are adapted for commitment metadata;
the obsolete automatic key-migration expectation now checks preservation/refusal.
No tests have run for this packet, and no earlier failure is relabelled green.

## Prepared isolated Development configuration

The non-secret tuple in
[shared-sync-development-scope-20260908.json](../artifacts/e2e/shared-sync-development-scope-20260908.json)
provides one fresh zone and separate payload-key account for the matching-device
journey. It is prepared, NOT applied or evidence of a cloud record/key creation.
The dedicated container, signing identity/access groups, service and Development
environment remain unchanged. Desktop retains native configuration/sign/install.

Mobile now stamps and requires `AHOI_CLOUDKIT_ZONE_NAME` and passes that SAME
value to key bootstrap, the normal provider and the existing rotation lane.
No hardcoded default can send bootstrap and domain records to different zones.
The public configuration's ordinary default remains AhoiBrowserSyncV3; existing
profiles/keys are not rewritten. Missing/unresolved app configuration stays
local-only rather than silently selecting a different zone.

`xcodebuild -showBuildSettings` for CloudKitDevelopment/iphoneos, with signing
explicitly disabled, resolved the prepared zone/account and exact existing
container/group/service/version/Development values on both app and Core targets.
Project generation, plist syntax and scoped diff checks passed. No compile,
signing, simulator, key or cloud action occurred for this configuration followup.
The previously built20 candidate remains immutable and does not yet contain
this later configurable-zone source.

The matching followup also stamps and requires
`AHOI_CLOUDKIT_SUBSCRIPTION_ID`. AppEntry passes the same explicit ID to the
sequential bootstrap, normal provider and rotation lane; account recovery
already reuses the provider configuration. This closes the previous gap where
the prepared Native subscription was explicit but Mobile still selected the
engine default. The ordinary public default matches Native's existing
`AhoiBrowserSyncSubscription`; the acceptance pair overrides it with the exact
subscription in the same prepared JSON. No new subscription was created by
this source change. Build19 remains the separate unchanged visible-UI candidate,
and Build20 does not contain these later configuration changes.
The configuration-only readback now resolves all seven prepared identifiers,
including that subscription, identically for app and Core with signing disabled.
Project generation/plist/whitespace checks passed; no compile or runtime pass.

The first unsigned iOS-device build48954 on77061ad reached the real Mobile
preflight and failed: it still required the ordinary `payload-key` account.
That original EXIT65/log/XCResult is retained under
artifacts/build/mobile-development-77061ad-20260908/. No successful device
candidate, signature or runtime is claimed for that run.

The corrected preflight accepts `AHOI_SYNC_ACCEPTANCE_SCOPE_ID` only in
CloudKitDevelopment, as a canonical lowercase UUIDv4 with an exact source SHA.
Zone, subscription and isolated payload-key account must all bind that SAME
scope. Team/container/services/access groups/key version and entitlement checks
remain exact. Ordinary builds without a scope require the public tuple;
provider-free and distribution modes reject the scope. This is a local build
input, not a wire field, a key claim or permission to access existing keys.
Three focused tests of the real preflight CLI passed (4.453s): the bound
Development tuple, invalid/mixed scopes and the existing ordinary mode contract.
These are configuration-gate checks, not an app or CloudKit acceptance.
