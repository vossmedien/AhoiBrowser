# Native Bookmark Sync source handoff — 2026-09-05

Status: **SOURCE ONLY / NOT BUILT / NOT E2E-ACCEPTED**.

Owner: `01a06d69-1034-7372-b784-0b05a53c87e0`. Canonical branch:
`codex/desktop-core-feature-wave-20260830`. Desktop owns the single combined
checkout/build/install/UI path; Mobile owns Swift. This manifest authorizes only
integration of the committed source into the guarded combined candidate, not
Production, release, global-v3 writes, a second runtime, or default-profile edits.

## Source scope and review

- Fixed Bookmark Entity11/wire2, six exact field groups, lossless positive Windows
  creation metadata, one canonical UUID golden; schema4-to5 copies existing
  record bytes and preserves outbox, tombstones and watermarks.
- Native BookmarkModel/MergedSurfaceService adapter, local/account-qualified
  identity, same-store durable journal and pre-apply receipts. Observer work is
  coalesced; native apply is guarded and never navigates or focuses a tab.
- Recovery source covers async native-save crash windows, receipt cloning and
  delayed ACKs, live edit reversals, move+delete, explicit Undo after tombstone/
  watermark, account detachment and missing-parent/tombstone safety.
- Separate explicit/default-off local category approval. Existing global Sync,
  provider/account/keys remain prerequisites; transport consent gates include
  opaque inbox hydration, outbox/recovery and account revocation.
- Local invalid metadata pauses Bookmark reconciliation, with no credentials in
  a backend snapshot and no native sanitization. A failed journal write cannot
  authorize applying an older projection over the unsaved local edit.
- Independent read-only M152 API/lifetime review found a real double-owner
  BubbleDialogModelHost fault. The Widget is now the sole owner. The popup
  distinguishes approval from transport readiness and displays local issues.

No source review, format check or fixture syntax check counts as an executed
product test. The installed plist readback is still `3d413efb5b6f196403e92f51631c346c9c55b2e5`;
this new source is not present there.

Preparation readback: owned C++/Objective-C++ sources use the pinned Chromium
clang-format and style file; the largest touched common module is 775 lines.
Owned diff whitespace and policy/golden JSON syntax were checked. Sync GN is
format-clean. Sidebar GN parses but asks to reorder two pre-existing Desktop
motion entries; those unrelated hunks were deliberately left exactly as owned
by Desktop. These are source preparation checks, not build/test acceptance.

## Required candidate-bound verification

Build the one combined candidate through `scripts/build-ahoi.sh dev`, preserving
Desktop's source/CPU/provenance gates. Include `ahoi_sync_unittests` and
`ahoi_sidebar_tree_unittests`. Do not execute test binaries as part of the build.

First visibly exercise native Bookmark create/folder/nested open/context action,
horizontal overflow/focus, consent open/cancel/approve/stop and restart on an
isolated profile. Cmd+D saves to Ahoi Tree and is not this native bookmark action.
After a larger correction repeat the affected visible journey. If Computer Use
fails for a fresh technical reason, document that exact boundary and only then
run meaningful independent tests without claiming E2E.

New sync source has 95 cases across:
`BookmarkSyncModelTest`, `BookmarkSyncSerializationTest`,
`BookmarkSyncStoreTest`, `BookmarkSyncBridgeTypesTest`,
the journal/recovery fixtures, `NativeBookmarkSyncAdapterTest`,
Bookmark consent/provider and profile consent fixtures. Discover exact names
from the built binary rather than treating this prose as a launcher filter.
Run these and affected existing wire-v2/SyncPump/store/merge/profile/provider
regressions. Three new Views/TestingProfile popup cases cover Cancel/reopen,
owner teardown with an open popup and category-only explicit approval. These
and changed shelf viewport tests belong to
`ahoi_sidebar_tree_unittests`. Results must bind the exact source and binary.

Historical shelf 11/11 on `3d413ef` was executed under the earlier documented
native-pipe exception, not visible acceptance. It does not cover this source
or later viewport assertions. Matching Mobile `313e351` Build15 has its own
visible and focused evidence, not a native Chromium/Mobile roundtrip.

