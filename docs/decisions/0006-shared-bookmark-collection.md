# ADR 0006: One logical bookmark collection, native platform adapters

2026-09-05 user override: [ADR 0009](0009-unified-prelaunch-sync-format.md)
requires one current format for all data, including Bookmarks, with no legacy
migration project. The separate collection, native authority, identity, privacy
and consent rules below remain; permanent wire-v2 authoring is superseded.

Status: implementation direction and bookmark wire-v2 fields frozen by the
bookmark owner on 2026-09-05. Implementation, native identity reconciliation
and platform acceptance remain open.

## Product decision

The user explicitly chose the same bookmark collection on Desktop and Mobile,
after being told that additional sync work is required. Workspace saved pages,
temporary tabs and bookmarks remain different entities. The existing Mobile
Library is not proof of this feature and will not be silently reclassified.

## Reuse and boundary

Keep Chromium's `BookmarkModel` authoritative for native Desktop bookmarks.
Mobile uses the existing local-first domain repository and native WebKit URL
opening. Add a typed bookmark domain to the existing C++/Objective-C++ and Swift
sync paths; do not build another transport, crypto provider or account service.

Both existing CloudKit codecs use `AhoiSyncRecord` and a `dataClass` string plus
an encrypted payload. A new bookmark discriminator does not itself require a new
CloudKit record type or public fields. This is not evidence that a Production
container/schema is deployed or that a real cross-device roundtrip has passed.

Introduce a distinct bookmark entity/discriminator, not a `treeNode` subtype.
The Desktop local `sync_records` table currently constrains entity IDs to 0–10,
so extending the model needs an explicit transactional schema migration. The
existing clock, field merge, encrypted envelope, quarantine, outbox and durable
deletion-watermark machinery must remain the shared implementation.

## Required domain contract

- One stable logical identity per bookmark/folder; native numeric Node IDs are
  never wire identity. Native GUID alone is insufficient across Local/Account
  storage. Keep a durable, profile-scoped identity mapping and migrate it when
  native storage/GUID changes, without duplicating the logical bookmark.
- Preserve native permanent root meaning, folder versus URL identity, title,
  URL, parent relationship and deterministic sibling order. Workspace IDs are
  not bookmark parents. A location move is one atomic field group.
- The final field names, enum values and clock groups must have shared golden
  payloads consumed by both C++ and Swift before the implementations diverge.
  Existing wire-record semantics must not be changed by an enum addition.
- Initial seeding is idempotent and waits for the native model to load. An
  incomplete snapshot, a missing key/account or a model-load failure is never
  evidence of deletion. Existing local entries are not replaced wholesale.
- Capture descendants before native subtree deletion and distinguish explicit
  removal from Account-root teardown. Deletion remains durable through offline
  replay, compaction and restart. A stale remote record must not resurrect it.
- Preserve the merged surface's visible Local/Account order, not merely each
  native storage's internal child order. A moved parent's descendants follow
  stable identity; cycles and unresolved/invalid parents require safe handling.
- Managed roots are not exported as an editable user collection. Private/Guest
  browsing never creates a second adapter or transport record. Profile isolation,
  current opt-in defaults, account/key boundaries and the sync denylist remain
  intact; no silent broadening of a prior consent is allowed.
- Use existing URL safety rules. A URL unsupported by Mobile must not trigger
  unsafe alternate navigation or be silently erased from Desktop bookmarks.

## Frozen bookmark wire-v2 contract

Use `EntityType::kBookmark = 11` / `SyncDataClass.bookmark = "bookmark"`, the
existing wire-v2 common metadata, and local Desktop store schema 5. Exactly one
of `root_kind` and `parent_id` is present; a present null is invalid.

| Payload field | Type / meaning |
| --- | --- |
| `kind` | integer: folder `0`, URL `1`; immutable for an identity |
| `root_kind` | top-level only; integer: bookmark bar `0`, other `1`, mobile `2` |
| `parent_id` | nested entries only; lowercase UUID of a bookmark folder |
| `sort_key` | nonempty visible ASCII, at most 1024 bytes; lexical byte ordering |
| `title` | UTF-8, at most 65536 bytes; empty is valid; no NUL |
| `url` | empty for folders; valid native URL metadata for URL entries, at most 131072 UTF-8 bytes, no NUL or embedded user/password |
| `created_at` | existing Windows-epoch microsecond decimal-string encoding; positive, correctable native metadata |

The exact field-clock set is `location`, `kind`, `title`, `url`, `created_at`,
`tombstone`. `location` copies/compares `root_kind`, `parent_id` and `sort_key`
atomically. Only `kind` is immutable. Older workspace/entity semantics remain
unchanged. Non-web native URLs remain metadata, not automatic navigation;
Mobile must surface unsupported activation without deleting the record.

