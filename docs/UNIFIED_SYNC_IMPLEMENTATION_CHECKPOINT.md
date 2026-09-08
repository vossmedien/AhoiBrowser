# Unified sync implementation checkpoint

Updated 2026-09-08. Owner: `01a06d69-1034-7372-b784-0b05a53c87e0`.
This is an implementation/source-handoff pointer, not product acceptance.

## Common native code handoff — 2026-09-08

The Common C++ Service/backend capture and receipt-backed native tree pipeline
are now implemented, including their current-format model/store/provider
dependencies. This is a SOURCE-only integration package: no compiler, app,
profile, CloudKit host or test was started here. Matching Swift remains owned
here and WIP. Desktop keeps Native B-D and the shared build/install/UI lease.
Do not insert this package into its separate browser-baseline snapshot.
Exact committed/pushed Common package: `e2f67111fcb02f08eabe44b6fbac52f0afb3a57b`,
55 scoped files with DCO sign-off. Direct Desktop handoff
`01a07f89-47a8-7201-bc72-74e83e919f66`; coordinator handoff
`01a07f89-4819-7c53-a906-b461716ab9d4`. No Common WIP remained after that commit.

The exact callable interfaces are in `sync/profile_sync_service.h` and
`sync/profile_sync_ui_bridge.h` (paths below `overlay/chromium/src/ahoi/browser/`):

- `RequestSharedTabCapture(window_key)` registers a window/invalidates its old
  capture after a native mutation. `PublishSharedTabCapture(window_key, capture)`
  answers the issued generation; every registered window must answer complete.
  `RemoveWindowTabs(window_key)` detaches without inferring a close-all.
- `shared_tab_sync_state()`, `GetSharedTabProvenance(tree_node_id)` and default
  observer `OnAhoiSharedTabSyncStateChanged` expose derived state, not write
  authority. Native support remains explicit/default false. Closing a window
  cannot retract a previously admitted device capability and block other peers.
- Native implements `GetSharedTabNativeSupport()` and
  `RequestSharedTabCapture(generation)` using the existing profile bridge.
  The old vector and unqualified tree backend methods are non-writing seams;
  they are not a fallback writer. Replace their native callers in B-D.
- **Required Native-B receipt seam:**
  `ExportTabTreeSyncSnapshot(TabTreeSnapshot*, std::string*)` returns the COMPLETE
  CURRENT profile tree plus its opaque baseline receipt atomically and durably;
  false defers while loading/local persistence is pending. Do not substitute an
  older durable snapshot for newer RAM edits. Re-notify the existing snapshot
  callback once that same current revision is durably committed. Concrete risk:
  native RAM B -> Common observation/outbox B -> crash before native Disk B ->
  restart A would otherwise be misread as a deliberate local undo. No new field
  or auxiliary transport is needed for this existing export boundary.
  `ApplySyncedTabTreeSnapshotWithReceipt(snapshot, receipt, authorization, completion)` must
  persist receipt+tree in the same native transaction, retain receipt on ordinary
  local edits/undo and carry the original authorization through persistence.
  Check it before apply/commit; do not renew it after an asynchronous hop.
  Its asynchronous Result completion reports durable success, not only RAM
  publication. Local callbacks stay live during the disk wait and revoke stale
  apply; Native suppresses only the Sync-origin callback at final publication.
  The defaults fail closed. Common checks the exact tree+receipt readback after
  completion. This corrects the first synchronous handoff, following Desktop's
  concrete request `01a07f98-70b9-7660-a620-ec057c927a3b` and its `cc7e7e3` envelope.
  The asynchronous correction is committed/pushed as
  `5e74472b3950862d13e94fa5ac0233ef13fafc7e`, direct handoff
  `01a07fb9-905e-75f2-bf74-da6884314e15`. Common no longer converts a temporarily
  deferred Export into missing implementation support; capability admission
  and snapshot durability are separate, so startup cannot latch support off.
  This is local crash-safety metadata, NOT a new wire field, storage partition
  ID, profile path or cloud migration. Desktop request: `01a073b2-528f-7693-a596-d8dda453f100`.

