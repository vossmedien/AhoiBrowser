# Chromium patch ledger

`series` is the authoritative application order. Every patch must have a ledger
entry below before it is accepted.

Required fields: patch filename, owner, upstream Chromium version/commit,
affected paths, rationale, rejected alternatives, tests, security/privacy
impact, expected rebase risk, and removal/upstream plan.

## `0001-ahoi-vertical-tabs-default.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`.
- **Affected paths:** `chrome/browser/ui/tabs/features.cc` and
  `chrome/browser/ui/tabs/tab_strip_prefs.cc`.
- **Rationale:** use Chromium M151's native Views vertical-tab implementation
  by default and make fresh Ahoi profiles start in vertical mode.
- **Rejected alternatives:** a parallel sidebar/tab implementation, bypassing
  `VerticalTabStripStateController`, or removing the horizontal mode.
- **Tests:** `tests/repository/test_vertical_tabs_patch.py` constrains the patch
  to the two intended default changes; the patch must also pass `git apply
  --check --whitespace=error-all` against the exact pinned checkout.
- **Security/privacy impact:** none. The patch changes browser chrome defaults
  only; profiles, storage, renderer isolation, networking, and permissions stay
  owned by Chromium.
- **Expected rebase risk:** low but source-sensitive; the two upstream default
  declarations may move or be removed on a Chromium roll.
- **Removal/upstream plan:** re-evaluate on every roll. Remove the feature
  default override when Chromium's launch feature is enabled by default; retain
  an Ahoi-owned fresh-profile default only while vertical mode remains the
  product default.

## `0002-ahoi-product-data-directory.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`.
- **Affected paths:** `chrome/app/app-Info.plist`.
- **Rationale:** set the outer application bundle's `CrProductDirName` from
  Chromium's existing `${CHROMIUM_SHORT_NAME}` plist substitution. Ahoi's
  pinned `PRODUCT_SHORTNAME=AhoiBrowser` branding therefore gives the browser
  its own `~/Library/Application Support/AhoiBrowser` profile root and matching
  `~/Library/Caches/AhoiBrowser` cache root instead of Chromium's fallback.
- **Rejected alternatives:** hard-coding `AhoiBrowser` in the upstream plist,
  adding an Ahoi-only runtime fork in `chrome_paths_mac.mm`, forcing
  `--user-data-dir`, or relying on release-signing-time distribution mutation.
  Each alternative either duplicates Chromium's established seam, breaks
  ordinary launches, or makes dev and release builds disagree.
- **Tests:** `tests/repository/test_product_data_directory_patch.py` freezes the
  single-file/two-line materialized delta and its Ahoi branding contract. The
  patch must also pass `git apply --check --whitespace=error-all` in series order
  against the exact pinned checkout; the resulting plist must pass
  `plutil -lint`. The M151 branding/GN chain must still map `PRODUCT_SHORTNAME` to
  `CHROMIUM_SHORT_NAME` for `chrome_app`. A static GRIT exclusion gate keeps
  the filesystem path independent of localized resource strings.
- **Security/privacy impact:** positive isolation boundary. New Ahoi profiles,
  cookies, storage, extension state, and caches no longer collide with an
  installed Chromium profile. The patch does not migrate or inspect existing
  data, add telemetry, change sandboxing, or alter incognito storage behavior.
- **Expected rebase risk:** low but source-sensitive; the outer app plist or its
  existing `CHROMIUM_SHORT_NAME` substitution may move during a Chromium roll.
- **Removal/upstream plan:** re-evaluate on every roll. Remove this patch if
  Chromium exposes a first-class branding value for the default product data
  directory; otherwise retain the outer-bundle key and stable Ahoi short name.

## `0003-ahoi-nested-tab-tree.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`.
- **Affected paths:** new `ahoi/browser/tab_tree` target and the SQL metrics
  histogram registry.
- **Rationale:** provide an Ahoi-owned, durable, arbitrarily nested
  workspace/folder/tab tree with transactional move, delete and undo semantics.
- **Rejected alternatives:** overloading Chromium tab groups, storing the tree
  in bookmarks, or creating a second renderer/WebView hierarchy.
- **Tests:** eight focused store tests are included; the patch was statically
  checked against M151 and is compiled and executed by the integration build.
- **Security/privacy impact:** profile-local SQLite data only; no network,
  telemetry, credential or incognito synchronization path is introduced.
- **Expected rebase risk:** low outside Chromium SQL/base API changes.
- **Removal/upstream plan:** retain as the Ahoi domain layer while keeping the
  storage adapter narrow enough to replace during future Chromium rolls.

## `0004-ahoi-workspace-navigation.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** new `ahoi/browser/navigation` target and one
  `chrome/browser/ui/BUILD.gn` dependency.
