# ADR 0009: One pre-launch sync format, no legacy migration project

Status: binding user decision, 2026-09-05. The app is not live and is not being
actively used. The user explicitly requests one uniform sync format on iOS and
macOS and excludes an elaborate development-data migration. This supersedes
ADR 0006/0008 wherever they require continuing wire-v2 authoring, parallel
v2/v3 operation, legacy promotion or mixed-version merge support.

## One current contract

The single target wire/model version is **3**, for every allowed entity below.
The machine-readable contract is `config/sync-format.json`; both languages
must match its discriminator, field-map and numeric bounds. Its fixturePath
reserves the new canonical all-entity golden, not an already executed test.
Envelope schemaVersion, payload model_version and version_model agree on 3.
There is no permanent Bookmark-v2 or Capability-v2 exception. Domain types stay
distinct: an identical format does not turn bookmarks into workspace pages or
merge private browser state into the sync collection.

| Entity ID | Existing dataClass | Meaning |
| --- | --- | --- |
| 0 | device | Device identity/liveness/explicit retirement |
| 1 | workspace | Workspace metadata and ordering |
| 2 | treeNode | Shared folders and normal saved/temporary pages |
| 3 | historyVisit | Allowed history with existing retention |
| 4 | deviceTab | Device Presence linked to the shared page |
| 5 | deviceSession | Device-session metadata |
| 6 | remoteCommand | Existing explicitly authorized command protocol |
| 7 | appearance | Shared appearance preferences |
| 8 | permittedSetting | Explicit settings allowlist |
| 9 | extensionInventory | Inventory, never extension storage |
| 10 | developerAsset | Individually approved non-secret assets |
| 11 | bookmark | The separate shared native bookmark collection |
| 12 | deviceCapability | Functional capability declaration |

IDs 0–11 and dataClass strings are preserved; Capability remains reserved 12.
All unchanged payload fields/atomic clock groups keep their current meaning.
The existing Device kind 3 (`other`) is represented losslessly on both clients;
it is descriptive metadata, not authority to execute commands. The existing
command lifecycle uses status transitions, not generic entity tombstones;
RemoteCommand tombstones remain disallowed in both implementations.
TreeNode uses explicit is_temporary and the web/new-tab/local-only target value
from ADR 0008; Presence adds its distinct tree_node_id and the same target value.
The native complete-capture contract remains preserving/fail-closed.

Fresh shared normal tabs all receive stable TreeNode identities. The local DTO
may be temporarily unlinked while capture is deferred, but a newly published
shared Presence must link its actual page. It may not reuse that page's UUID
as its own Presence record ID. Missing peers/parents delivered out of order are
retained pending validation/materialization, never replaced by fabricated rows.

All incoming/outgoing records use complete exact field-clock maps. No incoming
missing clocks are filled from a newer enclosing record clock. Local authoring
creates explicit clocks; field merge remains deterministic and tombstones/
deletion watermarks remain durable. Record/field devices are canonical lowercase
UUIDs; physical HLCs are lossless Windows-epoch microsecond strings at/after Unix
epoch, logical counters are exact unsigned-32-bit values. Readers must agree on
numeric bounds and reject booleans/nonintegral/out-of-range values. Bookmark
creation time stays separate positive signed-Int64 Windows metadata, including
pre-1970 values. No new creator-ID field or synthetic last-editor badge.

## What deliberately disappears

- No active writer for format 1 or 2; no per-entity mixed write versions.
- No lazy v1/v2 record upgrade, mixed-v2/v3 merge matrix, Boolean-only provenance
  migration or compatibility workflow for installed old development clients.
- No automatic conversion of old development stores, outboxes, encrypted caches
  or CloudKit records. Earlier source and evidence remain historical, not
  acceptance obligations for migration code.
- No old-client/offline-age compatibility machinery as a release prerequisite.
  All test clients are updated together to the unified candidate.

Unknown/old formats are rejected without overwriting their source. A current
reader encountering unsupported input must not treat it as deletion or an empty
authoritative snapshot. Retaining/quarantining opaque input remains useful for
corruption recovery; that is not a migration implementation.

## Fresh development state, not silent deletion

Use fresh isolated local stores/profiles and an explicitly shared fresh
Development test zone for the new cross-client acceptance. Do not reuse an old
engine checkpoint or inbox as if it belonged to the new namespace. Keep the
existing engine/container/crypto; a fresh zone is not a second transport.

The new local C++ sync store uses schema 6 with an exact-version check. Do not
renumber SQLite schema to 3: storage layout and the one wire format are different
technical contracts. Swift persistence also needs an explicit current-format
marker and must not silently decode an old snapshot as an empty new collection.
No expensive old-layout migration is required.

