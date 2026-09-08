# Unified sync implementation checkpoint

Updated 2026-09-08. Owner: `01a06d69-1034-7372-b784-0b05a53c87e0`.
This is an implementation/source-handoff pointer, not product acceptance.

## Existing mobile host-label layout follow-up — 2026-09-08

The actual Build17 navigation.png shows example.com truncated to `exa…` because
five44pt actions plus capsule icons/padding squeeze its96pt minimum. The existing
MobileHarborControlsLayout now reserves180pt for the address, using its already
implemented stacked arrangement on a narrow expanded phone. Hit targets, stable
view identities, compact collapse and accessibility layout remain; no new layout
architecture. Long addresses truncate in the middle and retain the complete
accessibility value/open-address surface. Source only; the frozen Build18/UI
slot is unchanged. Verify readability and expanded/compact navigation in the
next suitable candidate, not a new test matrix or replay of old Save evidence.

## Concrete native extension setup seam — 2026-09-08

`ProfileSyncUiBridge::{ReadNativeExtensionSetup,ApplyNativeExtensionSetup}` now
has code-owned DTOs in the small `:extension_setup_types` target and out-of-line
default-closed definitions. Native receives an original authorization lease,
unique operation ID, exact revision and trusted-source/install/enable tuple;
completion distinguishes real apply from pending download, required permission,
unsupported, policy-blocked, failed and cancelled states. No new Native file was
edited. Desktop can implement its accepted installer/enable scope against this
concrete header rather than uncommitted future model files.

Matching typed C++/Swift codecs represent desired setup separately from observed
inventory, through the existing format3 PermittedSetting class with a strict
`ahoi.extension.<id>.desired` namespace and atomic `value_json`. No new class,
SQLite migration or arbitrary package URL. The ordinary preference catalogue
does NOT admit this namespace yet: separate setup consent, origin-aware local
intents, Native completion orchestration and canonical fixture binding must be
integrated before publication. This is an implementation seam, not full feature
activation or an E2E pass. Exact semantics: EXTENSION_SETUP_SYNC.md.

## Concrete Home coupling correction — 2026-09-08

The reported `homepage_is_newtabpage`/excluded-homepage mismatch is confirmed:
the selector alone would activate B's local target rather than A's configuration.
It is removed from BOTH positive C++/Swift catalogues; local targets/records are
preserved. The independent `browser.show_home_button` and Ahoi toolbar-pin pref
remain supported. No raw Home URL, schema, migration, icon, native UI or build
change. Current source supports23 real preferences plus the native search choice.
The frozen Build18 candidate is unchanged; its search-journey evidence cannot
claim this later catalogue correction. Relevant source/GN/whitespace checks only.

## Search-engine mapping and Mobile provider continuation — 2026-09-08

Committed/pushed source:29c42dbfafb095da7e091668ad728dd4b6ad64a1. Its first Mobile
product build95119 ended EXIT65 at one file-private Array-helper call; the
preserving one-file correction894c9a2 is committed/pushed. Corrected product
build48865 on the same clean snapshot ended EXIT0. DebugLocal0.1(18) source,
project, plist and signature are independently bound in
artifacts/build/mobile-browser-settings-29c42db-20260908/candidate.json; app tree
bdeb580e666a4462a5cd541e45d99ab7c41402510f264b1d6eab93243c70a111.
Original failure and corrected logs/XCResults are retained beside that receipt.
No tests or UI ran for Build18. Fresh UI coordination request
01a080b0-f630-7690-a7a5-4d46edc1f366 explicitly includes the other Simulator
projects; Desktop was informed in01a080b0-f66a-70f0-ade9-5e41f3437e74.

The delayed 07:08 Simulator offer is already consumed and returned; the current
checkpoint/evidence and fresh device readback still say Shutdown. No old
Build16/17 UI steps were repeated. Native c20a759/its corrective UI work stays
Desktop-owned. The current source continuation adds one genuine shared setting:
TemplateURLService's supported built-in search choice on Mac and the existing
native iOS picker, alongside the previous 24 preference entries. No peer URL,
custom-engine overwrite, Google Sync processor or eager tab navigation.