- **Rationale:** define real workspace state, local command results and a
  deterministic Magic-Mouse/trackpad swipe state machine independent of tab
  groups.
- **Rejected alternatives:** tab groups as workspaces, global Cocoa event taps,
  remote command suggestions, or gesture handling inside page renderers.
- **Tests:** focused service and swipe-tracker unit tests are included; static
  M151 API, format and dependency checks passed before ledger integration.
- **Security/privacy impact:** commands and workspace metadata remain local;
  no browsing payload or input text is sent to a remote service.
- **Expected rebase risk:** medium around browser-command and input-event APIs.
- **Removal/upstream plan:** preserve the Ahoi services and adapt only their
  Chromium-facing seams during rolls.

## `0005-ahoi-session-bridge.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** new `ahoi/browser/session`, profile keyed-service wiring,
  profile BUILD/DEPS files and profile startup extra parts.
- **Rationale:** bind stable Ahoi tree UUIDs to live Chromium tabs and windows
  without replacing `TabStripModel` or `WebContents` ownership.
- **Rejected alternatives:** raw tab pointers in persistent storage, a global
  singleton, or a parallel tab/window host.
- **Tests:** focused multi-window, detach, replacement and lifetime tests are
  included, including weak-pointer protection against address reuse.
- **Security/privacy impact:** regular and off-the-record profiles are selected
  explicitly; no secret or OTR state is persisted by the bridge.
- **Expected rebase risk:** medium around profile and tab observer lifecycles.
- **Removal/upstream plan:** retain as the only Ahoi-to-Chromium session adapter
  and update its small observer surface on rolls.

## `0006-ahoi-sidebar-tree-model.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** `ahoi/browser/tab_tree` mutation events and new
  `ahoi/browser/ui/sidebar` model/controller targets.
- **Rationale:** project the durable nested tree into lazy, UUID-stable rows and
  implement rename, delete, undo, before/inside/after drag-and-drop and atomic
  cross-workspace subtree copy.
- **Rejected alternatives:** rebuilding the whole tree per mutation, storing UI
  pointers, or executing one SQL query per descendant.
- **Tests:** controller/model, DnD, undo and 10k-node performance-oriented unit
  tests are included; format/checkdeps and fresh-apply gates passed.
- **Security/privacy impact:** local UI/state mutation only; cycles and invalid
  cross-workspace operations fail closed.
- **Expected rebase risk:** low because the slice depends on Ahoi-owned layers.
- **Removal/upstream plan:** keep the controller/model stable and let native UI
  adapters consume row deltas.

## `0007-ahoi-native-command-bar.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** new `ahoi/browser/command_bar`, session BUILD wiring and
  narrow `BrowserView` accelerator/lifecycle seams.
- **Rationale:** implement a native Views command surface for URL/search,
  tab/workspace activation and an explicit allowlist of browser commands.
- **Rejected alternatives:** Chromium's experimental AI-backed Omnibox
  Everywhere WebUI, remote suggestions, or replacing the normal omnibox.
- **Tests:** controller, Views and execution-adapter unit tests cover OTR,
  cross-window activation, URL credential redaction and GET/POST search paths.
- **Security/privacy impact:** OTR receives no regular-profile items; URLs with
  embedded credentials are redacted and execution is locally allowlisted.
- **Expected rebase risk:** medium around `BrowserView`, omnibox and template
  URL service APIs.
- **Removal/upstream plan:** retain the native Ahoi command service/view and
  keep the `BrowserView` integration seam minimal.

## `0008-ahoi-three-pane-split.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** Chromium's existing split-tab model, Views containers,
  tab drag pipeline, session persistence/restore, accessibility strings and
  corresponding desktop/iOS live-tab interfaces.
- **Rationale:** generalize Chromium's native two-tab split implementation to a
  bounded maximum of three panes with horizontal, vertical and main/secondary
  layouts, resize, focus, close, drag and restore semantics.
- **Rejected alternatives:** multiple embedded WebViews, CSS page tiling, or an
  Ahoi-only parallel content host that bypasses Chromium tab ownership.
- **Tests:** model, Views, DnD and restart/restore tests are included; the 52-file
  patch passed an independent M151 lifecycle/API/format review.
- **Security/privacy impact:** Chromium `WebContents`, process isolation,
  permissions and security indicators remain authoritative for every pane.
- **Expected rebase risk:** high because upstream split-tabs is an active area.
- **Removal/upstream plan:** rebase onto upstream multi-pane support if it
  arrives; otherwise maintain this bounded extension as one cohesive patch.

