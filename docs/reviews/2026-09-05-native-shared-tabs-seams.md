# Native shared-normal-tab seams

Status: Desktop preparation against committed ADR 0008 / `09cae9f`.
The ADR and canonical JSON fixture remain the contract. This note records native
implementation boundaries and requested common APIs, not a replacement schema
or writer activation. Common C++ files remain Bookmark-owned; shared Swift
remains Mobile-owned.

## Prepared, but not in the running product

`session/shared_tab_target_policy.{h,cc}` is a small, UI-/network-free native
target boundary. It is currently referenced only by Session unit tests, not by
the browser/session runtime or a transport. It does not change the Native Tree
database, journal, model version, codec or global write-version policy.

- Normal eligible web targets retain canonical bounded HTTP(S) metadata.
- Private/automatic-placeholder eligibility defaults to excluded; explicit
  logical empty temporary tabs are a separate, deliberate input.
- Other targets retain only the allowed local scheme class; full file paths,
  code, extension URLs and credential-bearing URL bytes are not returned.
  Unsupported native web targets use local-only/other, not a stripped URL with
  different semantics. Incoming web records with userinfo are rejected.
- A linked Presence must match both logical Page ID and the entire selected
  target group. The helper does not repurpose its Presence record ID, and
  unlinked legacy rows remain outside this shared-row path.
- Explicit activation can choose web navigation, the platform NTP, or reuse of
  an existing local runtime with that same logical ID. A peer without a local
  binding cannot invent a file/code/OS launch or navigate to an empty fallback.

Six test cases consume the single canonical fixture's target cases and cover
the explicit-empty boundary, strict groups/byte limit, local-target redaction,
Presence identity and local activation. They are written, not compiled/run.
This is not a complete wire decoder or a passing shared-tab implementation.
Titles/other metadata, whole-record validation, capability/consent, clocks and
native runtime actions remain responsibilities of the integrating authorities.

## Existing paths to reuse

`ProfileSyncService::PublishWindowTabs` already owns the per-window union and
generates separate Presence IDs. `AttachUiBridge` observes the authoritative
tree through `ProfileSyncUiBridge`; the backend's `MergeLocalTabTree` and the
service's `ApplyDomainState` already reconcile through `tab_tree_sync_adapter`.
Keep these authorities and sequences. Do not add a parallel replication loop.

Before v3 integration, change the current raw-URL `ConvertNode`/
`TreeNodeToSyncRecord` conversions: native v1 TreeNode cannot represent empty
targets or `is_temporary`, and blindly assigning the empty wire URL would lose
an origin's local target. Preserve such native targets in local runtime/session
ownership, never in the portable payload. Origin-side reopening of unloaded
local-only saved pages needs durable local target lookup before activation.

## Requested common seams — pending owner agreement

1. Extend `LocalTabState` with optional `tree_node_id` and the versioned target
   description. Keep `sync_id` separate; `pinned` is not persistence authority.
2. Add a backend-computed `SharedTabSyncState` to `SyncStateSnapshot`, exposing
   `projection_ready`, `write_allowed`, a reason and blocking device IDs through
   a Service getter and its existing Observer pattern. Projection readiness is
   not the same as permission to author v3.
3. A default-fail-closed native-support value/subscription on the existing UI
   bridge lets the backend advertise support only after the full native path
   and conformance exist. No imperative `EnableV3Writer` shortcut. Check the
   current gate again on the backend sequence when mutating or enqueueing.
4. Return authoritative state from local capture/mutation completion, including
   the gate state. Derive origin/saving badges from retained field clocks and
   known devices through this same Service; Bottom/legacy gives unknown, not a
   guessed creator or a second creator-ID field.

Concrete handoff request sent to Bookmark in
`01a0718f-88a7-7123-8c5a-c9e858ff02b5`. Its common headers/backend are not edited
by Desktop before agreement.

**Preserve/defer is not deletion:** `ReplaceLocalTabs` currently filters via the
HTTP-only `IsShareableTab` and then tombstones entries absent from the resulting
map. A closed capability gate, unsupported target, incomplete capture or stale
authority must not be expressed as an authoritative empty/filtered capture.
Preserve records and pending local intent until the correct gate/migration
allows publication. Real user close is a separate, explicit native event.
App shutdown/session retirement removes local Presence, not every logical
temporary page. Remote-close cancellation must preserve local unsaved content
without resurrecting the tombstoned global ID or broadcasting an automatic NTP.

## Integration order

Keep `6bd3b70`'s Arc/Sidebar corrections and the impending Bookmark-v2 source
wave separate from native DB migration. The real Default-profile Arc journal
and backup fingerprints are still unresolved and must remain recoverable by
their exact schema. After that candidate's explicit recovery/acceptance and the
matching common v3 gate/schema handoff, implement native persistence, live
projection, capture bindings, save/unsave/close and origin decoration together.
Visible already-open Desktop/Mobile journeys precede focused programmatic
acceptance. No new standalone build, CloudKit signing change or v3 activation
is requested by this preparation.