Backend capture now binds its original provider/account/key scope at Begin,
rechecks the current gate before one authorized SQL batch, and retains committed
window/key ownership independently of delayed UI replies. Partial/missing/stale
captures preserve data and identity maps. Retained+new rows are checked together
for duplicate Presence/Page IDs. Presence and global Tree deletion stay separate.
`DeviceTabsService` now uses linked Page/target consistency, not an HTTP-only
filter that loses local-only/new-tab metadata. Actual Device/Capability ACKs and
initial-fetch completion drive readiness; an empty outbox is not acknowledgment.

`native_tree_sync_journal.{h,cc}` compares native value groups to durable local
observations in the existing SQLite database, and commits actual changed groups,
outbox and observations together. Thus a title edit cannot re-author a stale
remote URL. Prepared, unique projection receipts close the native-apply/restart
gap; unused receipts cannot become the native baseline. Complete native
Undo(Create) removals of previously observed live IDs become atomic tombstones;
missing window captures do not. Initial pre-link creation/saved provenance stays
unknown instead of inventing a creator device. Both raw-profile and original
provider authorization continue to fence bookmark and general pump paths.

Verification so far: bounded source/API reviews resolved original-scope capture,
retained-ID and native Undo(Create) findings; actual pinned Chromium clang-format,
GN formatting, scoped whitespace and the 800-line source budget were checked.
These checks are not build, codec, CloudKit or E2E passes. No optional test-matrix
expansion is a prerequisite. Next: Desktop B-D implementation against these
interfaces, matching Mobile live completion, then the runnable combined candidate
and representative visible E2E before minimal relevant programmatic checks.

Both current global and project `AGENTS.md` were read on 2026-09-08. Resource
coordination now assesses sustained total machine capacity; the obsolete single
80%-process gate and special Chromium priority are not current user policy.

**New binding scope:** ADR 0010 adds actual Chromium-settings and extension
installation/configuration restore after linking a second Mac. Earlier five
Ahoi settings + inventory-only code is insufficient. The one-format decision
remains; new corresponding field maps/native hooks/fixture coverage are not yet
implemented. Cookies/passwords stay local; unknown extension storage is not
silently approved. The goal prompt now includes this expanded runtime acceptance.
The subsequent workspace-settings packet also covers ordered extension action
pins by existing Workspace ID and, only if needed by Desktop's isolation design,
a logical website-session assignment. `docs/WORKSPACE_SESSIONS.md` remains the
native owner contract. No local partition/profile ID, path, permission grant,
cookie, storage content or account context is a portable setting; remote moves
must not switch a running tab's local account. These metadata extensions are
required follow-up implementation, not added speculatively to this base handoff.

## Binding scope and delivered code

ADR 0009 / `config/sync-format.json` implement the direct pre-launch user
decision: ONE active format 3 for all 13 permitted classes on C++/macOS and
Swift/iOS. No complex development-data migration or permanent mixed writers.
Existing profiles, Arc recovery data, CloudKit data and keys remain untouched.
This owner now also owns `spikes/cloudkit` and `apps/AhoiMobile`; the former
Mobile owner coordinates/reviews only. Desktop owns Native Tree/Session/UI,
`tab_tree_sync_adapter`, Chromium checkout/out/build/install/runtime.

Three separate committed/pushed handoffs do not release the remaining WIP:

- `92694fe36539d024af6567646103f1cf246d5364`: the one-file legacy-fixture
  `std::array` compiler correction. Direct Desktop handoff
  `01a072dc-c1bf-7f33-bd5a-884b16e58530`; no new-format product code.
- `5885d01`: five common native leaf/capture-type files, including the isolated
  `:shared_tab_types` GN target and default-false UI-bridge methods. Direct
  Desktop handoff `01a072ee-d5d3-71b3-b6f4-c28edfd1911e`. Native A can alias the
  committed common target types. Service/backend capture implementation is NOT
  included; B-D remain Desktop-owned pending its concrete completed API handoff.
