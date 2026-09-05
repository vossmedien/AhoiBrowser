# Shared-tab native/common seams

Status: native seam proposal updated for the explicit fresh-data/single-format
user decision of 2026-09-05. The app is not live/actively used. The target is one
active format for every relevant, permitted entity on macOS/iOS: format 3 under
ADR 0009 / `config/sync-format.json`, including Bookmark and Capability. No complex old-data migration,
permanent v2/v3 coexistence or old-client support is required. Fresh isolated
stores are used for the new matching-candidate acceptance; existing profiles,
CloudKit data and keys must not be silently reset, deleted or rewritten.

This decision supersedes the mixed-version/legacy-bootstrap parts of the earlier
ADR 0008 / `09cae9f` proposal. The simplified canonical format/field contract is
published as `1bfae11`; common target/capture value types and default-off UI
bridge declarations are explicitly handed over as `5885d01`. The Sync owner
still supplies the actual Service/backend capture implementation before runtime
callers or new defaults are integrated. The identity, target,
capture and privacy invariants below remain required; this document introduces
no additional wire fields. The frozen 225df88 UI/compile baseline and
its test-only correction 22e2f2b are not final unified-format acceptance.

## Ownership and dependency direction

- Unified Sync/common owner: common leaf types, `profile_sync_types.h`,
  `sync_model.h`, `profile_sync_ui_bridge.h`, Service/backend/store/provider,
  C++ codec/merge/schema/GN/policy and tests.
- Desktop owner: native Tree/Session/UI callers, `tab_tree_sync_adapter`,
  `session/shared_tab_target_policy.*`, shared checkout/build/install.
- Matching Swift domain/wire/persistence/projection/UI/tests are coordinated
  and implemented by the same unified Sync owner, not Desktop or the read-only
  coordinator. Current explicit assignments are in `docs/ACTIVE_SYNC_COORDINATION.md`.

## Accepted Desktop-native implementation package A-D

Desktop `01a04f97-e3ba-70f2-a031-220b214d352d` explicitly accepts all four parts;
no part is delegated back to the Sync owner. This is implementation ownership,
not an implementation/test pass. Paths are relative to
`overlay/chromium/src/ahoi/browser/`.

| Part | Desktop-owned files/seams | Required result |
| --- | --- | --- |
| A | `session/shared_tab_target_policy.h` and its owned implementation/tests | Alias the completed common leaf target type; no competing enum or Common-to-Session dependency. |
| B | `session/session_bridge.h`, `session_bridge.cc`, `session_bridge_runtime.cc`, `session_bridge_session.cc`, `session_bridge_observers.cc`; `tab_tree/tab_tree_model.h` and existing native Store seams | Stable global IDs for normal temporary tabs; Save/Unsave preserves identity; complete persistence/snapshot/undo/readback and native lifecycle handling. |
| C | `sync/tab_tree_sync_adapter.{h,cc}` and owned adapter tests | Full current-format Tree/target projection into the existing normal tree UI; no device-only substitute, identity echo, focus stealing or eager loading. |
| D | `ui/sidebar/browser_sidebar_host_device_tabs.cc`, `browser_sidebar_host_core.cc` and owned host/capture tests | Replace unqualified `PublishWindowTabs`/`RemoveWindowTabs` behavior with generation-bound complete/deferred capture; absent/detached windows never become global delete authority. |

For B the existing native `tab_tree_store.{h,cc}`, `_internal.{h,cc}`,
`_node_mutations.cc`, `_queries.cc`, `_snapshot.cc`, `_undo.cc`,
`_duplicate_workspace.cc`, `_move_delete.cc`, `_workspace.cc` and `_validation.cc`
remain Desktop-owned as needed, with `tab_tree_store_unittest.cc`. This is NOT
the Common `sync_store.*` scope. The existing native tree controller/row/projection
surface also stays with Desktop; no parallel tree UI is introduced.

Existing adapter cases inside a Common-owned test file and `sync/BUILD.gn` are
not implicitly transferred with C. Use a disjoint native test source and
coordinate its Common GN registration, or obtain an exact test-hunk handoff;
do not edit the unified owner's shared fixture in parallel.

The unified owner retains Common C++/Golden/policy/schema and all matching Swift
code. Use the five committed files from `5885d01` for the target/capture values
and default-off UI bridge; subsequent Common WIP is not an include contract.
Do not reimplement their types or edit common headers in parallel.
Sequence: common format/goldens/fresh store,
matching Swift, native/Mobile binding, then one coordinated candidate E2E/test
wave. The separate 225df88/22e2f2b UI/compile baseline is not widened or restarted
for this package reservation. Shared checkout/out/build/sign/install/native UI
remain exclusively Desktop-owned; no new runtime/portal/key permission follows.

`dc01cb5` introduced isolated native target policy, not a runtime caller or
capability announcement. Desktop implementation `906dac8` now replaces its
duplicate enum/struct with aliases of `ahoi::sync::SharedTabTargetKind` and
`ahoi::sync::SharedTabTarget`, and adds the public dependency on
`//ahoi/browser/sync:shared_tab_types` in the owned Session GN target.
The four shared headers were read back unchanged from `5885d01`; its pure GN
leaf is unchanged even though other Common GN sections are WIP. No Common file
was edited. Native classification/validation/activation policy is unchanged.
GN formatting, pinned clang-format with Chromium's actual style, and scoped
diff checks passed. This is SOURCE-only A completion, not a compiled/live Sync
pass. No checkout/out refresh, build, installation, capture override, generation
publication or readiness activation occurred. B-D remain pending the genuine
Service/backend implementation handoff; an old vector fallback is not allowed.
The common layer must not depend on Session/UI. Do not edit Desktop's policy
files from the common-owner thread.

