# Reviewed extension-owned settings

ADR0010 implementation, 2026-09-08. Common C++ and matching Swift are owned by
the unified Sync owner. Native SessionBridge/StorageFrontend and the only
Chromium build/install path remain Desktop-owned. Source is not runtime proof.

## First useful positive catalogue

Vimium, trusted Chrome Web Store identity `dbepggeogbaibhgnhhndojpepiihcmeb`,
installed native version **2.4.2**: `smoothScroll`, `filterLinkHints`, `hideHud`,
`hideUpdateNotifications`. Values are actual Booleans, not numeric/string
coercions. Default values are respectively true/false/false/false.
Native compares the locally installed version with the code-owned descriptor;
a peer cannot claim compatibility or supply a download URL/package. Unreviewed
versions remain Unsupported until their actual source is checked.

The [versioned upstream settings implementation](https://github.com/philc/vimium/blob/v2.4.2/lib/settings.js)
stores these individual keys in `chrome.storage.sync`, removes default-valued
keys and reloads settings on its normal storage change event. No forced extension
reload or active-page navigation is part of this adapter. A native store callback
alone does not prove that a visible extension view has reacted.

All other extension keys remain local, including custom key bindings, CSS,
site/exclusion lists, global marks, URLs/search configuration and opaque values.
No raw local/session/managed storage, extension IndexedDB, whole-store export,
Google Sync processor, or secret-denylist fallback. uBO's local-storage settings
and Dark Reader's cached settings are not silently labelled live-sync compatible.
This is a concrete first catalogue, not a claim to cover every extension setting.

## Same format and explicit consent

Use the existing format3 PermittedSetting carrier and stable native setting ID:
`ahoi.extension.<catalogue-id>.storage.sync.<catalogue-key>`.
`value_json` is exactly a Boolean or JSON null for an explicit keyed Remove.
No record tombstone, new entity class, new database, parallel writer or migration.
The existing atomic value group and exact C++/Swift field clocks remain intact.
The canonical fixture now includes Boolean/reset examples: 30 examples, 13 carrier
classes, SHA256 `18d3a0e5140359ecc6a768681029ec01b09cf4508e11c61552f050795ac7934d`.
Both existing language consumers reference this same resource; not yet executed.

Desktop approval `ahoi.sync.extension_settings.enabled` defaults false and is
separate from extension installation approval and ordinary browser preferences.
It grants only the finite catalogue's record IDs through the existing SQL,
outbox, provider, rehydration and final-upload authorization. Original account,
category and per-record leases remain revoked after off/on or a newer revision.
iOS has a separate default-off read-only metadata approval and section. It does
not author/seed/echo these settings or execute an extension. Cached encrypted
records are preserved and can be hydrated after explicit approval.

## Concrete Desktop-owned wiring

`ProfileSyncUiBridge` now has default-closed out-of-line methods and UI-free
`:extension_storage_types` DTOs. No native implementation was edited here.

- `ReadNativeExtensionSettings(original_authorization, completion)` reads ONLY
  catalogue keys through the existing native sync-area storage. A complete
  capture includes every supported key of each eligible extension; absent keys
  have value=nullopt. An ineligible/missing extension contributes no entries.
  Deferred reads never imply resets. First seeding waits for initial fetch,
  contributes only actually present values and checks absence again on the
  existing backend sequence, so a peer arriving during capture is not overwritten.
- `ApplyNativeExtensionSetting(request, completion)` carries the exact key/value,
  operation UUID, opaque shared revision and original authorization. Native must
  verify current trusted source, reviewed installed version, profile/policy and
  sole native storage ownership before keyed Set/Remove. Preserve all other keys.
  Check original authority on the actual ordered storage path immediately before
  commit, and return kStored only after successful write and matching keyed
  readback with the same operation/revision. No prompt/reload/global async guard.
- The Desktop-owned `storage_frontend.{h,cc}` patch reports actual extension
  writes before its no-JS-listener early return. Carry our apply origin/operation
  across async storage and event dispatch; preserve normal extension JS events.
  Our own apply must NOT call `PublishNativeExtensionStorageChange(value)`.
  Other actual reviewed-extension sync-area changes do call that Service method;
  a real removed key is a reset, a fresh empty capture/uninstall is not.
- `NotifyNativeExtensionSettingsReady()` retries only on a real installation,
  storage/policy readiness transition or explicit user Retry. Do not emit it for
  every storage callback/our own completion: that would create an apply loop.
  `extension_settings_results()` exposes per-key Deferred/Unsupported/Blocked/
  Failed/Cancelled or actual Stored status, never inferred remote UI success.

`NativeExtensionStorageController` cancels only the superseded key, deduplicates
identical refreshes and verifies the native result's exact authorized readback.
Session/bridge detach cancels pending work; unrelated local edits stay visible.
No callback can renew an old authority or silently replay a cancelled write.

## Remaining acceptance

Native hooks must be implemented against this committed API and included in the
single coordinated candidate. Then change a supported setting in Vimium's real
options on isolated Mac A; observe Mac B while running, reset and restart, and
check iOS's recognized metadata. Keep unknown storage and permission/account
boundaries intact. Visible E2E first, then minimal relevant regression checks.
Build19 and all older accepted journeys remain unchanged and cannot prove this
later source. No test matrix, native install, key/Portal or Production action
was taken for this packet.
