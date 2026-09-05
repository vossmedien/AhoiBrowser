# Shared-tab native/common seams

Status: post-Bookmark integration contract, 2026-09-05. This records the
requested interfaces and preserving behavior; it does not claim that these
headers, gates or callers are implemented. Bookmark source `c28ec4a` and the
current single Desktop build remain unchanged. ADR 0008 / `09cae9f` owns the
wire contract and canonical fixture; no new wire fields are introduced here.

## Ownership and dependency direction

- Bookmark/common owner: common leaf types, `profile_sync_types.h`,
  `sync_model.h`, `profile_sync_ui_bridge.h`, Service/backend/store/provider,
  C++ codec/merge/schema/GN/policy and tests.
- Desktop owner: native Tree/Session/UI callers, `tab_tree_sync_adapter`,
  `session/shared_tab_target_policy.*`, shared checkout/build/install.
- Mobile owner: matching Swift domain/wire/persistence/projection/UI/tests.

`dc01cb5` is isolated native target-policy preparation, not a runtime caller or
v3 capability announcement. During the later explicit header handoff, move its
pure target value types to a common leaf header and preserve a native alias.
The common layer must not depend on Session/UI or maintain a second target enum.
Do not edit Desktop's policy files from the common-owner thread.

## Requested additive API

The following are interface names/shapes, not landed declarations:

```cpp
// Add to LocalTabState; sync_id remains the separate Presence identity.
std::optional<base::Uuid> tree_node_id;
std::optional<SharedTabTargetKind> target_kind;
std::optional<std::string> local_scheme;

enum class LocalTabCaptureStatus { kDeferred, kComplete };
struct LocalTabCapture {
  uint64_t generation = 0;  // Service-issued, local-only capture token.
  LocalTabCaptureStatus status = LocalTabCaptureStatus::kDeferred;
  std::vector<LocalTabState> tabs;
};

struct SharedTabNativeSupport {
  bool projection = false;
  bool capture = false;
};

enum class SharedTabSyncIssue {
  kDisabled, kNativeNotReady, kBootstrapPending, kPeerUpgradeRequired,
  kRecoveryPending, kCaptureDeferred, kInvalidCapture, kStoreError, kNone
};
struct SharedTabSyncState {
  bool projection_ready = false;
  bool write_allowed = false;
  SharedTabSyncIssue issue = SharedTabSyncIssue::kDisabled;
  std::vector<base::Uuid> blocking_devices;
};

// Derived UI result only; never stored or serialized as new creator fields.
struct SharedTabProvenance {
  std::optional<base::Uuid> creation_device;
  std::optional<base::Uuid> saved_device;
};
```

The existing `url` and added target fields form one value, copied/validated
atomically. Absence represents legacy/unready state, not an implicit v3 web
write. `local_scheme` admits only ADR 0008's eight strings. A local-only capture
contains an empty transport URL; original native paths/code remain local.
Every participating new shared normal tab needs its stable TreeNode binding.
`sync_id` cannot equal `tree_node_id`; equal URLs do not imply equal identities.

Add `SharedTabSyncState shared_tabs` to `SyncStateSnapshot`, with the same
default-false semantics. On ProfileSyncService expose:

```cpp
const SharedTabSyncState& shared_tab_sync_state() const;
SharedTabProvenance GetSharedTabProvenance(const base::Uuid& tree_node_id) const;
void PublishSharedTabCapture(std::string window_key, LocalTabCapture capture);
```

Its Observer gains default-no-op
`OnAhoiSharedTabSyncStateChanged(const SharedTabSyncState&)`. Existing consumers
do not become v3-ready merely because this method or getter exists.

Extend the existing ProfileSyncUiBridge with default implementations:

```cpp
virtual SharedTabNativeSupport GetSharedTabNativeSupport() const { return {}; }
virtual void RequestSharedTabCapture(uint64_t generation) {}
```

Keep the old `PublishWindowTabs(vector)` / `RequestLocalTabCapture()` signatures
for legacy callers. Distinct names avoid ambiguous `{}` overloads. They remain
v2-only and cannot become an authoritative fallback when shared-v3 capture is
unavailable. This is one service/bridge with versioned entry points, not another
transport, engine or native tab registry.

## Capture authority: preserve/defer before any mutation

Current source risk is concrete: `ProfileSyncBackend::ReplaceLocalTabs` filters
with HTTP-only `IsShareableTab`, then tombstones missing live Presence rows.
`PublishCombinedLocalTabs` also prunes identity/reverse-lookup maps before the
backend reply. Neither behavior may be reused for an incomplete shared capture.