## `0009-ahoi-sidebar-views.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at the pinned commit above.
- **Affected paths:** native Ahoi sidebar row/tree Views plus narrow
  `BrowserView` and `VerticalTabStripRegionView` install/remove seams.
- **Rationale:** render the nested Ahoi tree inside Chromium's native vertical
  tab region with recycled visible rows, keyboard navigation, rename,
  accessibility and validated drag feedback.
- **Rejected alternatives:** WebUI/Electron sidebars, one View per 10k-node
  tree, or replacing Chromium's frame/window host.
- **Tests:** native View tests cover keyboard, accessibility actions,
  virtualization, rename and DnD; final clang-format, GN-format and checkdeps
  gates passed before recovery into this series.
- **Security/privacy impact:** UI-only integration; it consumes validated Ahoi
  controller operations and does not obtain network or credential authority.
- **Expected rebase risk:** medium around native vertical-tab and frame layout
  APIs.
- **Removal/upstream plan:** keep the Ahoi row/tree Views and adapt only the two
  native installation seams during Chromium rolls.

## `0010-ahoi-live-sidebar-integration.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0009`.
- **Affected paths:** Ahoi command-bar, session bridge, tab-tree and sidebar
  components plus narrow `BrowserView`, macOS native-widget,
  `MultiContentsView`, live-tab-context and vertical-tab-region integration
  seams.
- **Rationale:** connect the persistent Ahoi tree to live Chromium tabs and
  windows, replace the stock vertical-tab list with the product sidebar, show
  the native centered command surface, expose current titles, URLs and
  favicons, and provide single-click activation/collapse, nested folders,
  native context menus, drag feedback and native two-/three-pane split rows.
- **Rejected alternatives:** a second WebView tab host, a WebUI sidebar, DOM
  drag orchestration, using bookmarks as the runtime tab model, or mutating
  parentage merely to make split panes look grouped.
- **Tests:** focused command-bar and sidebar Views tests cover safe synchronous
  command execution, widget/delegate lifetime, split-row projection,
  cross-folder split anchoring, validated drop zones, nested-folder creation
  and single-click folder collapse/expand. The native component build and
  complete app relink pass at `out/AhoiDev`.
- **Security/privacy impact:** profile-local tree/session metadata only. Normal
  and off-the-record profiles remain separated; command results remain local;
  no new renderer privilege, network endpoint, credential storage or sandbox
  exception is introduced.
- **Expected rebase risk:** medium-high around BrowserView layout, native macOS
  activation and upstream split-tab APIs; Ahoi-owned model/View code is lower
  risk.
- **Removal/upstream plan:** retain the Ahoi-owned services and Views while
  keeping the Chromium-facing seams small; replace individual seams with
  upstream equivalents when native vertical-tree or multi-pane APIs mature.

## `0011-ahoi-drag-and-tree-persistence.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0010`.
- **Affected paths:** Ahoi's session bridge, tab-tree store and native sidebar
  drag event/layout paths plus their focused tests.
- **Rationale:** persist the nested tree in one SQLite database inside each
  regular Chromium profile, rebind restored tabs to saved pages at any nesting
  depth, durably refresh committed destinations, and let native Views decide a
  drag before click selection mutates and rebinds recycled rows. The temporary
  New Group drop target occupies a real layout row so it cannot cover ordinary
  reorder or split targets.
- **Rejected alternatives:** continuing with an in-memory runtime tree, a
  second bookmark store, flattening folders during restore, URL matching only
  at workspace root, persisting pending navigation URLs, selecting a row on
  mouse-down before Views starts its drag loop, or overlaying the group target
  on top of live rows.
- **Tests:** all 11 `AhoiTabTreeStoreTest` cases and all 6 `SessionBridgeTest`
  cases pass, including database reopen and nested-tab recreation. Seven
  focused native sidebar tests and eight command-bar tests pass; the incremental
  `chrome` build at `out/AhoiDev` reports no remaining work after the component
  relink.
- **Security/privacy impact:** the database is profile-local and is never
  created for off-the-record profiles. It stores only workspace/tree metadata
  and page destinations; it introduces no cookie, credential, secret-header,
  extension-storage, network or telemetry path.
- **Expected rebase risk:** low for the Ahoi-owned SQLite/query changes and
  medium around native Views drag event sequencing.
- **Removal/upstream plan:** retain the profile-local tree as Ahoi's durable
  authority; replace the URL fallback with a stable upstream tab-session token
  if Chromium exposes one, and adapt only the narrow native drag hooks during
  rolls.

## `0012-ahoi-visual-language-and-nested-search.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0011`.
- **Affected paths:** Ahoi's semantic visual tokens, native command bar,
  sidebar host/rows, session bridge and tab-tree store plus their focused
  tests.