Mobile browser-settings consent now reaches the actual provider/cache/direct
enqueue/seed/domain-result/final CKRecord callback; original per-ID leases and
monotonic UI intent epochs prevent off/on revival. Native user choice commits
in the local repository before AppStorage projection, with unchanged field
clocks retained. Existing settings UI is split into a small browser section to
stay under800 lines, with German/English labels. No new wire field or version.
Details and remaining wider scope are in BROWSER_SETTINGS_SYNC.md.

No test suite or runtime is claimed for this source. The product-only build is
now complete; next is a NEW coordinated Simulator window for the changed
search/settings journey. The old returned UI slot is not reopened. Desktop retains Chromium
checkout/out/build/install; no My-Mac, profile, key, Portal or Production action.

## Browser settings packet — 2026-09-08

Committed/pushed source: `41de599f94f92688e7c0fca5f947402972f83b49`, 37 scoped
files with DCO. Native UI/API handoff `01a0806a-8620-7590-9485-c2bf7f9ab028`;
coordinator update `01a0806a-8661-71b1-9f4a-d6142b8d0ec8`. No Native source or
shared checkout/out was changed. This packet does not interrupt Desktop's
separate candidate-bound UI journey or authorize an uncoordinated build.

Native setting capture/apply now uses an explicit 24-entry checked catalogue,
real USER values, default-reset intent and original-version local recovery
payloads. Per-setting revocation is connected through C++ backend, outbox pump,
Mac direct upload, delayed CKRecord delivery and ACK. The previous unversioned
setting writer/opt-out tombstone is removed. Matching Swift recognizes the same
IDs/types and filters outgoing, seeding and merge reenqueues; unknown metadata
remains stored. No current wire fields/version or SQLite schema changed.
Concrete native category UI methods, exact supported/excluded scope, verified
existing USER-layer observer behavior, Mobile apply/provider gaps and E2E-first next
step: [BROWSER_SETTINGS_SYNC.md](BROWSER_SETTINGS_SYNC.md).

Only pinned formatting/GN, Xcode project generation and scoped source checks
have run for this packet. There is no new product build/test/E2E claim. Desktop's
separate corrected c20a759 candidate and Mobile's Build17 remain their own
evidence. Settings/extension restoration, iOS mappings and workspace metadata
are not complete merely because the first catalogue and settings pipeline exist.

## Native app-first compiler correction — 2026-09-08

Exact correction `dfcc32edf01b480f0a4b462c0509443610cb0fc9` is pushed; direct
Desktop handoff `01a08043-d2a3-7c33-955d-51b46679cfd2`. The old compiler blocker
is resolved in source and must not be reopened as a missing ownership handoff.

Desktop's app-only build71760 on74ceb15 is terminal EXIT1. Its Common causes
are corrected separately from the ADR0010 setting-catalog WIP: the three
nonempty default virtual methods move unchanged into profile_sync_ui_bridge.cc
(registered in its existing GN target), the Backend destructor is marked
override, and the private PutLocalRecordInTransaction no longer reacquires its
caller's already-held sequence context. Both public callers retain their
DCHECKs and the private VALID_CONTEXT_REQUIRED annotation remains. No warning,
authorization, schema or behavior assertion was relaxed. Scoped whitespace/GN
checks only; no build, test or runtime was started here. Desktop owns the one
cached corrective build including its separate native5a15614 correction.

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
  Native's committed `1db7688` adds `TabTreeStore::Result::kCancelled` for its
  authorized disk-first path. Common now maps that completion to deferred,
  preserving state and avoiding a false storage-error indication. Real store
  failures remain errors; no immediate retry loop or early success is added.
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

Canonical system Inbox bootstrap now matches Swift exactly in the native
journal: first-not-found reserved Inbox ID, name `Inbox`, empty icon, sort `0`,
absent accent, both times UnixEpoch and not tombstoned receives a Bottom record
clock and all seven Bottom field clocks. It does not call the user's HLC Tick.
Other tuples and genuine later edits retain normal mutation clocks. A focused
source regression checks the complete Bottom map/no user-clock advance, then a
real rename and a late default merge that cannot overwrite it. This correction
and regression are source-only; no native build/test slot was taken and no
existing profile or Native Store/adapter file was edited.

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

