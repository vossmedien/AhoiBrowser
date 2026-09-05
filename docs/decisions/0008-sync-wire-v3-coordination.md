# ADR 0008: Combined bookmark/tab wire-v3 coordination

Status: ownership and identity assignments recorded on 2026-09-05. The semantic
v3 additions below implement ADR 0007; capability publication and legacy-clock
upgrade details must be agreed before v3 writers are enabled.

## Disjoint ownership

- Bookmark owner `01a06d69-1034-7372-b784-0b05a53c87e0`: common C++ sync models,
  serialization, field merge/validation, SyncStore schema, provider mappings,
  common version/policy configuration and shared golden wire fixtures. Its
  Bookmark adapter remains a separate native domain from workspace tabs.
- Mobile owner `01a044d6-1545-7532-8394-6b7df1144bb1`: shared Swift record/wire
  code in `spikes/cloudkit` and `apps/AhoiMobile`, compatible local persistence,
  Mobile binding/projection/activation and tests for both ADR 0006 and 0007.
  It does not edit the common C++ seams listed above.
- Desktop owner `01a04f97-e3ba-70f2-a031-220b214d352d`: native TabTree/session/UI
  integration, tree persistence, `tab_tree_sync_adapter.{h,cc}` and the shared
  checkout/build/install lease. Common ProfileSyncService/backend headers stay
  with the Bookmark owner; send proposed signatures before integrating new
  native methods. Separate implementation files may be assigned explicitly.

Assignments were sent to Mobile in `01a070cb-a957-78d3-b96f-ebe9828d1ed8` and
Desktop in `01a070cb-a9c3-7040-a2f7-290737197fda`. These are ownership grants,
not proof that an implementation has started or that the remaining freeze
points below have been accepted.

## Shared tab identity and fields

The logical tab/page identity is `TreeNodeID` on every client. Runtime tab
handles/UUIDs and per-device presence record IDs are distinct and local-bound;
no presence record may reuse a TreeNode UUID in CloudKit's shared UUID namespace.
Do not deduplicate independent tabs by URL or replace a saved node on save/unsave.

Wire v3 adds `TreeNodeRecord.is_temporary` (boolean, default false for legacy
records) and `RemoteTabRecord.tree_node_id` (optional UUID, legacy unlinked).
Each has its own field clock. `pinned` remains presence/navigation presentation,
not saved-page persistence authority. Folders cannot be temporary.

Explicit empty temporary pages use an empty URL and a platform-owned new-tab
surface; automatic startup/recovery placeholders stay local. The existing
invariants about no focus stealing/eager loading, remote close/before-unload,
private-state exclusion and separated bookmarks remain binding in ADR 0007.
Nonportable targets must have an explicit compatibility treatment; they must
not disappear silently or be replaced with a fake portable URL.

## One deterministic system Inbox

Reserve Workspace UUID `83699047-edf8-580d-948d-9c37acc35cb6` for the shared
Inbox and system-actor UUID `9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86` for deterministic
bootstrap/unknown-provenance metadata. These are UUIDv5 values using the standard
URL namespace and names `ahoi:workspace:shared-inbox:v1` and
`ahoi:sync:system-actor:v1`; no network request or runtime hash is needed.

Canonical initial Inbox fields: name `Inbox`, empty icon, sort key `0`, no accent,
created/modified time Unix epoch, tombstone false. Initial record/field HLC uses
Unix epoch, logical zero and the reserved system actor. On the desktop wire,
that physical time is decimal string `11644473600000000` (Windows-epoch us).
Display may localize the unchanged system name; wire values never depend on UI
locale or the creating device. A real user edit uses its actual device clock.
The system actor is not a real-device origin badge or a device enrollment.

Use this workspace for genuinely unassigned explicit tabs/empty first-run state;
do not move or rename existing user workspaces as a side effect. Existing user
content and ambiguous legacy runtime bindings need a preserving migration.

## Version and legacy safety

- Read old tree/presence records according to their original v1/v2 schema;
  v2 keeps its exact existing field-clock map. Never append v3 fields to v2.
- A legacy read projects saved/persistent and unlinked defaults, but absence
  is not a new write. An old update with a newer record clock cannot erase
  explicit v3 `is_temporary` or `tree_node_id` field state.
- Unknown/new-field clocks must not manufacture a creator/saving-device badge.
  C++ and Swift must agree a canonical migration representation and cover
  both merge directions. Swift HLC fields require real UUID strings and cannot
  represent Windows-epoch timestamps before the Unix epoch.
- Maximum readable version and the version authored by an operation are
  different concepts. Merely raising a constant must not upgrade unrelated
  records or emit v3 from a generic merge before the capability gate.
- Existing Bookmark v2 payloads remain readable after v3 support; their six
  field groups and entity discriminator `11` do not change.
- Enable shared-tab v3 publication only with explicit matching-client/capability
  evidence. Capability announcement itself must work before that gate; do not
  implement a handshake that requires a v3 write to permit its first v3 write.
  Unknown/older peers need an actionable upgrade/retirement state, not data loss.

Capability representation, the precise legacy-clock upgrade and nonportable-tab
representation are the remaining cross-owner freeze points. No new server,
Production-CloudKit deployment, Android implementation or fixture-based runtime
pass is authorized or claimed by this document.