- **Rationale:** establish one theme-aware chrome surface, spacing and radius
  vocabulary; make inactive command results transparent while selected rows,
  URLs and favicons remain legible; index saved pages at unlimited nesting
  depth; and keep all on-disk SQLite work off Chromium's UI sequence. The live
  store is in-memory and complete validated snapshots preserve tree state,
  tombstones and durable undo history on a dedicated `MayBlock` runner.
- **Rejected alternatives:** per-View hard-coded colors, dark cards behind
  every command result, flattening saved pages into bookmarks, suppressing
  Chromium's blocking `DCHECK`, or permitting synchronous profile-database
  reads during navigation/title callbacks.
- **Tests:** all 12 `AhoiTabTreeStoreTest` cases, all 7 `SessionBridgeTest`
  cases, all 15 command-bar tests and 7 focused native sidebar interaction
  tests pass. The incremental `chrome` gate is clean. A fresh-profile HTTPS
  runtime smoke stayed alive through navigation, wrote a valid 57,344-byte
  SQLite database with workspace/node/undo rows, produced no crash report and
  exited cleanly. This is development-runtime evidence, not installed
  `CU_E2E_PASS` evidence.
- **Security/privacy impact:** persistence remains profile-local and excludes
  off-the-record profiles. The change introduces no network endpoint, secret
  field, renderer privilege or sandbox exception; it removes blocking disk I/O
  from the browser UI sequence.
- **Expected rebase risk:** low for Ahoi-owned snapshot/session code and medium
  around native Views colors, bubble layout and Chromium favicon APIs.
- **Removal/upstream plan:** retain the Ahoi visual-token and in-memory model
  layers; adapt only narrow native Views integration seams during Chromium
  rolls and replace custom colors with upstream semantic equivalents where
  their contracts match.

## `0013-ahoi-group-actions-and-localization.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0012`.
- **Affected paths:** Ahoi's native sidebar host, virtualized tree rows and
  focused controller/View tests, plus Chromium's generated English and German
  resource catalogs.
- **Rationale:** make the nested tree operable without drag-only affordances:
  the workspace root can create groups, folders can create subgroups and
  collapse or expand, pages can be wrapped in a group, and pages or complete
  subtrees can be duplicated, renamed or moved to the undo-aware trash. All
  newly visible sidebar text now comes from GRIT with complete German
  translations, while reusable Views receive localized accessibility text
  explicitly and remain independently testable.
- **Rejected alternatives:** hard-coded German strings, a separate bookmark
  context menu, destructive immediate deletion, rebuilding one native View per
  tree node, or moving split tabs out of their persistent parent merely to
  depict the split association.
- **Tests:** the full `ahoi_sidebar_tree_unittests` binary passes 21/21 tests,
  including root/nested group creation, single-click collapse, virtualization,
  drag zones, tab-on-tab split and grouped split projection. Adjacent command,
  navigation, session and persistent-tree suites pass 53/53 tests; the
  incremental `chrome` build completes at `out/AhoiDev`.
- **Security/privacy impact:** mutations stay profile-local and travel through
  the existing validated controller/store operations. Trash remains tombstoned
  and undoable; cross-workspace copies remain atomic; no renderer privilege,
  credential path, network endpoint, telemetry or off-the-record persistence is
  added.
- **Expected rebase risk:** low for Ahoi-owned Views and model code, medium for
  Chromium GRIT catalogs and the native sidebar integration host.
- **Removal/upstream plan:** retain the Ahoi tree action layer and move its
  product strings into a dedicated Ahoi GRIT bundle when that bundle is wired
  into every locale pak; adapt only the narrow Chromium resource and host seams
  during rolls.

## `0014-ahoi-live-tab-lifecycle-and-bidirectional-drag.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0013`.
- **Affected paths:** Ahoi's native command bar, session bridge, transactional
  tab-tree store and sidebar host/controller/Views plus their focused tests.
- **Rationale:** make saved pages and ordinary open tabs one explicit native
  lifecycle: a temporary tab can be dragged into any validated tree position,
  a saved tab or complete split can be dragged back below the separator without
  closing its live `WebContents`, and both kinds expose useful native context
  menus. Split membership moves atomically, real favicons flow through the
  sidebar and command results, the command surface is capped at five results,
  and deferred bubble destruction removes the observed Cmd+T activation crash.
  Saved and temporary rows share one scroll surface so the sections remain
  adjacent while Chromium's New Tab button stays at the bottom of the chrome.
- **Rejected alternatives:** a second runtime tab model, bookmark-only state,
  deleting live tabs when unpinning, removing split panes from their persistent
  parent, a drop target fixed to the window bottom, rebuilding all persistent
  rows per tab event, or destroying the command Widget from inside its native
  activation observer callback.