## Mobile candidate — bounded Build17 Save/Restart PASS, other gates open

Current candidate is `7b706a73f98802fe31b206731b7e88c1da6b6c59`, DebugLocal0.1(17).
Its product-only build Session `79364` is terminal EXIT0. Embedded source/build,
deep/strict signature and clean-source/Xcode-project receipt are bound under
`artifacts/build/mobile-shared-intents-7b706a7-20260908/`. App tree artifact hash:
`d84c635869568ef1a90b809895d3571b148a75d814dcc41bd3842346a24dab87`.
The app was installed into fresh Simulator `F8253C50-E423-4424-8EE3-5F152C593A31`
and matched to the exact receipt. Visible Navigate -> canonical Inbox -> Save ->
OS/App restart passed with one saved tab and stable distinct Runtime/Presence/
Tree IDs; full Workspace/Tree values and clocks remained unchanged on restart.
Evidence: `artifacts/e2e/mobile-shared-intents-7b706a7-20260908/README.md`.
Unsave UI remains untested because CUA did not open the long-press menu. After
the restart proof, a final Simulator SaveScreen/Home call switched to the foreign
MindBodyCompass window; attribution is unproven, UI was immediately stopped and
the possible effect reported. Only the own simulator was then explicitly shut
down. UI handback `01a0801b-0086-71f0-97f5-46f712d925ad` closes this slot; do not
reuse it for more UI. Four existing Shared Swift boundary cases then passed on
the CLI after current-format test-only adaptation, not as a substitute Unsave/
CloudKit/Chromium pass. The own temporary source snapshot remains frozen.
Build16 was preserved byte-for-byte and verified
at `artifacts/build/mobile-unified3-4e64c5f-20260908/AhoiMobile-bba0b86.app` before
incremental product-cache reuse. Its historical evidence follows below, not a
new Build17 acceptance. No default profile or Desktop lease was touched.

Global/project AGENTS were reread fully after the latest September8 update:
finish already-authorized preparation before asking approval, reuse existing
same-scope approvals, continue unaffected work and infer no extra skill gates.
Actual source/UI ownership and E2E-first still apply. The granted short Simulator
slot was used and explicitly returned as recorded above; its approval was not
requested again. Further UI work must coordinate the shared Simulator surface
to avoid the observed cross-project window switch, not invent a new skill gate.

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
This Build16 source is historical; use the current Build17 source/product above
for acceptance of the material deferred-intent correction.

## Current implementation state — remaining integration, not Sync acceptance

- Mobile follow-up now retains up to three coalesced local mutation intents
  (navigation/location/title) on each normal runtime row. A missing/restored
  binding defers instead of dropping an action. Matching current domain identity
  resumes the queue without overwriting pending local fields or allocating a
  Presence for dormant metadata edits. Private records clear all pending data.
  Exact mutation IDs are committed atomically with domain changes; a crash before
  runtime acknowledgment cannot replay that old action over a newer peer value.
  Existing session-flush completion permits conservative receipt pruning: only
  IDs known before the flush and absent from that persisted pending set are
  removed. A failed flush retains receipts; a later/new mutation is not pruned.
  This is local operation bookkeeping, not a new wire entity, creator field or
  migration. The built `bba0b86` candidate is being preserved before a new
  product-only candidate for this material correction; no visible pass is claimed.
- ADR0010 integration exploration found the smaller actual PrefService seam:
  `Preference::registration_flags()` plus `GetUserPrefValue()` and
  `user_prefs::PrefRegistrySyncable::{SYNCABLE_PREF,SYNCABLE_PRIORITY_PREF}`.
  A positive privacy/value catalogue can use `components/prefs` +
  `components/pref_registry`, without importing the large Chrome Sync/UI target
  or taking over Google Sync. Reset uses native `ClearPref`; initial missing USER
  value is not reset intent. This is an implementation input, not a claim that
  the catalogue/native setup restoration is finished. No speculative GN cycle
  is claimed and no new test/review matrix was started.
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
