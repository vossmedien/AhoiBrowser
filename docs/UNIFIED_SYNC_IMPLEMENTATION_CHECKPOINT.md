# Unified sync implementation checkpoint

Updated 2026-09-05. Owner: `01a06d69-1034-7372-b784-0b05a53c87e0`.
This is an implementation continuation pointer, not a source-freeze or acceptance.

**New binding scope:** ADR 0010 adds actual Chromium-settings and extension
installation/configuration restore after linking a second Mac. Earlier five
Ahoi settings + inventory-only code is insufficient. The one-format decision
remains; new corresponding field maps/native hooks/fixture coverage are not yet
implemented. Cookies/passwords stay local; unknown extension storage is not
silently approved. The goal prompt now includes this expanded runtime acceptance.

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

## Current implementation WIP — not integrated or built

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
  needs its explicit allowed-main-frame hook; title fidelity and some pending-
  intent/recovery edge behavior remain for the runnable-candidate pass.
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
  as acknowledgment. These facts are NOT yet connected to final shared-tab
  readiness/capture. New ProfileSyncBackend callers/tests must explicitly supply
  their authorization; no implicit authorization is granted by the default.
- Mobile identity/passive projection source is now implemented in
  `MobileBrowserModels.swift`, `MobileBrowserControllerSharedTabs.swift` and
  bounded `MobileBrowserController.swift` lifecycle hunks. APIs: distinct
  `presenceID: TabID?`, safe `sharedTarget`, `participatesInSharedTabs`, local
  `MobileSharedTabBindingState`, `reconcileSharedTabs(snapshot:)`, and value-bound
  `bindTab(_:to: TreeNode)`. Automatic/recovery placeholders do not participate;
  dormant mirrors have no Presence ID and do not load WebKit. Existing runtime
  URL/selection/order survives passive updates. Root must still wire actual
  Repository/AppModel capture, distinct close/undo identities and before-unload
  handling; unavailable/deleted/missing bindings must not become inferred deletes.
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

1. **Common native capture/service/backend:** finish the advertised Service
   getter/Observer/provenance and `PublishSharedTabCapture` interface, real
   NativeSupport/readiness, original account/global-scope authorization and
   complete/deferred multi-window generations. Replace the existing HTTP-only
   `ReplaceLocalTabs` filter/delete path with the atomic primitive. Do not prune
   accepted IDs before commit or turn detached/missing windows into deletion.
   A closed Presence never deletes its shared TreeNode. Passive runtime capture
   must not undo a newer logical Page URL that was deliberately not auto-loaded.
2. **Capability/live projection:** implement control publication and genuine
   provider/bootstrap acknowledgment facts. Default-false native support is
   not yet connected; no automatic feature declaration exists. Preserve
   out-of-order dependent records and validate Page/Presence before projecting.
   `DeviceTabsService` still has its old HTTP-only presentation filter and must
   adopt the shared target/linked-page boundary. Backend/store admission must
   match Swift's independently known Device/Capability dependency handling.
3. **Mobile live binding:** `CompanionStore.publishLocalMobileTab` and
   `CompanionAppModel.reconcilePublishedMobileTabs` now have the current Page/
   Presence input and preserving atomic capture described above. Finish the
   same-frame user-navigation hook, verify move/reorder across opaque native
   sort keys, title/custom-title fidelity, pending/restored intents and the
   before-unload/recovery/undo cases. Explicit save uses the deterministic page
   ID even before first capture; unsave still needs its actual user-action
   wiring. Do not reintroduce the removed URL-filter/absence-delete loop.
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