Fresh defaults are named explicitly in the manifest: Desktop
`Ahoi Sync/sync-format3.sqlite` and `cksync-format3.state`, Mobile
`AhoiMobile/SyncFormat3/snapshot-format3.json`, shared Development zone
`AhoiBrowserSyncV3`. A missing new file may start empty; incompatible existing
bytes must fail without overwrite. Native bookmark observation-ledger rows
contain local content, not fabricated wire clocks; actual apply receipts keep
the real current-format record. None of these source defaults performs an
account, server, key or existing-profile mutation on its own.

Existing profiles, backups, failed-run logs, journals, provisioning assets,
keys and server records are not automatically deleted. If a reset is needed,
identify the exact isolated test target and retain any required recovery evidence
before that bounded action. This ADR does not authorize a broad account/profile
wipe or a Production mutation. Product local bookmark/tab editing stays usable.

## Feature readiness is separate from format version

Capability declarations also use format 3. They can be exchanged as control
metadata before shared-tab authoring is ready; no circular requirement that a
data writer be enabled before its first capability announcement. Their model
lists describe the single current format, not legacy authoring. The existing
`readable_models` and `writable_models` are each `[3]` for the unified client;
unsupported functional capabilities are omitted from features, not represented
by advertising an old write format. The feature identity
`shared-normal-tabs-v3` denotes implemented native behavior.

A common format does not itself prove that a native consumer is implemented.
Keep default-false native capture/projection support, the backend's effective
authority checks and actual provider acknowledgments. Do not advertise a feature
or activate automatic native projection merely by changing a version constant.
This readiness boundary must not become a second simultaneous wire format.

Global/category opt-in, account/key/recovery boundaries and the new original-
generation authorization guards stay intact. Cookies, passwords, autofill, site
data/cache/permissions, private tabs, extension storage, secret headers and
Keychain material remain excluded; the current sync allowlist is not broadened.
Device-local split topology remains local unless separately decided by the user.

## Ownership and implementation sequence

One canonical branch; no competing development branches or build paths.

1. Unified Sync owner `01a06d69-1034-7372-b784-0b05a53c87e0`: C++ model/codec/
   validation/merge/store/provider/config/GN and canonical format-3 fixtures,
   PLUS matching Swift models/wire/domain/persistence/provider/bridge/Mobile
   binding/UI/tests in `spikes/cloudkit` and `apps/AhoiMobile`. The explicit
   handoff through `f25eea5` supersedes the earlier separate Mobile owner. Do
   not wait for another Swift implementation or retain obsolete migration code
   merely to satisfy its historical tests.
2. Coordinator `01a044d6-1545-7532-8394-6b7df1144bb1`: read-only product review,
   coordination and its own checkpoint/prompt; no parallel Swift product writes.
3. Desktop owner `01a04f97-e3ba-70f2-a031-220b214d352d`: native Tree/Session/UI and target/capture/projection bindings,
   plus shared checkout/out/build/install. Common types are handed off explicitly.

The currently running 225df88 build stays immutable and may finish as a UI/
compiler baseline. It is not the final unified-format sync candidate. The three
bounded test-API fixes in 22e2f2b do not activate or implement this format.
Future integration uses only agreed committed source, not another owner's WIP.

Concrete package sequence: common format/fixtures and strict fresh-store C++;
matching Swift one-format domain/wire/persistence; native/Mobile capture and
live projection on the agreed headers; then one coordinated candidate wave,
representative visible E2E and the focused cross-language/consent suites. Native
Tree/Session/UI files require a specific Desktop handoff rather than silent edits.

## Acceptance

- One canonical golden set for all 13 entity classes, consumed directly by both
  C++ and Swift; roundtrip and exact field/clock/identity validation.
- Only format 3 is authored/accepted by the live boundary. Formats 1/2, incomplete
  maps and incompatible stores are rejected without mutation; fresh stores work.
- Real candidate-bound macOS/iOS visible bookmark and shared normal-tab journeys,
  then programmatic tests. A technical E2E exception is explicit, not a pass.
- Actual cross-client CRUD/order/offline-conflict/restart/delete convergence,
  no duplicate identities, no focus stealing/eager loading, correct native-
  target handling and preserving incomplete capture.
- Original-scope consent/recovery regressions remain required. Production,
  provisioning/key bootstrap and real CloudKit proof are separate evidence.

No old migration test or two-simulated-peer relay substitutes for this acceptance.
