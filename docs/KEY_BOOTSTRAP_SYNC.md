# Shared first-use key setup

Common C++ and matching Swift source, 2026-09-08. This packet closes the previous
Mac-only load-an-already-provisioned-key path. It does not claim a signed build,
actual Keychain mutation, successful CloudKit request or cross-device E2E.
Signing tooling, native UI and the only Chromium build/install remain Desktop-owned.

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
