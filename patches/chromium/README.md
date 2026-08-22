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
