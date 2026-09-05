# ADR 0008: Combined bookmark/tab wire-v3 coordination

Status: concrete coordination contract frozen on 2026-09-05 for matching C++
and Swift implementation/review. This is not writer activation. All current
writer defaults remain v2 until the capability and migration gates pass.

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

## Capability bootstrap: separate wire-v2 entity

Reserve `EntityType = 12`, `dataClass = "deviceCapability"`, authored using
unchanged common wire-v2 metadata, with exact field-clock groups `device_id`,
`capabilities`, `tombstone`. Do not extend Device-v2 or make a v3 record necessary
for the first capability announcement.

| Payload field | Contract |
| --- | --- |
| `device_id` | immutable lowercase UUID of the corresponding DeviceRecord |
| `readable_models` | sorted, unique integer list in 1…32; supported readers |
| `writable_models` | sorted, unique integer list in 1…32; implemented writers, not active defaults |
| `features` | sorted, unique ASCII identifiers, at most 32 entries of 64 bytes; known feature is `shared-normal-tabs-v3` |

The last three values form atomic group `capabilities`. Empty model lists are
invalid; unknown feature names never grant known capabilities. Common record
and all field-clock device IDs must equal `device_id`. Record ID is UUIDv5 with
the standard URL namespace and UTF-8 name
`ahoi:sync:capability:v1:<lowercase-device-id>`. It is distinct from Device,
TreeNode and Presence IDs. A capability declaration is accepted only alongside
its independently known, authenticated same-collection DeviceRecord; it cannot
enroll a peer, grant remote control or change device retirement.

V3 writes require global sync/normal-profile authority, a completed successful
initial private-zone download (all pages), acknowledged local device/capability
publication, and compatible declarations for every known non-retired peer.
Each peer needs readable model 3, writable model 3 and `shared-normal-tabs-v3`.
Only advertise that feature when the local native/domain implementation and
its matching conformance gates actually exist; read-only preparation is not it.

Unknown peers, missing/invalid/tombstoned capability records and pending device
records close the gate. Offline age does not retire devices. Explicit user
retirement uses the existing DeviceRecord retirement operation, not capability
deletion. New/reappearing incompatible peers close subsequent v3 writes without
downgrading, deleting or stripping existing v3 records. Show the peer needing an
upgrade or deliberate retirement. Reevaluate on every registry/capability/account
change. Failed/offline bootstrap does not guess a complete roster. No mechanism
can predict a device that has never enrolled; its later enrollment closes the
gate, and older readers must retain unsupported encrypted records unchanged.

## Legacy absence and promotion

Persist actual v1/v2 new-field absence, not synthetic writes. In a mixed merge,
absent `is_temporary` or `tree_node_id` contributes no candidate clock/value;
an explicit v3 field survives even a newer v2 record clock. Preserve that rule
in both merge directions and after local snapshot restart. Unchanged unrelated
fields still use their original merge rules and clocks.

At an explicitly authorized v3 promotion, encode default false / omitted binding
with the agreed Bottom clock: Unix zero, logical zero, system actor
`9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86`. Present `tree_node_id:null` is invalid;
nil is omission, while its v3 field clock is still required. A Bottom-clock new
field can represent only false/unlinked. Recognize this sentinel as absence,
not through normal device-ID tie ordering. An actual v3 mutation must use a
non-system device and a clock strictly later than Unix zero.

TreeNode creation-time values remain immutable. Legacy v1 clocks can have been
synthesized, and a later v2 rewrite does not prove original authorship. For
conservative v3 promotion of legacy data, keep the creation-time value but use
Bottom for its *provenance clock* unless genuine creation provenance was already
retained explicitly. Subsequent legacy input must not replace v3 creation
provenance with a synthetic last-editor clock. New v3-authored nodes retain their
real creation clock. No Mobile/creator badge is derived from Bottom, v1 fallback
normalization or last-record clocks. Bookmark creation metadata is unaffected.

## V3 tab target values: distinguish web, new-tab and local-only

Normal tabs need a different target policy from unrestricted bookmark metadata.
V3 Page TreeNode and Presence payloads add integer `target_kind` and optional
`local_scheme`; these belong atomically to the existing clock group `url`
together with `url`, not to a new independent field clock. The two new clock
groups remain `is_temporary` and `tree_node_id` on their respective entities.

| `target_kind` | `url` | `local_scheme` | Meaning |
| --- | --- | --- | --- |
| `0` web | canonical, valid HTTP(S), host required, no user/password, at most 131072 bytes | omitted | normal platform URL policy on deliberate activation |
| `1` new-tab | empty | omitted | explicit temporary page only, linked Presence required |
| `2` local-only | empty | one of `about`, `chrome`, `chrome-extension`, `file`, `blob`, `data`, `javascript`, `other` | preserve row/identity and explain unavailable activation; never open a fallback |

Folders omit both target fields and retain empty URL. V1/v2 retain their original
web-only layout; nonportable or empty v2 input is not silently upgraded. Local-only
targets are not fake URLs: this is explicit versioned availability metadata.
Their original URL/code/file path remains solely in the originating native
runtime/session store. A shared title is metadata, not executable input. Presence
for new-tab/local-only targets must link a valid shared page and agree with its
current target representation; it never creates another global page authority.
Do not overwrite an origin's local-only native target with the empty wire URL.
No URL scheme may cause automatic OS, file or JavaScript execution on another
device. Automatic startup/recovery placeholders remain local as in ADR 0007.

Canonical future conformance fixture:
`overlay/chromium/src/ahoi/browser/sync/testdata/shared_tab_wire_v3_contract.json`.
Both implementations must consume it, plus real migration/registry/runtime tests.
This file and the contract do not enable v3 or establish a passing test.
No new transport, Production deployment or Android implementation is authorized.

The requested post-Bookmark native/common API and preserving capture contract is
recorded in [SHARED_TAB_NATIVE_SEAMS.md](../SHARED_TAB_NATIVE_SEAMS.md). It does
not change this wire fixture or activate any header, runtime caller or writer.