`created_at` is a positive signed-Int64 count of Windows-epoch microseconds,
not an HLC or an unsigned Unix timestamp. Keep it as lossless raw metadata in
Swift, including valid pre-1970 values. HLC record/field timestamps have their
own cross-client bounds; do not apply those bounds to bookmark creation time.

Known parent links must lead to folders and cannot cycle. A missing parent may
arrive on a later provider page; retain the detached record but never materialize
it natively until its live ancestry is available. Do not persist a redundant
root on every descendant. Graph traversal must not depend on recursion depth.

Golden payloads: `overlay/chromium/src/ahoi/browser/sync/testdata/bookmark_wire_v2.json`.
Frozen fixture SHA-256:
`b09a5f898a07351f4cd80a68521dffadb05e21abb9799c0d86d61672d244e443`.
Both language test paths must consume the same fixture; the fixture itself is
not a passing codec, transport or UI result. The new native adapter still needs
reconstructible first-bind/move/clone handling across its two persistence owners.

## Native adapter constraints, verified against M152

`BookmarkModelObserver` provides load/change/pre-remove hooks, but a folder
deletion reports its root and bulk removal does not report individual deletes.
`BookmarkModel::AddFolder`/`AddURL` accept explicit GUIDs for idempotent applies;
copy/clone operations are not replication because they create new GUIDs.

`BookmarkMergedSurfaceService::Move` can change only merged ordering and issue
its own notification. It also translates Account-root removal into child-removal
notifications. Observe both native lifecycle and merged presentation where
needed; never turn Account detachment into remote user-deletion tombstones.

Observer callbacks copy/coalesce immutable values and return. Remote applies
run later on the native model sequence behind an explicit origin guard. Do not
mutate synchronously during a merged Move notification, and do not use
`added_by_user` as a feedback guard (`AddFolder` reports false). Extensive-change
notifications are batching, not a transaction. Resolve GUID/storage collisions
before native APIs can hit CHECKs. Register factory dependencies and remove
observers/weak callbacks during profile shutdown.

The Desktop journal shares the existing SyncStore transaction. Before applying
remote metadata, it durably records the intended value and a local receipt;
Chromium saves that token alongside the native fields. A receipt is trusted only
after its logical identity is resolved through the profile ledger, never to
assign identity to a clone/import. Original baseline, acknowledged observation
and per-adapter observation session stay distinct for asynchronous-save recovery.
No test-only flush API or second persistence authority is introduced.

Invalid native content (including embedded URL credentials) is not sanitized or
exported. It pauses the Bookmark reconciliation with a visible local explanation
until a complete valid capture exists. Existing local data remains usable and
unchanged, and other categories continue independently. A failed local journal
write must likewise block an older remote projection from replacing that local
mutation. This conservative pause is not a partial-sync success; native schemes
allowed by the frozen content contract do not themselves trigger that pause.

## Ownership and acceptance

The bookmark owner owns this cross-platform contract and the Desktop bookmark
domain/adapter integration. Mobile product/Swift implementation is coordinated
with the Mobile owner; no concurrent schema implementation starts without the
field contract and file handoff. The Desktop owner retains tree animation files,
the shared checkout/build/install lease and its current UI transaction.

First prove the visible journeys on exact built/installed Desktop and Mobile
candidates, then run focused codec, migration, merge, idempotence, deletion,
account/opt-in and adapter regression tests. Test with synthetic data and existing
guarded Development facilities; do not infer Production authority. Two simulated
Swift peers do not exercise the Chromium adapter. Missing device/account/key
bootstrap or provisioning evidence remains explicitly open, not a sync pass.

## Evidence pointers

- `spikes/cloudkit/Sources/AhoiCloudKitSpike/SyncModels.swift`
- `spikes/cloudkit/Sources/AhoiCloudKitSpike/AppleCloudKitRecordCodec.swift`
- `apps/AhoiMobile/Sources/AhoiMobileCore/CompanionSyncBridge.swift`
- `overlay/chromium/src/ahoi/browser/sync/sync_model.h`
- `overlay/chromium/src/ahoi/browser/sync/sync_store_schema.cc`
- `overlay/chromium/src/ahoi/browser/sync/cloudkit_sync_record_codec_mac.mm`
- M152 `components/bookmarks/browser/bookmark_node.h:74`,
  `bookmark_model_observer.h:28`, `bookmark_model.h:326`.
- M152 `chrome/browser/bookmarks/bookmark_merged_surface_service.cc:265`
  and `:539`.