## Remaining acceptance risks and external boundaries

- A rare GUID-changing cross-storage move followed by a crash before the alias
  ledger write remains ambiguous against a genuine imported clone. Ordinary
  observed moves and move+delete have regression source; do not claim exhaustive
  identity/crash proof without the exact native persistence experiment.
- Durable apply receipt history is intentionally retained; bounded retention/
  garbage collection must not erase a receipt still reachable from native disk.
  Identical record intents reuse a receipt, but changed history can grow.
- Partial native apply, delayed native save, account/key changes, merge conflicts,
  deep/large collections and actual native restart still need executed proof.
- Invalid native metadata currently pauses this whole category's reconciliation;
  it does not selectively merge the remaining entries or claim their success.
- The installed `3d413ef` Mac candidate is provider-free. Matching real CloudKit
  configuration/provisioning/key bootstrap must be established on a future exact
  Mac candidate for cross-client roundtrip; two Swift peers are not a substitute.
- The shared-tab v3 contract in `09cae9f` is a separate future wave. Global writers
  remain2; Bookmark authoring remains2. No v3 feature is enabled by this package.
- Chromium .83 roll and Desktop folder-motion/rendering/Arc acceptance retain
  their separate owner, resource and visible-runtime gates.

## Exact owned integration files

Common sync paths below are relative to
`overlay/chromium/src/ahoi/browser/sync/`:

```text
bookmark_profile_consent_unittest.cc
bookmark_sync_bridge_types.cc
bookmark_sync_bridge_types.h
bookmark_sync_bridge_types_unittest.cc
bookmark_sync_consent_unittest.cc
bookmark_sync_journal.cc
bookmark_sync_journal.h
bookmark_sync_journal_receipts.cc
bookmark_sync_journal_unittest.cc
bookmark_sync_model_unittest.cc
bookmark_sync_recovery_unittest.cc
bookmark_sync_serialization.cc
bookmark_sync_serialization.h
bookmark_sync_serialization_unittest.cc
bookmark_sync_store_unittest.cc
cloudkit_bookmark_sync_consent_unittest.mm
cloudkit_sync_provider_mac_consent.mm
cloudkit_sync_provider_mac_persistence.mm
native_bookmark_sync_adapter.cc
native_bookmark_sync_adapter.h
native_bookmark_sync_adapter_unittest.cc
native_bookmark_sync_apply.cc
profile_sync_backend_bookmarks.cc
profile_sync_service_bookmarks.cc
sync_serialization_internal.cc
sync_serialization_internal.h
BUILD.gn
cloudkit_sync_provider_mac.h
cloudkit_sync_provider_mac.mm
cloudkit_sync_provider_mac_internal.h
cloudkit_sync_util_mac.mm
profile_sync_backend.cc
profile_sync_backend.h
profile_sync_prefs.cc
profile_sync_prefs.h
profile_sync_service.cc
profile_sync_service.h
profile_sync_service_factory.cc
sync_field_merge.cc
sync_merge.cc
sync_merge.h
sync_model.cc
sync_model.h
sync_provider.cc
sync_provider.h
sync_pump.cc
sync_pump.h
sync_serialization.cc
sync_store.cc
sync_store.h
sync_store_schema.cc
```

Additional owned source:
`config/sync-policy.json`;
`overlay/chromium/src/ahoi/browser/ui/sidebar/sidebar_bookmark_shelf_view.cc`;
`sidebar_bookmark_sync_control.{h,cc}` and its focused new test file in the same
directory. In the shared sidebar `BUILD.gn`, only Bookmark-control source/test
entries and direct preference-target dependencies belong to this package;
Desktop's motion entries in `ef0f965` are retained.

Documentation: ADR0006, `ACTIVE_BOOKMARKS_CHECKPOINT.md` and this handoff.
Golden fixtures remain their already-versioned canonical files.
No Mobile files, native Tree/Session/`tab_tree_sync_adapter`, Desktop Arc/motion/
appearance/shell edits, patches, user `AGENTS.md` or unrelated evidence are staged
by this handoff.
