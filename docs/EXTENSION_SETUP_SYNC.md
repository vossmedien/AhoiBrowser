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
These source codecs are not activated merely by being in the build: the ordinary
browser-setting catalogue does not admit the extension namespace. Its separate
explicit setup-category consent, snapshot fetch/intent orchestration and final
provider authorization must be connected before the writer is enabled.

## Callable native seam

The UI-free extension_setup_types target depends only on base and existing local
authorization. ProfileSyncUiBridge exposes default-closed methods:

- ReadNativeExtensionSetup() returns an explicit complete/deferred inventory of
  eligible ordinary-profile trusted extensions. Component, unpacked, policy and
  unrecognized external-source entries are not restore-authoring candidates.
- ApplyNativeExtensionSetup(request,completion) carries a unique operation ID,
  opaque shared revision, desired tuple and the ORIGINAL authorization lease.
  Neither a late callback nor a new approval renews that lease. The native owner
  checks it before actions, asynchronous installer continuation and activation.
- Completion echoes exact operation/revision and reports Applied, Pending,
  NeedsConfirmation, BlockedByPolicy, Unsupported, Failed or Cancelled. Applied
  means native readback really matches, not a submitted download or a visible
  permission sheet. There is no automatic permission confirmation.

Desktop implements this in its existing SessionBridge/native extension helper
scope. Do not use the Google-account install-approval/Sync processor as a shortcut.
Common will retain original user intents and origin-suppress its own native
applies per operation; it must never hide a genuine later user uninstall/disable
behind a global asynchronous "applying" flag. Complete capture alone cannot
turn a remote or automatic install/enable into new user intent.

Native storage remains authoritative for actual installation and enabled state.
ExtensionSetting transfer is a later positively reviewed per-extension/key/value
adapter through StorageFrontend, not a raw Preferences or extension-storage copy.
iOS only preserves/shows the typed setup metadata; it cannot install Chromium
extensions. These seams do not claim the required two-Mac setup or storage
roundtrip. Representative real installation/confirmation/restart comes before
focused programmatic checks, using the single coordinated native candidate.