- **Tests:** all 15 tab-tree, 25 sidebar/controller/View, 17 command-bar and 8
  session tests pass (65 focused tests). The full `chrome` target links at
  `out/AhoiDev`. In an isolated development profile, real mouse input verified
  temporary-to-saved split creation, atomic saved-split-to-temporary roundtrip,
  both context menus, five-result Cmd+T with favicon, and the formerly crashing
  Enter path without a crash report. An `about:blank` idle sample with four test
  tabs measured approximately 0.2% aggregate CPU. This is development runtime
  evidence, not installed or release `CU_E2E_PASS` evidence.
- **Security/privacy impact:** the new lifecycle metadata remains profile-local
  and off-the-record persistence remains excluded. Drops and context actions
  reuse Chromium tabs and validated Ahoi transactions; no renderer privilege,
  credential store, secret-header path, network endpoint, telemetry or sandbox
  exception is introduced.
- **Expected rebase risk:** low for Ahoi-owned store/controller code and medium
  around native Views drag, favicon and bubble-lifecycle APIs.
- **Removal/upstream plan:** retain the Ahoi saved/temporary domain contract and
  adapt only the native integration seams when Chromium's vertical-tree or
  multi-pane APIs evolve.

## `0015-ahoi-save-shortcut-and-drag-feedback.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0014`.
- **Affected paths:** Ahoi's session bridge, tab-tree store/model, native
  sidebar host/controller/rows and localization plus narrow Chromium
  BrowserView, vertical-region, omnibox/location-bar, macOS command-dispatch,
  dependency and build seams (29 files).
- **Rationale:** complete the explicit saved/temporary lifecycle from the
  keyboard and make its runtime projection stable. `Cmd+D` now promotes the
  active temporary tab idempotently at the active workspace root through both
  Views accelerators and the native macOS menu-key path. A running saved page
  is indexed exactly once in Cmd+T while genuine separate tabs remain legal in
  the sidebar. Runtime binding changes refresh the two sidebar sections, row
  recycling restores titles after split/drag changes, the complete free lower
  section accepts saved-to-temporary drops, and the new-group target occupies a
  deliberate drag-only layout row. The same patch adds persistent group
  name/icon/accent customization, nested/duplicate/move/copy-link actions,
  collapse animation, scoped recent-link hover search, sidebar undo, split-pair
  rows and native side/stack/reverse/separate actions. The page frame now owns
  the remaining width, the sidebar width is bounded and persisted, and the
  fixed bottom dock exposes New Tab, Downloads, History, and Settings. Direct
  omnibox clicks and `Cmd+T` open the centered bounded command surface, while a
  location-bar action copies the active URL.
- **Rejected alternatives:** URL-based global deduplication that would hide
  real duplicate tabs, preserving both open- and saved-result copies in Cmd+T,
  mapping `Cmd+D` back to a second bookmark model, polling the sidebar,
  permanent per-row cards, a second bookmark bar, or unrelated Unicode icons.
- **Tests:** `gn check out/AhoiDev //chrome/browser/ui/views:views` and the
  incremental `chrome -j10` build pass. The complete stack composes to tree
  `353ec0b4a17a4fe561ff2a997d4cb9a04b731a67` with a verified overlay delta.
  Development-runtime acceptance passed the four-action dock, direct omnibox
  command bar with five unique results, group actions/customization, `Cmd+Z`,
  searchable recent-link card, clear-temporary action, paired split rows,
  side/stack switching and stacked-layout restoration after restart. The prior
  focused unit suites remain recorded; this larger UX slice did not rerun every
  unit target, and native pointer drag/resize remains a manual dogfood gate.
- **Security/privacy impact:** saving remains profile-local, reuses the
  validated SQLite tree and is unavailable for off-the-record profiles. The
  change adds no renderer privilege, credential path, network endpoint,
  telemetry, sync payload or sandbox exception.
- **Expected rebase risk:** low for Ahoi-owned bridge/View/store code and medium
  for the narrow BrowserView, vertical-region, omnibox and macOS command hooks.
- **Removal/upstream plan:** retain the Ahoi lifecycle and presentation
  callbacks; adapt only the keyboard/menu hooks if Chromium consolidates its
  macOS command routing.

## `0016-ahoi-modular-ui-auth-and-native-drag.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0015`.
- **Affected paths:** Ahoi command, session, tab-tree, modal and sidebar
  components; the browser profile/login integration; localized login strings;
  narrow BrowserView, vertical-region, omnibox, Remote Cocoa and macOS Views
  drag seams (136 files, dominated by splitting three former monoliths into
  focused implementation units).
