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

## Concrete native storage consumer

Common now owns `NativeExtensionStorageAdapter` and its small storage-sequence
I/O functions. They use the real profile StorageFrontend and the existing
`:extension_storage_types` DTOs/controller. The unused storage methods on
ProfileSyncUiBridge are removed: this API needs no window or SessionBridge.
Native installation/enable/permission UI remains Desktop-owned through the
separate extension-setup bridge.

The observer starts as soon as global/category opt-in is active, even while
backend/key setup is pending. Bridge attach/detach cancels only the UI-bound
install/enable controller, not the profile-wide storage observer or native
preference work. Thus a still-running extension can publish a genuine local
change without an open browser window. Global/category revocation and profile
shutdown still cancel and tear down storage work synchronously.

- `NativeExtensionStorageAdapter::Read(original_authorization, completion)` reads ONLY
  catalogue keys through the existing native sync-area storage. A complete
  capture includes every supported key of each eligible extension; absent keys
  have value=nullopt. An ineligible/missing extension contributes no entries.
  Deferred reads never imply resets. First seeding waits for initial fetch,
  contributes only actually present values and checks absence again on the
  existing backend sequence, so a peer arriving during capture is not overwritten.
- `NativeExtensionStorageAdapter::Apply(request, completion)` carries the exact key/value,
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
- The additional `ObserveSyncSettingsWriteRequests` Native hook must invalidate
  the extension's epoch on UI BEFORE a native sync-area Set/Remove/Clear is
  queued. Completion-only observation can revoke an already queued remote write
  too late, including an A->X->A local sequence invisible to value comparison.
  Common first reads on the same Storage backend and returns to UI before its
  mutation; earlier native commits can publish their true local intents, and
  later native submissions revoke the original epoch. No global suppression,
  pending-counter framework or wire field. This precise two-file followup was
  requested in01a08182-b3b0-7a41-b45e-ba352ccca250; it must be committed before
  this consumer is declared build-ready.
- `NotifyNativeExtensionSettingsReady()` retries only on a real installation,
  storage/policy readiness transition or explicit user Retry. Do not emit it for
  every storage callback/our own completion: that would create an apply loop.
  `extension_settings_results()` exposes per-key Deferred/Unsupported/Blocked/
  Failed/Cancelled or actual Stored status, never inferred remote UI success.

`NativeExtensionStorageController` cancels only the superseded key, deduplicates
identical refreshes and verifies the native result's exact authorized readback.
Profile/category cancellation revokes pending storage work; unrelated local edits stay visible.
No callback can renew an old authority or silently replay a cancelled write.

The adapter checks actual CWS source/ID, reviewed version, manifest3/storage
permission and current native management policy. Registry, permission and
policy changes revoke existing epochs; related keyed services outlive this
consumer. Missing installation/loading is Deferred, policy refusal is Blocked,
and an unsupported version/schema remains Unsupported. Google's competing
writer is excluded by the EXISTING mandatory `--disable-sync` startup policy,
checked again at storage entry; no Google Sync service is created or changed.
Safe actual WriteResult changes reach the current same-extension JS listener
with remote origin even if a later readback fails. Such a result is not falsely
labelled Stored, and no replaced extension is notified.

## Remaining acceptance

This is a Common source freeze for coordinated integration, NOT a standalone
build-ready HEAD: it calls the still-pending Native entry hook. Required delta
after851e3bb/0036 is committed as f36e4bd at
artifacts/build/native-extension-storage-consumer-20260908/native-write-request-hook.proposed.patch
(SHA9f8ef3ca5fbb23970e41b9feec0afd1697a85d5204568f59d8171c2e30dcfecf).
Desktop may combine these exact sources with that owned dependency in one
candidate; the current frozen checkout/out must not be refreshed with only one
half. The explicit proposal to take over that bounded Native change remains
01a081d0-fc37-7fc2-a9c4-8ed85164c08f, not an inferred ownership transfer.

The pre-write request hook above is the remaining source dependency. Native
observer851e3bb and this consumer must enter one coordinated candidate. Then
change a supported setting in Vimium's real
options on isolated Mac A; observe Mac B while running, reset and restart, and
check iOS's recognized metadata. Keep unknown storage and permission/account
boundaries intact. Visible E2E first, then minimal relevant regression checks.
Build19 and all older accepted journeys remain unchanged and cannot prove this
later source. No test matrix, native install, key/Portal or Production action
was taken for this packet.