1. Service issues a generation bound to the current profile/backend, native
   bridge, registered-window set and relevant account/policy scope. All windows
   answer the same request. A stale reply, missing window, failed native read,
   shutdown/detach or changed roster makes the aggregate deferred. Never infer
   completeness from vector size or process/window absence.
2. Deferred input preserves the last accepted window data, pending local intent,
   Presence-ID mappings and reverse lookups. It neither feeds an empty vector to
   the legacy path nor deletes/rewrites domain rows, clocks or outbox entries.
   Request a fresh capture on a relevant readiness/registry event; no polling.
3. Exclude private tabs and automatic recovery placeholders at the established
   native participation boundary. An unrepresentable *normal* tab, missing
   logical binding or invalid target defers/rejects the aggregate; filtering it
   out is not evidence of user deletion. Never attach secret native URL bytes to
   an error or deferred snapshot.
4. Backend rechecks current authority and capture generation on its own sequence,
   including immediately before the mutation transaction. UI `write_allowed`
   is an explanation, not a capability token or authorization argument.
5. Validate every identity, duplicate, target and required Page/Presence link
   before constructing the authoritative next set. Apply the complete batch in
   one existing-store transaction. Publish the new in-memory accepted state and
   prune its identity maps only after successful commit/acknowledgment. A partial
   SQL failure cannot leave half a capture committed or falsely acknowledged.
6. A genuinely complete, current, authorized empty capture can close Presence
   rows for its scope. Presence absence never authorizes deletion of global
   TreeNodes. Saved pages survive native close; temporary logical-tab deletion
   requires the separately accepted explicit close mutation. App/host shutdown
   must not fabricate a global close-all command.

The backend reconciliation result must distinguish applied, deferred, invalid
and store failure. Only applied advances the accepted capture generation. A
deferred result returns current state/issue, not a successful empty replacement.
When a gate reopens, first request a fresh complete capture; do not replay a
previously filtered or pre-account-transition vector as authoritative.

Passive capture of an old loaded WebContents target must not undo a newer shared
Page target when the peer was deliberately not auto-navigated. Explicit local
navigation authors the Page target through the native owner; Presence is not a
second URL authority. Keep the shared row and existing local runtime distinct
and validate the linked target representation before publication.

## Gate and provenance meanings

`projection_ready` means a validated current domain snapshot can be consumed by
the native projection. It does not imply writer permission. A compatible reader
may retain/project accepted v3 state while a newly incompatible peer closes
writes. False readiness preserves local/native state; it does not remove rows,
switch focus or load a URL.

`write_allowed` is specifically shared-normal-tab v3 authoring authority. Backend
requires implemented common/native support, normal profile/global Sync, current
account/key/recovery authority, complete successful provider bootstrap, actual
matching local Device/Capability publication acknowledgments, and every known
non-retired compatible peer from ADR 0008. Native support alone cannot announce
capability or activate a writer. Blocker IDs are unique/sorted and derived from
the verified same-collection registry. No `EnableV3` shortcut and no global
version bump; Bookmark/Capability authoring remains wire-v2.

Gate transitions must invalidate queued mutations and delayed projection/apply
replies; a cached UI preference or old status callback is not renewed authority.
Provider-side fencing remains an additional final transport boundary.

Creation provenance comes from a genuine v3 `created_at` field clock. Saved
provenance, for a persistent page, comes from its genuine `is_temporary=false`
field clock (explicit creation/save, not a promoted default). Resolve only known
DeviceRecords from the same immutable snapshot. Bottom/system, synthetic legacy,
missing clocks or unknown device metadata yield no badge; never use the last
record editor. These derived IDs add no persistent/wire creator field. Preserve
genuine local creation evidence independently during legacy rewrites/promotion;
the Mobile review finding on `3964bcb` remains a separate correction requirement.

## Required regression/acceptance cases

- Complete nonempty -> incomplete/invalid/nonportable-not-yet-representable ->
  recovery: zero inferred tombstones and unchanged Presence IDs.
- Gate closes between capture, queue and backend commit; stale completion or
  account/window-generation reply cannot mutate or clear accepted state.
- Multi-window move/close/detach and missing replies; no identity churn or global
  delete from a transient empty aggregate; explicit authorized Presence-empty
  remains distinguishable from deferred.
- Injected mid-batch SQLite failure rolls back records, tombstones, clocks/outbox
  and acceptance, retaining the last in-memory/native baseline.
- Linked web/new-tab/local-only targets, no eager navigation/focus or original
  path leakage; later peer appearance closes writes while preserving projection.
- Creation versus save badges, Bottom/legacy/unknown-device cases and retained
  genuine local provenance across both merge directions and restart.

First the exact candidate's visible journey, then meaningful programmatic
tests, or an explicitly documented technical-E2E exception. The existing six
native target-policy tests are preparation, not these capture/gate proofs.