- **Rationale:** establish the maintainable product foundation required for
  continued feature work. The session bridge, transactional tree store and
  native sidebar host/View are divided into responsibility-scoped files while
  preserving their public contracts. Central visual and motion tokens drive
  modal surfaces, rows, controls and the dock. The bottom New Tab/Incognito
  control becomes one vertically stacked, independently accessible cell with a
  full-width inner divider; the recent-link search field gets reliable text
  padding and a restored focus ring. HTTP Basic/Digest authentication gains a
  profile-local credential service, realm-aware saved-account selection and
  three mutually exclusive persistence choices: use once, save only after a
  successful challenge, or never save for that protection space. Finally,
  macOS native drag startup is forwarded from AppKit into the active Views
  `DragController`, concrete pasteboard fallback data keeps previews viable,
  drag-only layout changes are deferred into the nested loop, and virtualized
  source rows remain materialized until `OnDragDone()` can clear every drag
  affordance.
- **Rejected alternatives:** retaining 1,000-4,600-line implementation files;
  per-component hard-coded colors and timing; two contradictory independent
  auth checkboxes; persisting credentials before server acceptance; mutating
  sidebar layout before AppKit has entered a native drag session; pinning every
  row during drag; or replacing Chromium's native Views/AppKit drag stack with
  a parallel event system.
- **Tests:** the incremental `chrome`, `views_unittests` and
  `ahoi_sidebar_tree_unittests` targets build with no remaining work. All six
  Ahoi suites pass 117/117. The new
  `DragDropClientMacTest.NativeDragStartedIsForwardedFromAppKit` regression
  passes, including Xcode 26.5 nullability enforcement; `gn check` passes for
  Views, Remote Cocoa and the Ahoi sidebar, and all touched focused files pass
  Chromium clang-format plus `git diff --check`. A freshly restarted
  development process visibly confirms the stacked New Tab/Incognito actions
  and the three exclusive auth choices. Computer Use cannot emit a trustworthy
  physical `mouseDragged` event in this environment, so real pointer drag and
  the hover-only recent-link surface remain explicit manual dogfood gates.
- **Security/privacy impact:** positive credential semantics. Saved HTTP-auth
  secrets reuse Chromium's profile password store, are keyed by the complete
  protection space, are written only after successful authentication and are
  never automatically read or written off the record. The native drag hook
  forwards lifecycle state only; it adds no renderer privilege, network path,
  telemetry, sync payload, sandbox exception or persistent external data.
- **Expected rebase risk:** low for the Ahoi-owned modular files, medium for
  BrowserView/login integration, and medium-high for the deliberately narrow
  AppKit/Remote Cocoa/Views drag lifecycle hook.
- **Removal/upstream plan:** retain the Ahoi domain modules and credential
  policy; adapt the narrow host and native-drag seams on Chromium rolls, and
  remove the AppKit forwarding hook if upstream exposes an equivalent Views
  lifecycle callback.

## `0017-ahoi-product-source-freeze.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0016` and the complete 437-file product overlay.
- **Affected paths:** 242 existing files across the Ahoi browser/application
  modules and their narrow Chromium integration seams under `chrome/`,
  `components/`, `content/`, `extensions/`, `services/`, `third_party/`,
  `tools/` and `ui/`. All 414 newly introduced product, documentation and
  local-fixture files are stored as complete overlay inputs instead of being
  hidden inside this modification-only patch.
- **Rationale:** freeze the complete reviewed product source state after the
  modular UI, privacy, account-network, secure component transport, sync,
  extension, developer-toolkit, popup, media, resource-policy and native
  interaction work. The split between full overlay files and a full-index
  modification-only patch makes the exact dirty-checkout result reproducible
  from the pinned Chromium commit without relying on untracked local files.
- **Rejected alternatives:** resetting or staging the canonical dirty
  Chromium checkout; applying the overlay to that checkout; preserving only a
  worktree diff that omits untracked files; allowing rename detection to
  rewrite source identity; or accepting an approximate source snapshot whose
  composed Git tree was not proven equal to the captured target.
- **Tests:** the pre-patch composition was reconstructed twice as tree
  `218613f2c92cc6d89a7ac0b64666242ab87786a5`. Two independently emitted
  `--binary --full-index --no-renames` deltas were byte-identical with SHA-256
  `5f00c505128e5c3c22f0c9d6d055000a12bfca49887f12dfd1d1d64fe368f4d7`.
  Applying either delta through a fresh temporary index in an isolated shared
  clone produced the captured target tree
  `15bdaa897e94fbe8fbdcfde132438c49c4ae852a`; repository contracts pin the
  patch order, hash, 242-path count, full object IDs and modification-only
  form. The final tree includes both type-correct `EXPECT_EQ(0, NumPending())`
  assertions from the component-build regression. `diff.gitattributes` makes
  Git encode Sparkle's vendored license as an
  exact binary delta so its historical trailing space is retained while the
  combined overlay remains applicable with `--whitespace=error-all`.