- `c3c3d20`: canonical `sync/testdata/sync_wire_v3.json`, 26 examples / all 13
  classes, plus explicit fresh namespace/metadata contract. Resource SHA256:
  `f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00`.
  Direct Desktop resource handoff `01a07303-b127-75d3-9107-0d39580999ac`.
  Entries use `records[{name,entity_type,data_class,payload}]`; `payload` is a
  canonical compact JSON STRING and array position is not an entity ID.

The signed checkpoint follow-up attests the author's DCO for these three
AI-assisted contributions. Published commits are not rewritten; this does not
claim that an automated per-commit DCO check on their historical trailers ran
or passed. Subsequent commits must use `git commit -s`.

## Mobile candidate — product build succeeded, visible acceptance open

Swift package `4e64c5f2f4e6052c1a4aefb2d6b9c6617cf76963` is signed/committed/pushed.
First product-only build Session `1526` ended EXIT 65 at one stale external-link
navigation caller. Exact one-line-route correction is `bba0b86ad2a4b67fe0c6ff0ca763a3ca1e6bfabb`.
The subsequent same-target incremental build Session `11655` is terminal EXIT 0:
DebugLocal 0.1 (16), generic arm64 iOS Simulator, Xcode 26.6/17F113, two build/Swift
jobs. App+Core+Shared Swift compiled/linked and ad-hoc signed; no test target ran.
The only tool warnings were AppIntents extraction skipped because these targets
do not depend on AppIntents; no compiler warning suppression was introduced.

`artifacts/build/mobile-unified3-4e64c5f-20260908/` retains both raw logs/result
bundles, the original red classification and `candidate-bba0b86.json`. Built plist
matches exact source/Build16/DebugLocal and codesign deep/strict verification
succeeded. Receipt app-tree hash is `153d0d7d5b339c12424aa22d427ce9b7716c45e779b8fcd8e30bb68b1e250fad`.
Its existing receipt tool uses domain-separated artifact hashes, not plain
`shasum` file hashes. No installation, Simulator/My-Mac/App start, test or real
CloudKit mutation occurred. Desktop still owns the UI; its short Simulator slot
was requested and is NOT inferred from a successful build or idle processes.
Keep `/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/repo` frozen at `bba0b86` and its
current DerivedData app unchanged for the initial visible journey.

## Current implementation state — remaining integration, not Sync acceptance

- Mobile Save/Unsave now uses one Page/Presence/domain commit with rollback,
  preserving stable Page ID and authoritative bound target/title. Tab-switcher
  Save/Make Temporary is wired to it; dormant metadata edits create neither a
  Presence nor WebKit instance. Late UI callbacks check the original Presence
  identity. `performLocalFirstMutation` keeps the original runtime generation/
  Bridge through persistence; already queued intents are awaited inside it.
- The allowed main-frame WebKit callback is now connected to page-identity-
  checked navigation intent. Address/new-tab/back/forward and in-page links/forms
  acquire a document generation; committed or genuinely failed user navigation
  can publish its safe target. Passive restore and automatic recovery do not.
  Real document title is passed with the navigation/finish event. Cancellation,
  pending restored/deferred intents and runtime safety still need the candidate
  journey; this is not acceptance from source inspection.
- The real Xcode project was generated from `project.yml` on 2026-09-08 (36
  added project entries, no Info.plist delta). A provider-free DebugLocal
  product-only iOS Simulator build is next, capped at two jobs following fresh
  aggregate capacity checks. No test-only binaries are an app-build prerequisite.
  Desktop was notified in `01a07f9d-3b4a-7062-98c6-90b39e80a685`; native Desktop
  app/UI is untouched and visible Simulator E2E needs its explicit short slot.
- Mobile live publisher now uses `CompanionMobileSharedCapture` and
  `CompanionMobilePresenceStore`: complete local upserts commit once, Page IDs
  derive stably from local Device/Presence identities, unassigned pages use the
  canonical Inbox, all Presence targets come from the actual repository Page,
  and missing input no longer infers close/delete. UI callers bind the returned
  IDs before publishing the refreshed model. Revoked Devices are not silently
  reactivated by a heartbeat.