## Requested native API

The canonical all-entity fixture is separately frozen as `c3c3d20` at
`overlay/chromium/src/ahoi/browser/sync/testdata/sync_wire_v3.json`, SHA256
`f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00`.
Desktop independently read back byte identity, JSON syntax, 26 unique named
examples, all 13 explicit entity IDs, the frozen manifest's complete field maps
and four matching Page/Presence target pairs. Dispatch on `entity_type`, never
array position; `payload` is a compact JSON STRING, not an already decoded
record. Use this one resource for the coordinated C++/Swift acceptance, not a
native copy. These resource checks do not execute a codec, merge, signature
verification, capability publication or runtime. No baseline build was requested.

The value-type/default-bridge subset below is landed in `5885d01`; the Service
getter/Observer/publication and authority methods remain proposed shapes until
their separate completed source handoff. Do not treat this document as their
implementation:

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
  kDisabled, kNativeNotReady, kBootstrapPending, kUnsupportedFormat,
  kRecoveryPending, kCaptureDeferred, kInvalidCapture, kStoreError, kNone
};
struct SharedTabSyncState {
  bool projection_ready = false;
  bool write_allowed = false;
  SharedTabSyncIssue issue = SharedTabSyncIssue::kDisabled;
};

// Derived UI result only; never stored or serialized as new creator fields.
struct SharedTabProvenance {
  std::optional<base::Uuid> creation_device;
  std::optional<base::Uuid> saved_device;
};
```

The existing `url` and added target fields form one value, copied/validated
atomically. Absence represents unready/invalid capture, not an implicit web
write or a reason to start a legacy writer. `local_scheme` uses only the
allowlist confirmed in the final common contract; the existing eight-string
target-policy preparation is not expanded here. A local-only capture
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
do not become format-ready merely because this method or getter exists.

Extend the existing ProfileSyncUiBridge with default implementations:

```cpp
virtual SharedTabNativeSupport GetSharedTabNativeSupport() const { return {}; }
virtual void RequestSharedTabCapture(uint64_t generation) {}
```

Distinct method names avoid ambiguous `{}` overloads. Existing local
`PublishWindowTabs(vector)` / `RequestLocalTabCapture()` callers may be adapted
during the coherent source change, but the delivered candidate must have one
active format rather than permanent v2-only and v3-only entry paths. A missing
shared capture must not fall back to an old vector path or a second writer.
Reuse the existing service/bridge, transport, engine and native tab registry.

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
   an unqualified vector path nor deletes/rewrites domain rows, clocks or outbox entries.
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

`projection_ready` means a validated snapshot of the one supported format can
be consumed by the native projection. It does not imply writer permission.
Unsupported stored/incoming formats are rejected without rewriting or clearing
local/native data. False readiness never removes rows, switches focus or loads
a URL. Preserving bytes is not a requirement to implement old-client decoding,
projection, migration or interoperable writers.

`write_allowed` explains shared-normal-tab authoring authority under the same
format used by every other permitted entity, including Bookmark and Capability.
Backend still requires implemented common/native support, a normal profile,
the relevant global/category consent, current account/key/recovery authority
and a successfully initialized provider. Initial account/key/provider bootstrap
is distinct from the discarded v2-capability/legacy-peer transition protocol.
The exact Device/Capability validation and activation contract comes from the
Sync owner; do not recreate a complex old-peer upgrade matrix or indefinite
dual-format gates. Native support or a UI boolean alone never authorizes writes.
A matching whole-format implementation and fresh-store acceptance are required,
not an isolated version-constant bump.

Gate transitions must invalidate queued mutations and delayed projection/apply
replies; a cached UI preference or old status callback is not renewed authority.
Provider-side fencing remains an additional final transport boundary.

Creation provenance comes from a genuine v3 `created_at` field clock. Saved
provenance, for a persistent page, comes from its genuine `is_temporary=false`
field clock (explicit creation/save, not a promoted default). Resolve only known
DeviceRecords from the same immutable snapshot. Bottom/system, synthetic,
missing clocks or unknown device metadata yield no badge; never use the last
record editor. These derived IDs add no persistent/wire creator field. Preserve
genuine creation evidence across current-format merges, save/unsave and restart.
Keep the already committed Swift corrections 895daf9/f25eea5; they are source
fixes, not acceptance of the new format. Do not add a legacy-clock migration
pipeline just to generate provenance for obsolete fixtures.

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
  path leakage; unsupported-format input preserves the last accepted local state.
- Creation versus save badges, Bottom/missing/unknown-device cases and retained
  genuine local provenance across both merge directions and restart.
- One supported format across all permitted entities, explicitly including
  Bookmark and Capability; no active old writer or empty legacy-capture fallback.
- Matching fresh isolated macOS/iOS stores and exact candidates: an already-open
  peer receives normal temporary/persistent tabs without focus, eager loads or
  duplicate TreeNode identity; the Bookmark collection stays separate. Existing
  profiles/CloudKit records/keys are unchanged; no complex migration matrix.

First the exact candidate's visible journey, then meaningful programmatic
tests, or an explicitly documented technical-E2E exception. The existing six
native target-policy tests and old wire-v2 runs are preparation/baseline evidence,
not these capture/gate or unified-format proofs. Final header/default changes
wait for the Sync owner's simplified contract, without parallel Common writes.