- **Security/privacy impact:** this patch adds no authority by itself; it
  preserves the already reviewed source truth. The captured state includes
  fail-closed HTTPS-only component transport, disabled product account
  background initialization, profile/OTR boundaries, local-only credential
  handling and the existing no-product-telemetry policy.
- **Expected rebase risk:** high. This is an exact source-freeze delta over a
  broad product integration surface and is intentionally sensitive to any
  upstream or preceding-patch drift.
- **Removal/upstream plan:** use this freeze as the recovery and acceptance
  baseline. On the next Chromium roll, repartition surviving product changes
  into focused patches and remove this catch-up patch only after the newly
  composed tree has equivalent build, test and runtime evidence.

## `0018-ahoi-user-agent-client-hints-brand.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0017`, represented by tree `15bdaa897e94fbe8fbdcfde132438c49c4ae852a`.
- **Affected paths:** exactly
  `components/embedder_support/user_agent_utils.cc`.
- **Rationale:** expose AhoiBrowser's existing product name as its UA-CH
  product brand while preserving Chromium's upstream omission when the
  product name is exactly `Chromium`. This keeps the brand decision tied to
  the configured product identity instead of an upstream branding buildflag
  that cannot distinguish the Ahoi product build.
- **Rejected alternatives:** hard-coding `AhoiBrowser` in the UA utility;
  changing the legacy User-Agent string; adding a second client-hints
  implementation; or reporting a product brand for stock Chromium.
- **Tests:** two independently emitted `--binary --full-index --no-renames`
  patches were byte-identical with SHA-256
  `75cd21010930a8f551253a52fec7f8337e606c90d7f773fdb325240c4adb6561`.
  Strict cached application changes only the named path and produces
  intermediate tree `a16923ab31f7ac725428d8806b137dfd096a17df`.
  Repository contracts pin the patch hash, path and product-name guard.
- **Security/privacy impact:** UA-CH now exposes the same stable product
  identity already configured for the browser instead of silently presenting
  Chromium's brand behavior. It adds no per-user entropy, network endpoint,
  secret, privilege or telemetry path.
- **Expected rebase risk:** medium because upstream periodically revises its
  UA-CH brand construction and grease ordering.
- **Removal/upstream plan:** retain the product-name guard while Ahoi uses the
  upstream UA-CH utility; replace it with an upstream-supported embedder brand
  hook when one can express the same Chromium fallback.

## `0019-ahoi-deterministic-platform-tests.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0018`, represented by tree `a16923ab31f7ac725428d8806b137dfd096a17df`.
- **Affected paths:** exactly
  `components/autofill/core/browser/metrics/autofill_metrics_test_base.cc` and
  `components/input/web_input_event_builders_mac_unittest.mm`.
- **Rationale:** make the affected upstream tests independent of the host's
  date parsing, timezone and active macOS keyboard layout. The Autofill test
  uses an explicit UTC instant; the three layout-sensitive input tests scope
  themselves to the U.S. layout they already document and assume.
- **Rejected alternatives:** changing the machine timezone or keyboard layout;
  depending on locale-sensitive `FromString`; skipping the tests outside a
  U.S. host configuration; or weakening their expected key mappings.
- **Tests:** two independently emitted `--binary --full-index --no-renames`
  patches were byte-identical with SHA-256
  `ef8294746937cd7070c89fc7fc2ca5cc3ffb5fee9895356345a6b6199fbd73b4`.
  Two fresh isolated-index runs applied `0018` and then `0019` with
  `--whitespace=error-all`; both produced final target tree
  `41f621e9d65b6dea9f02256075d779b866fc7746`. Repository contracts pin the
  two paths, UTC instant and three scoped U.S. layouts.
- **Security/privacy impact:** none. Both changes are test-only and do not
  alter production code, runtime permissions, networking, telemetry or stored
  browser data.
- **Expected rebase risk:** low, limited to upstream test fixture API or Cocoa
  keyboard-test helper changes.
- **Removal/upstream plan:** upstream these deterministic test fixes where
  practical and remove the patch once the pinned Chromium baseline contains
  equivalent UTC and scoped-layout setup.

## `0020-ahoi-upstream-site-data-clock-revert.patch`