- Explicit close in both main UI and tab switcher commits through
  `CompanionMobileSharedClosing` before runtime removal. Temporary Page deletion
  and Presence deletion are separate; saved Pages remain. Close-before-initial-
  capture resolves the same deterministic page ID, and late callbacks check
  the original Presence ID. Undo rotates Presence and does not automatically
  reuse a deleted temporary page. These paths are source-only, not verified.
- `MobileSharedTabIntent` and the native Controller callback now distinguish
  explicit navigation/move/rename from passive projection. The AppModel
  serializes per-runtime intents, applies them through
  `CompanionMobileSharedIntent`, and protects pending intent rows from stale
  projection. The ViewModifier coalesces sync-relevant capture changes without
  favicon/progress-triggered publication. Same-frame link navigation still
  is now wired through actual allowed-main-frame/commit events; pending-
  intent/recovery edge behavior remains for the runnable-candidate pass.
- A concrete review found restart duplication between domain persistence and
  browser-binding persistence. The AppModel now restores exact deterministic
  bindings BEFORE passive mirror creation and reserves still-pending page IDs;
  it does not deduplicate by URL or discard a normal runtime row. Only source
  review/diff/line-budget checks have run, no new test or build.
- Additional live-integration source since `3e9552f`: profile-wide original
  authorization is now passed from Service through Backend into the Mac
  provider; opt-out/shutdown cancels it synchronously. Provider callbacks and
  SyncPump recheck the same scope after asynchronous hops for every data class.
  `SyncStore` writes actual acknowledged-record versions plus initial-fetch
  completion; recovery clears those facts instead of treating an empty outbox
  as acknowledgment. These facts now drive common shared-tab readiness/capture.
  New ProfileSyncBackend callers/tests must explicitly supply
  their authorization; no implicit authorization is granted by the default.
- Mobile identity/passive projection source is now implemented in
  `MobileBrowserModels.swift`, `MobileBrowserControllerSharedTabs.swift` and
  bounded `MobileBrowserController.swift` lifecycle hunks. APIs: distinct
  `presenceID: TabID?`, safe `sharedTarget`, `participatesInSharedTabs`, local
  `MobileSharedTabBindingState`, `reconcileSharedTabs(snapshot:)`, and value-bound
  `bindTab(_:to: TreeNode)`. Automatic/recovery placeholders do not participate;
  dormant mirrors have no Presence ID and do not load WebKit. Existing runtime
  URL/selection/order survives passive updates. Root must still wire actual
  pending/restored-intent and before-unload handling; actual Repository/AppModel
  capture and distinct close/undo identities are implemented. Unavailable/deleted/
  missing bindings must not become inferred deletes.
- C++ current-only model 3 / SQLite schema 6, all 13 variant/discriminator
  branches, exact field maps, lossless HLC UInt32 and canonical UUID/time parsing.
  Field value dispatch is extracted into `sync_field_values.{h,cc}`; targets
  are atomic URL/kind/scheme values. Capability validation lives in
  `sync_unified_validation.{h,cc}` and is included in backend state readback.
- Swift central `SharedSyncFormat`, matching strict codecs and current-only
  field merges. Tree `is_temporary` and Presence `tree_node_id` are regular
  explicit fields, not legacy defaults. Creation provenance is derived from
  its actual field clock, not an old Boolean/last-editor marker. Timestamp
  values compare independently from their register's device/counter metadata.
- Canonical numeric parsing now agrees on integral JSON doubles as well as
  integer tokens. Both enforce the existing Int32/UInt32 bounds. History device
  identity is mandatory; command optional UUIDs are validated even in branches
  where unused. Command issued/expiry values use exact milliseconds, not silent
  microsecond truncation; ordinary timestamp values require Unix epoch or later.
  Bookmark creation remains positive raw Int64 Windows microseconds, including
  pre-1970 values and the explicit Int64 boundary fixture.
- Fresh names: `Ahoi Sync/sync-format3.sqlite`, `cksync-format3.state`,
  `AhoiMobile/SyncFormat3/snapshot-format3.json`, zone `AhoiBrowserSyncV3`.
  Swift snapshot marker and Desktop cache marker reject incompatible existing
  bytes without overwrite. The Mac provider's old decoded-bookmark-cache
  migration path is removed; current encrypted bookmark retention/consent stays.
