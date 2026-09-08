# Native extension setup integration

ADR0010 implementation packet, 2026-09-08. Common/Swift ownership stays with
the unified Sync owner; native install/enable/uninstall/permission flows and
StorageFrontend upstream patch stay with Desktop. No app/build/permission or
Production action is authorized by this source handoff.

## Existing format3, separate desired configuration

Use the EXISTING PermittedSetting wire class for a typed browser-setup setting:
`ahoi.extension.<32-character-extension-id>.desired`, carrying exactly
`{"enabled":bool,"installed":bool,"source":"chromeWebStore"|"pinnedUblockClassic"}`.
This is not a device-specific ExtensionInventory record. The whole desired tuple
is the existing atomic value_json field group; setting_id and tombstone groups
are unchanged. The record ID is the same code-owned setting-ID derivation used
by native preferences. No new database, engine, entity discriminator, wire
version or parallel writer is needed for this browser configuration.

An explicit uninstall is installed=false/enabled=false, retained as a versioned
desired state. Absence on a fresh machine, a partial inventory, shutdown and
loss of a window are NOT uninstall intent. installed=false/enabled=true and
configuration tombstones are rejected. Source is a routing enum, never a URL,
CRX, extension signing key, profile path or permission grant. The pinned Classic
source only admits the actual release identity fkgkibajhfbepljeaefdnfnegdcjomkh;
its historical Web Store identity is not silently routed through normal CWS.
Native must still apply current source/signature/permission/managed/MV2 policy.

The typed C++ codec is extension_setup_setting.{h,cc}; Swift counterpart is
CompanionExtensionSetup.swift. Both preserve the standard3 exact clock contract.
The ordinary browser-preference catalogue does not grant this namespace.
The new native setup path has separate local `ahoi.sync.extension_setup.enabled`
approval, default false. It adds only positively validated known record IDs to
the existing final provider permission source; generic preference UI enablement
cannot enroll extension installation. Initial capture waits for actual first
fetch and a complete native inventory. It only seeds present eligible entries;
an absent or partial inventory never authors uninstall.

Common now stores explicit user intents using the existing canonical local
preference-intent queue and original HLC; replay does not restamp them. The
original provider/account/category lease guards their existing SQLite+outbox
commit. Native restoration then consumes per-record leases: unrelated tab
updates do not invalidate an extension download, while a change to THAT desired
record does. These are source implementations, not a successful product build.

## Callable native seam

The UI-free extension_setup_types target depends only on base and existing local
authorization. ProfileSyncUiBridge exposes default-closed methods:

- ReadNativeExtensionSetup() returns an explicit complete/deferred inventory of
  eligible ordinary-profile trusted extensions. Component, unpacked, policy and
  unrecognized external-source entries are not restore-authoring candidates.
  Its separate installed_extension_ids MUST contain ALL actual installed IDs,
  including excluded entries. Absence from the eligible subset alone cannot
  prove successful uninstall after an extension became managed. Both lists
  are local readback, not new wire properties.
- ApplyNativeExtensionSetup(request,completion) carries a unique operation ID,
  opaque shared revision, desired tuple and the ORIGINAL authorization lease.
  Neither a late callback nor a new approval renews that lease. The native owner
  checks it before actions, asynchronous installer continuation and activation.
  user_initiated is false on background restoration: report NeedsConfirmation
  instead of stealing focus for a prompt. True comes only from explicit Retry.
- Completion echoes exact operation/revision and reports Applied, Pending,
  NeedsConfirmation, BlockedByPolicy, Unsupported, Failed or Cancelled. Applied
  means native readback really matches, not a submitted download or a visible
  permission sheet. There is no automatic permission confirmation.

Desktop implements this in its existing SessionBridge/native extension helper
scope. Do not use the Google-account install-approval/Sync processor as a shortcut.
Common's NativeExtensionSetupController now retains exact revision/operation
identity, permanently cancels old leases and checks complete native readback.
Identical refreshes do not restart downloads/prompts. Following a Pending or
NeedsConfirmation completion, passive refresh performs only a readback and can
recognize a subsequently completed native install. Same-revision action retries
require explicit local input. No global asynchronous "applying" flag is used.

Concrete additional ProfileSyncService entry points for Desktop's accepted scope:

- extension_setup_sync_enabled()/SetExtensionSetupSyncEnabled(bool): explicit
  category UI, separate from browser-preference consent and native permissions.
- PublishNativeExtensionUserIntent(desired): Native calls when a genuine local
  user install/enable/disable/uninstall choice is accepted. Not for an automatic
  update, inventory load, policy refresh, our own apply, or delayed completion
  of an already superseded user choice. Duplicate unchanged values are coalesced.
- NotifyNativeExtensionSetupReady(): native loading/completion event requests
  fresh readback, never a missing-entry deletion.
- RetryExtensionSetup(extension_id) and extension_setup_results(): explicit
  action plus honest Native result for the existing compact Settings surface.

Canonical sync_wire_v3.json now includes two typed setup examples (28 total,
still13 carrier classes). SHA256:
645d4f3559e7eb3360189a35f891b7588ec69ef39f83cb8832ca4048204c006b.
The existing C++/Swift golden consumers point at this same resource and check
the typed roundtrip in their existing loops. No new suite/matrix or execution.

Native storage remains authoritative for actual installation and enabled state.
The first positively reviewed per-extension/key/value StorageFrontend adapter
is now specified and implemented in Common/Swift in
[EXTENSION_SETTINGS_SYNC.md](EXTENSION_SETTINGS_SYNC.md). Its Native hooks and
runtime acceptance remain open; it is not a raw storage copy or an expansion
of this installation category's consent.
iOS now has a separate explicit read-only metadata toggle/section. Its epoch is
installed synchronously on the provider before actor hops, hydrated cached
records must validate the exact typed tuple/identity/clock envelope, and only
actually accepted typed records can resolve a prior quarantine. Unknown values
are not outgoing seeds/merge echoes. iOS cannot install Chromium extensions or
publish extension desired state through this read-only approval. Existing
domain-merge activity/cancellation boundaries are retained.

These seams do not claim the required two-Mac setup or storage
roundtrip. Representative real installation/confirmation/restart comes before
focused programmatic checks, using the single coordinated native candidate.