- **Owner:** AhoiBrowser project; exact backport of the official Chromium
  revert `7de113b6a9ebac43911cb99dbc93fba1acbe3c2d` (Change-Id
  `Ide538602776e119975742aecccf40b3dedb60152`, Chromium CL
  [8126355](https://chromium-review.googlesource.com/c/chromium/src/+/8126355),
  main position `#1665269`).
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patches `0001` through
  `0019`, represented by tree `41f621e9d65b6dea9f02256075d779b866fc7746`.
- **Affected paths:** exactly
  `components/performance_manager/persistence/site_data/site_data_impl.cc`.
- **Rationale:** restore wall-clock timestamps as upstream did after finding
  that `TimeTicks` can stop while a machine is suspended. Caching a mapping
  between the monotonic clock and the Unix epoch can therefore drift from real
  time; `Time::Now() - Time::UnixEpoch()` preserves the persisted timestamp
  contract.
- **Rejected alternatives:** accepting suspend-induced timestamp drift;
  weakening or skipping the affected tests; maintaining an Ahoi-specific
  clock conversion; or rolling unrelated post-M151 Chromium changes only to
  acquire this one-file correction.
- **Tests:** two isolated-index generations emitted byte-identical
  `--binary --full-index --no-renames` patches with SHA-256
  `50ec277a35046de4e6cb36cb1f90f464c7bd42db349624d499ea843daddaba39`.
  Two strict `--whitespace=error-all` applications produced intermediate tree
  `d88f36c778eeb80578c84e49ee5493af8ca06cd9`. Post-fix focused evidence across
  patches `0020` and `0021` is 50/50 plus a 100/100 repeat lane; the preceding
  retry-free full suite still had four failures and is not reported as green.
- **Security/privacy impact:** none. This changes the clock source for existing
  local site-data timestamps and adds no network, telemetry, identity,
  credential, storage-authority or sandbox path.
- **Expected rebase risk:** low. The single helper is source-sensitive, but the
  correction is already present upstream after Chromium main position
  `#1665269`.
- **Removal/upstream plan:** remove this backport when the pinned baseline
  contains `7de113b6a9ebac43911cb99dbc93fba1acbe3c2d` or an equivalent successor;
  retain an explicit regression check for suspend-safe wall-clock timestamps.

## `0021-ahoi-upstream-page-load-tracing-test-isolation.patch`

- **Owner:** AhoiBrowser project; M151-compatible backport of the official
  Chromium fix `2e1143e225f92ded424380beaa9aa77df332b93a` (Change-Id
  `I97a89551922458fda79c2b33496b321c3f91919c`, Chromium CL
  [8160156](https://chromium-review.googlesource.com/c/chromium/src/+/8160156),
  main position `#1676463`).
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` after patch `0020`, represented
  by tree `d88f36c778eeb80578c84e49ee5493af8ca06cd9`.
- **Affected paths:** exactly
  `components/page_load_metrics/browser/observers/core/uma_page_load_metrics_observer_unittest.cc`,
  `content/public/browser/tracing_support.cc` and
  `content/public/browser/tracing_support.h`.
- **Rationale:** isolate `UmaPageLoadMetricsObserverTest` instances from the
  process-global tracing registration whose state otherwise leaks across tests.
  The production helper now owns the registration in resettable optional
  storage, and the test harness resets it during teardown. The backport retains
  M151's existing `NamedTrack` API and contains no Ahoi-specific behavior.
- **Rejected alternatives:** changing test order or process sharding; disabling
  the affected tests; leaking a second registration; resetting unrelated
  tracing state; or importing unrelated post-M151 source changes.
- **Tests:** two isolated-index generations emitted byte-identical
  `--binary --full-index --no-renames` patches with SHA-256
  `d4ccef20872a7432aec4e3dd0f1e9b7ec58f599bea82780324e8040f08b2d2e5`.
  Two strict applications produced final target tree
  `a3865fc6e9f89ccd9403fa888ccce1a54d67c4e4`. The complete overlay and all 21
  patches composed twice byte-identically to that tree (combined delta SHA-256
  `8cfd72782b7beb911466939dbd0a20e6fd16d97b397d8bd212598dd00a14dd3a`).
  Post-fix focused evidence across patches `0020` and `0021` is 50/50 plus a
  100/100 repeat lane; a post-fix retry-free full-suite pass is still pending.
- **Security/privacy impact:** none. The reset hook is used by tests and does
  not change renderer privileges, production trace collection, network access,
  telemetry policy, credentials, persistent browser data or sandboxing.
- **Expected rebase risk:** low. Upstream already carries the same lifecycle
  fix after main position `#1676463`; only tracing-track API evolution may
  require a mechanical refresh on a future roll.
- **Removal/upstream plan:** remove this backport when the pinned baseline
  contains `2e1143e225f92ded424380beaa9aa77df332b93a` or equivalent resettable
  registration ownership, while retaining the repeated-process test gate.