- Native bookmark observation baselines now have a dedicated LOCAL content
  representation (`bookmark_native_observation.*`), with no fake `native` clock.
  Actual apply receipts still contain the real current-format wire record.
- `SyncStore::PutLocalBatch` is a transaction-bound common primitive with an
  original-scope authorization check before commit, whole-batch rollback,
  combined Tree/Bookmark graph checks and one post-commit observer notification.
  It is not yet wired to the native capture path. Idempotent single writes keep
  `kAlreadyApplied` even if a related Page/Device changed later.
- New C++ store / canonical wire tests and Swift canonical wire/provenance
  tests are written. `project.yml` and GN reference the same canonical resource.
  No Xcode generation, compiler, test, app, provider host or UI action ran here.

Read-only reviews found and source-corrected the two missing `ReadUuid` bool
arguments, a single-write idempotence regression, missing combined graph
validation, integral-number parser differences, the optional History device
gap, and ignored unused command UUIDs. These are source findings/fixes, not
executed regressions. Only resource JSON/field-map/hash checks, scoped API
readback and `git diff --check` have run in this implementation wave.

## Exact next implementation work

1. **Native integration:** Common capture/getter/Observer/provenance, backend
   complete/deferred batches and receipt-backed projection are implemented above.
   Desktop implements Native B-D including the local receipt seam; no native
   file has been taken over. Current default bridges cannot activate the feature.
2. **Matching live admission:** complete/verify Swift's actual Device/Capability
   publication/ACK/readiness wiring against the implemented C++ path. Preserve
   pending dependencies; source-only preparation and the old Relay are not the
   matching live roundtrip or permission to reuse old profile/CloudKit data.
3. **Mobile live binding:** `CompanionStore.publishLocalMobileTab` and
   `CompanionAppModel.reconcilePublishedMobileTabs` now have the current Page/
   Presence input and preserving atomic capture described above. Finish the
   remaining pending/deferred user-navigation cases, verify move/reorder across opaque native
   sort keys, title/custom-title fidelity, pending/restored intents and the
   before-unload/recovery/undo cases. Explicit save uses the deterministic page
   ID even before first capture; Save/Unsave now has real user-action wiring.
   Do not reintroduce the removed URL-filter/absence-delete loop.
4. **Retire obsolete preparation code/tests:** `SharedTabFieldReadMerge.swift`
   is no longer a live dependency but still contains old mixed-version helpers.
   Old `SharedTabCreationProvenanceTests` / `SharedTabFrozenContractTests` still
   reference the removed promotion API; these are known source-compilation
   obligations, not preserved migration requirements. Update the other older
   test fixtures to current valid clocks/IDs/targets without blindly changing
   pure HLC algorithm tests or unrelated app/storage schema numbers. Preserve
   historical fixtures/evidence as history or unsupported-input probes.
5. Finish compiler-required source/fixture/project consistency, then deliver
   one coherent exact source freeze. Optional extra assertion coverage waits
   behind the actual candidate journey. Native/Common production activation is
   not authorized merely by the currently committed leaf declarations.
6. Coordinate build/runtime/CPU gates with Desktop. Visible representative
   candidate E2E first, then focused suites and the matching C++/Swift/real
   provider roundtrip. Old Build15 Bookmark proof is not this wave's proof.
   Production/portal/key actions and the Chromium roll remain separate gates.

## Runtime truth

Desktop's independent baseline `92694fe` built successfully as session17302.
The coordinator has independently verified its installation receipt and
executable, then recorded a RED startup journey. Desktop owns the toolbar/widget
lifecycle correction (`88ebbe9`); do not start a duplicate launch/build or use
that failure as an invented unified-Sync verdict. Its live checkpoint/receipts
remain authoritative for the next native runtime slot. This WIP is excluded
from that baseline. No default-profile Arc or other runtime state was changed
by this owner in the unified-format implementation wave.
