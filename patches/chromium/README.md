# Chromium M152 patch ledger

`series` is the authoritative application order. The active stack targets
Chromium Mac Stable `152.0.7977.65` at
`fc4d67f1788019a27e32511137ceccbd2fafdaaa`. A roll is accepted only when the
overlay and every patch compose offline to one exact tree, the real checkout
matches that tree, and the build/test evidence names the same commit.

The superseded 21-patch M151 stack remains recoverable from
`refs/ahoi/recovery/product-source-freeze-20260826-a3865fc6e9f8` and
`artifacts/build/recovery/ahoi-m151-final.bundle`; it is intentionally not kept
as a second active patch stack.

## `0001-ahoi-m152-integration-seams.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium Mac Stable `152.0.7977.65` at the exact commit
  above.
- **Affected paths:** 252 existing Chromium integration paths. They cover the
  native browser frame/sidebar and command bar, two-to-four-pane split tabs,
  sessions/startup, history/device tabs, settings including the theme-resource
  product logo in the Ahoi menu entry and the fail-closed Ahoi
  Settings-handler dependency boundary, HTTP authentication,
  cookie/cache/privacy policy, extensions, autoscroll, macOS native
  WebContents-to-Views drag routing and crash-safe macOS history-overlay
  teardown across nested native drag loops,
  branding/localization, and their focused Chromium tests. Product-owned
  implementations remain under the tracked `overlay/chromium/src/ahoi` tree.
- **Rationale:** keep Ahoi-owned modules isolated while adapting the smallest
  practical set of Chromium-owned seams once for M152. Squashing the former
  dependent M151 chain into one milestone seam patch prevents future rolls from
  replaying obsolete intermediate states.
- **Rejected alternatives:** carrying the M151 patch chain unchanged, forking
  Chromium wholesale, replacing native Views/WebContents with a parallel
  WebView host, or weakening Chromium profile, sandbox, permission, credential,
  and process-isolation ownership.
- **Tests:** offline composition and full-index checks, repository contracts,
  Chromium format/checkdeps/build gates, focused native and browser tests
  including detached-host no-navigation and attached-swipe lifecycle
  regressions, and installed-bundle visible E2E evidence. The patch is
  binary/full-index and is applied with `--whitespace=error-all` against the
  exact baseline.
- **Security/privacy impact:** profile and off-the-record separation, Chromium
  networking/security indicators, sandboxing, permission ownership, and local
  credential boundaries remain authoritative. Ahoi additions are local and
  fail closed at their Chromium adapters.
- **Expected rebase risk:** high for split-tab, browser-frame, startup/session,
  history/settings WebUI, and macOS drag APIs; low-to-medium for Ahoi-owned
  modules because they live in the overlay.
- **Removal/upstream plan:** on each Stable milestone, preserve the Ahoi overlay,
  re-derive this seam patch directly from the new exact pin, and drop seams as
  Chromium gains equivalent native behavior.

## `0002-ahoi-deterministic-platform-tests.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium Mac Stable `152.0.7977.65` at the exact commit
  above.
- **Affected paths:** Autofill time-dependent metrics test setup and the macOS
  input-event keyboard-layout tests.
- **Rationale:** remove host clock and keyboard-layout nondeterminism from the
  local verification lane without changing production behavior.
- **Rejected alternatives:** skipping flaky tests, broad retries, changing
  production locale/input behavior, or accepting host-dependent evidence.
- **Tests:** exact two-path structural contract plus the affected Chromium test
  binaries in the focused matrix.
- **Security/privacy impact:** none; test code only.
- **Expected rebase risk:** low and source-sensitive.
- **Removal/upstream plan:** remove each hunk when the equivalent deterministic
  setup exists upstream.

## `0003-ahoi-upstream-page-load-tracing-test-isolation.patch`

- **Owner:** AhoiBrowser project; narrow backport of Chromium's upstream tracing
  isolation fix.
- **Upstream baseline:** Chromium Mac Stable `152.0.7977.65` at the exact commit
  above.
- **Affected paths:** the UMA page-load observer test and public tracing-support
  implementation/header.
- **Rationale:** isolate test tracing state so the page-load test is repeatable
  in the complete component matrix.
- **Rejected alternatives:** disabling the test, order-dependent execution, or
  importing unrelated post-M152 changes.
- **Tests:** exact three-path structural contract and the affected page-load
  metrics test binary.
- **Security/privacy impact:** none; production tracing behavior is unchanged
  outside the state-reset seam used by tests.
- **Expected rebase risk:** low.
- **Removal/upstream plan:** remove when the upstream fix is present in the next
  pinned Stable baseline.

## `0004-ahoi-lean-profile-compose-guards.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium Mac Stable `152.0.7977.65` at the exact commit
  above, applied after the Ahoi M152 integration seam.
- **Affected paths:** the Glic interactive-test aggregate and renderer
  context-menu implementation/test aggregates. The Ahoi integration seam owns
  the desktop Settings guard so each ordered patch has one clear owner.
- **Rationale:** keep every remaining `//chrome/browser/compose` edge behind
  Chromium's `enable_compose` argument so the Lean profile can disable the
  dedicated Compose product slice without reaching its fail-closed assertion.
- **Rejected alternatives:** silently re-enabling Compose in the Lean profile,
  removing Compose source code, weakening its child-target assertion, or
  carrying a non-reproducible checkout-only edit.
- **Tests:** strict ordered patch composition, full-index validation, Lean
  component-matrix roll checks, `gn gen` with Compose disabled, and the normal
  full development profile with Compose enabled.
- **Security/privacy impact:** none; the full profiles retain upstream Compose,
  while Lean profiles remove only already build-flagged dependency edges.
- **Expected rebase risk:** low-to-medium because Chromium can add new parent
  edges when Compose integrations move between desktop surfaces.
- **Removal/upstream plan:** remove individual guards as upstream consistently
  guards every parent edge with `enable_compose`.

## `0005-ahoi-content-card-seam-and-resize-affordance.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the complete
  Ahoi integration and Lean guard stack.
- **Affected paths:** the tabbed browser layout and native split resize area.
- **Rationale:** Ahoi's outer content card already supplies the split gutter;
  retaining Chromium's second inset exposed an 8 px background seam. The
  native split resize path remains unchanged, while its restrained handle is
  kept visible so stacked height resizing is discoverable and targetable.
- **Rejected alternatives:** changing Chromium's global split inset, covering
  the seam with an overlay, widening the resize hit target into page content,
  or replacing the upstream mouse, touch, keyboard and persistence path.
- **Tests:** visible installed-browser seam and stacked/side-by-side resize
  journeys first, followed by ordered composition and focused split layout
  tests.
- **Security/privacy impact:** none; layout and affordance only.
- **Expected rebase risk:** low-to-medium because the browser layout owns the
  side-panel and fullscreen inset policy.
- **Removal/upstream plan:** drop the seam exception if Ahoi no longer owns an
  outer content card or Chromium exposes a product-level inset policy.

## `0006-ahoi-command-bar-current-tab-semantics.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the complete
  Ahoi integration, Lean guards and native content-card fixes.
- **Affected paths:** generated browser resources and German/English command
  surface translations.
- **Rationale:** the Cmd+T active-tab state has its own accessibility copy and
  must not depend on an unrelated Developer Toolkit label that happens to have
  the same English text today.
- **Rejected alternatives:** hardcoded accessibility text or reusing the
  developer-asset scope resource.
- **Tests:** visible Cmd+T active-row and VoiceOver semantics first, followed by
  ordered patch composition and generated-resource compilation.
- **Security/privacy impact:** none; accessibility semantics only.
- **Expected rebase risk:** low.
- **Removal/upstream plan:** retain while the native Ahoi command bar exists.

## `0007-ahoi-split-resize-hit-testing-and-accessibility.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the command-bar
  accessibility seam.
- **Affected paths:** the native multi-contents pane host and its resize area.
- **Rationale:** macOS WebContents are independently composited native siblings.
  The later pane hosts could therefore win pointer hit testing over an earlier
  divider even when their layout bounds only met at the seam. Resize areas now
  sit above every pane but below drop/overlay surfaces, and the visible AX
  slider delegates keyboard plus increment/decrement actions to the canonical
  Chromium ratio/persistence path.
- **Rejected alternatives:** an overlay-only fake handle, JavaScript resizing,
  polling pointer coordinates, widening the divider into page content, or
  bypassing Chromium's split model and snap-point logic.
- **Tests:** installed-browser pointer resize in side-by-side and stacked modes
  first, including AX increment/decrement and persistence; then ordered patch
  composition plus focused native split layout/interaction tests.
- **Security/privacy impact:** none; z-order, pointer routing and accessibility
  actions only.
- **Expected rebase risk:** low-to-medium because native WebContents and resize
  area construction order can change in Chromium rolls.
- **Removal/upstream plan:** keep until Chromium guarantees native child hit
  testing independently of pane construction order and exposes the AX actions
  on the visible split handle.

## `0008-ahoi-focused-split-pane-reorder.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the native
  split resize seam.
- **Affected paths:** BrowserView split binding and MultiContentsView pane-host
  ordering.
- **Rationale:** a pure split reorder must not detach focused WebContents while
  Chromium is synchronously notifying TabStripModel observers. The native
  focus hand-off otherwise attempts a nested tab activation and aborts on the
  model's reentrancy guard. Two-to-four-pane permutations now reorder their
  existing hosts in place; actual membership changes retain the overlap-safe
  detach/attach lifecycle.
- **Rejected alternatives:** weakening TabStripModel's reentrancy CHECK,
  deferring the complete model transaction, globally swallowing focus events,
  or detaching then guessing which WebContents should regain focus.
- **Tests:** installed-browser focused pane drag from side-by-side to stacked in
  both directions first; then the focused split-drop regression, native pointer
  interaction case, and ordered patch composition.
- **Security/privacy impact:** none; native view ownership, active-tab identity,
  and focus are preserved rather than recreated.
- **Expected rebase risk:** medium because Chromium's native multi-contents
  host and observer notification paths evolve with upstream split view.
- **Removal/upstream plan:** retain until upstream supports arbitrary two-to-
  four-pane in-place host permutations without synchronous focus churn.

## `0009-ahoi-empty-surface-extension-menu.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the focused
  split-pane reorder seam.
- **Affected paths:** Chromium's platform-agnostic extensions-menu model and
  contract, the desktop delegate, the focused model browser test and the site-
  permissions interactive UI test.
- **Rationale:** Ahoi deliberately supports a live zero-tab window. Chromium's
  extensions menu assumes every browser window has an active WebContents and
  dereferenced null while opening the menu from Ahoi's empty surface. The menu
  now exposes installed extensions in a generic, page-independent state while
  hiding site controls, disabling actions that require a tab, and ignoring
  stale site-access callbacks until a real tab is active.
- **Rejected alternatives:** creating a synthetic tab, disabling the complete
  extensions menu, catching the crash, or weakening Ahoi's true empty-window
  contract.
- **Tests:** installed-browser zero-tab menu opening with existing extensions
  and the open-site-permissions-to-zero-tab transition first, followed by the
  focused empty-tab-list/stale-action model regression, the site-permissions
  interactive UI regression and ordered patch composition.
- **Security/privacy impact:** site-bound extension actions and permission
  controls fail closed without active WebContents; extension management remains
  available.
- **Expected rebase risk:** low-to-medium because upstream's extensions menu is
  still evolving toward a shared desktop/Android view model.
- **Removal/upstream plan:** retain while Ahoi supports true zero-tab windows,
  unless upstream makes all extensions-menu entry points null-WebContents safe.

## `0010-ahoi-zen-standard-import-seam.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the zero-tab
  extension-menu guard.
- **Affected paths:** Chromium's normal importer registry and its macOS target
  dependency.
- **Rationale:** safe Zen profiles should appear in the established
  `Browserdaten importieren` dialog and reuse Chromium's Firefox importer for
  categories it truly supports; a separate onboarding surface is unnecessary.
- **Rejected alternatives:** a duplicate importer UI, treating every Firefox
  directory as Zen, importing passwords through unsigned NSS loading, or
  claiming Zen sidebar compatibility from a file-name/header match alone.
- **Tests:** visible standard import-dialog source detection first, then bounded
  profile/INI/path fixtures and ordered patch composition.
- **Security/privacy impact:** discovery is bounded to the real Zen data root,
  rejects traversal and symlinks, and advertises only categories backed by safe
  regular files. Sidebar mutation remains disabled.
- **Expected rebase risk:** low because the seam is one source-provider call and
  one target dependency.
- **Removal/upstream plan:** retain until upstream supports branded
  Firefox-derived profile roots through a public importer-provider API.

## `0011-ahoi-command-scroll-and-auth-policy-hardening.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the compact Zen
  importer seam.
- **Affected paths:** BrowserView command-scroll preview/activation and
  Chromium's desktop HTTP-auth prompt, coordinator, tab-helper and LoginView
  ownership seams.
- **Rationale:** command-plus-scroll must preview and commit one stable tab in
  the current workspace without stealing web zoom or operating through a modal
  pane. HTTP-auth account management must stay main-frame-only, preserve the
  Ahoi credential-service boundary and never leave an asynchronous prompt with
  a dangling handler.
- **Rejected alternatives:** re-resolving a different tab at gesture commit,
  checking only the currently active split pane, weakening modal ownership,
  enabling generic credential storage for subresources, or relying on a raw
  LoginHandler pointer after the widget closes.
- **Tests:** installed current-workspace command-scroll preview/cancel/commit
  and split-modal rejection first, followed by synthetic main-frame,
  subresource and prompt-destruction HTTP-auth cases plus ordered composition.
- **Security/privacy impact:** no URL or query telemetry is added. Credential
  storage remains profile-scoped, main-frame-gated and mutually exclusive with
  Ahoi's account service; subresource prompts remain value-blind and ephemeral.
- **Expected rebase risk:** medium because BrowserView gesture routing and the
  upstream login prompt lifecycle are milestone-sensitive.
- **Removal/upstream plan:** remove individual auth or gesture guards only when
  Chromium exposes equivalent stable-target, all-pane-modal and weak-lifetime
  contracts.

## `0012-ahoi-daily-driver-lifecycle-and-branding.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the shared
  command-scroll eligibility patch owned by the parallel navigation wave.
- **Affected paths:** macOS BrowserView global-shortcut integration plus
  Chromium default-browser resources and German translations.
- **Rationale:** Alt+Space must retry registration after early listener/native
  failure, unregister after the last regular window and start its activation
  cooldown only after a Quick Window was actually created. Default-browser
  settings, prompts, menu actions and PDF handoff must name AhoiBrowser rather
  than exposing Chromium branding.
- **Rejected alternatives:** a process-lifetime one-shot registration flag,
  suppressing the in-window fallback, starting cooldown on failed opens,
  runtime string replacement or a new onboarding surface.
- **Tests:** no test or build was run while preparing this late-visible-E2E
  wave. The required order is installed Alt+Space cold-start/reopen/conflict
  behavior and reachable DE/EN default-browser surfaces first, then focused
  unit/browser coverage and ordered patch composition.
- **Security/privacy impact:** no shortcut or string telemetry is added. The
  local fallback remains available only when native global registration is not
  active.
- **Expected rebase risk:** low-to-medium because BrowserView shortcut setup
  and upstream default-browser promos can move between milestones.
- **Removal/upstream plan:** retain while Ahoi owns the Quick Window and
  Chromium-branded default-browser resource set.

## `0013-ahoi-standard-import-surface.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the focused
  daily-driver lifecycle and branding seams.
- **Affected paths:** Chromium's standard Settings import dialog, its browser
  proxy and build inputs, localized resources, and focused macOS WebUI tests.
- **Rationale:** Arc's bounded preview and transactional import belong in the
  established `Browserdaten importieren` flow. The standard source selector
  now hands Arc to the existing profile-scoped Ahoi service while retaining a
  compact preview, explicit backup/commit confirmations, real split choices,
  and honest imported/skipped/degraded/excluded result counts.
- **Rejected alternatives:** a separate transfer center, a marketing wizard,
  letting the standard Chromium importer consume Arc's synthetic source
  index, or moving transaction ownership into WebUI.
- **Tests:** visible installed-dialog source selection, preview, commit result,
  restart and identical-snapshot no-op first; then the focused macOS WebUI and
  repository contracts plus ordered patch composition.
- **Security/privacy impact:** WebUI never receives private Arc titles or URLs
  outside the already redacted service contract and cannot invoke Chromium's
  generic importer for the Arc entry. Backup and commit remain separately
  confirmed and all mutation remains owned by `ArcImportService`.
- **Expected rebase risk:** medium because the Polymer import dialog and its
  Lit migration/build lists remain milestone-sensitive.
- **Removal/upstream plan:** retain the small dialog bridge while Ahoi owns a
  structure-aware Arc importer; remove it if upstream exposes an equivalent
  transactional custom-source provider.

## `0014-ahoi-ubo-build-gate.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the standard
  Arc import surface.
- **Affected paths:** Chromium's browser command controller, app/extension menu
  action construction, toolbar target, and direct GN dependencies.
- **Rationale:** the narrow uBlock Origin Classic MV2 exception must be absent
  from normal and release products unless an explicit dogfood GN profile
  compiles it in. Disabled builds expose no menu/action entry point, reject the
  command again at execution, revoke stale authorization, and disable the
  exact Classic ID if it was retained by an older opted-in build.
- **Rejected alternatives:** a mutable preference, Finch/remote configuration,
  hiding only one menu item, leaving stale MV2 authorization loadable, or
  enabling the exception in every development/release profile.
- **Tests:** visible dogfood menu/install behavior and a gate-off installed
  profile first; then focused policy/service tests, build-profile contracts,
  release GN inspection, and ordered patch composition.
- **Security/privacy impact:** default-deny is a generated compile-time build
  flag. Release profiles explicitly keep it false; a disabled build clears
  only Ahoi's local authorization for the exact pinned ID and uses Chromium's
  unsupported-manifest disable reason without widening MV2 support.
- **Expected rebase risk:** low-to-medium because command/action dependency
  ownership and the extensions submenu implementation can move upstream.
- **Removal/upstream plan:** remove only after the Classic path is retired or
  upstream offers an equally narrow signed-package policy with a release-safe
  build-time exclusion.

## `0015-ahoi-zen-import-availability.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the uBlock
  Origin Classic compile-time gate.
- **Affected paths:** Chromium's importer-source registry and Settings handler,
  the standard import dialog and browser proxy, localized resources, and the
  focused macOS WebUI launcher/build inputs.
- **Rationale:** Zen must be represented by real backend discovery state. An
  absent installation adds no option, a running source adds one visibly
  disabled sentinel with an actionable close-Zen reason, and an available
  source exposes only profiles and categories confirmed by bounded discovery.
  Parallel metadata preserves the exact backend index even after Ahoi's Arc
  option is inserted in the rendered selector.
- **Rejected alternatives:** an always-enabled frontend-only Zen option,
  silently dropping a running source, trusting a renderer-supplied index, or
  advertising passwords/sidebar structure without a verified importer.
- **Tests:** visible installed-dialog not-installed/running/available behavior
  first, followed by deterministic application/profile fixtures, the focused
  WebUI availability suite, handler fail-closed coverage and ordered patch
  composition.
- **Security/privacy impact:** unavailable or forged selections are rejected in
  the browser process before import-type evaluation. Discovery sends only
  source/profile metadata already required by Chromium's standard dialog and
  never exposes history, bookmark or sidebar contents.
- **Expected rebase risk:** medium because importer list ownership, WebUI
  payload construction and the Lit dialog may change together upstream.
- **Removal/upstream plan:** retain until Chromium exposes a first-class
  availability/disabled-reason contract for branded Firefox-derived sources.

## `0016-ahoi-current-session-receipt.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after Zen importer
  availability.
- **Affected paths:** Chromium's session command-storage backend/manager,
  desktop `SessionService`, and their focused unit/browser tests.
- **Rationale:** an Arc structure import may be called committed only after a
  complete Current Session reset has been flushed to durable storage and the
  exact file has been decoded back into the expected windows, tabs and native
  split commands. The rebuild also preserves Ahoi workspace/tree extra-data
  and records the most recently active trackable window.
- **Rejected alternatives:** waiting for the normal 2.5-second save timer,
  treating a posted backend task as a durable receipt, reading Last Session,
  accepting only one side of a dual-write stage, or rebuilding without Ahoi
  extra-data.
- **Tests:** visible Arc import/restart/recovery proof first; then focused
  backend flush/read, empty-reset, encryption-not-ready, dual-write mismatch,
  and current-session browser receipt coverage plus ordered patch composition.
- **Security/privacy impact:** verification remains profile-local and exposes
  only Chromium's already-decoded session model to the browser process. It
  fails closed on missing markers, flush/read errors, unavailable encryption
  or unequal cleartext/encrypted command sequences, and never moves old Last
  Session files or initializes platform-session state.
- **Expected rebase risk:** medium because encrypted session rollout stages,
  command-storage file ownership and full session rebuild hooks may move.
- **Removal/upstream plan:** retain until Chromium offers a public durable
  Current Session reset-and-readback receipt with equivalent dual-write and
  embedder extra-data guarantees.

## `0017-ahoi-session-receipt-durability.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the Current
  Session receipt seam.
- **Affected paths:** Chromium's session command-storage backend/manager and
  their focused unit tests.
- **Rationale:** a successful receipt must survive a crash that follows a new
  Current Session file creation. The file and, on POSIX, its containing
  directory are therefore flushed before readback. Decode, marker, flush or
  reopen failures close the suspect backend and notify the existing delegate
  so its normal full-session rebuild repairs persistence.
- **Rejected alternatives:** continuing to append to a file that failed its own
  readback, treating file-only flush as directory-entry durability, or
  returning an error without scheduling Chromium's established rebuild path.
- **Tests:** visible Arc import/restart/recovery proof first; then the focused
  corrupt-file close/reset and manager delegate-notification cases.
- **Security/privacy impact:** no session payload leaves the profile. Receipt
  failures remain fail-closed and now avoid extending a corrupt Current file.
- **Expected rebase risk:** low; this is a narrow durability/error-propagation
  follow-up around the APIs introduced by patch 0016.
- **Removal/upstream plan:** fold into an upstream durable Current Session
  receipt API if Chromium adopts one.

## `0018-ahoi-arc-import-web-component-build.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after session receipt
  durability.
- **Affected paths:** Chromium Settings WebUI `BUILD.gn` only.
- **Rationale:** `web_component_files` requires a raw sibling `.html` and then
  generates its `.html.ts` wrapper. The Ahoi section already provides an
  authored Lit pair (`.ts` plus `.html.ts`), so declaring it as a web component
  both duplicated the generated JavaScript output and later made Ninja require
  a nonexistent raw `.html`. The fix keeps both authored modules in `ts_files`;
  the existing `css_files` entry continues to generate the style wrapper.
- **Rejected alternatives:** adding a second raw HTML source, renaming a
  generated output, or maintaining two competing template pipelines.
- **Tests:** successful GN generation and installed import-surface journey first,
  followed by the focused Settings WebUI test and ordered patch composition.
- **Security/privacy impact:** none; this changes build ownership only.
- **Expected rebase risk:** low; remove the follow-up when patch 0013 is folded
  or rebased.
- **Removal/upstream plan:** squash into the standard import-surface patch after
  the feature wave is accepted.

## `0019-ahoi-session-receipt-callback-adapter.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the Arc WebUI
  build declaration correction.
- **Affected paths:** Chromium session command storage manager only.
- **Rationale:** `PostTaskAndReplyWithResult` delivers the backend's single
  `ReadCommandsResult` value. The durability follow-up bound that value directly
  to a callback expecting separate `(commands, read_error)` arguments. Reusing
  Chromium's existing `OnBackendReadFinished` adapter preserves the manager's
  fail-closed rebuild callback while moving the command vector exactly once.
- **Rejected alternatives:** exposing the backend's nested result type through
  the manager header, adding three duplicate lambdas, or weakening the delegate
  rebuild path on receipt errors.
- **Tests:** successful `chrome` compile first; after installed runtime
  acceptance, the parameterized current-session receipt manager tests cover
  success, dual-write mismatch, empty reset, and delegate error propagation.
- **Security/privacy impact:** none; the adapter remains in-process and does not
  log or export session commands.
- **Expected rebase risk:** low; this composes existing Chromium callback types.
- **Removal/upstream plan:** squash into the session-receipt durability patch
  after the feature wave is accepted.

## `0020-ahoi-sidebar-navigation-non-overlap.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the current
  session callback adapter.
- **Affected paths:** Chromium's tabbed browser layout and vertical sidebar
  region implementation/header.
- **Rationale:** a mounted docked, floating or edge-revealed sidebar is a hard
  obstruction for browser chrome even when it remains an overlay for page
  content. The navigation surface now stays beyond the actual sidebar card
  through reveal/hide animation, while temporary edge reveal shares the
  content card's leading inset and cannot expose the former 6-DIP sliver.
- **Rejected alternatives:** painting the toolbar above the sidebar, shrinking
  omnibox controls, resizing WebContents for floating presentation, or hiding
  the visual collision behind a clipping layer.
- **Tests:** visible docked/floating/edge-reveal and narrow-window journeys
  first, then the focused floating-browser geometry/animation browser tests,
  ordered composition and the exact installed-bundle crash-difference gate.
- **Security/privacy impact:** none; geometry and hit testing only.
- **Expected rebase risk:** medium because Chromium owns top-container and
  vertical-tab layout sequencing.
- **Removal/upstream plan:** retain while Ahoi owns a floating sidebar; rebase
  onto a future upstream obstruction API if one becomes available.

## `0021-ahoi-zero-tab-extension-context-menu.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the navigation
  non-overlap correction.
- **Affected paths:** Chromium's extension context-menu model and its
  parameterized browser test.
- **Rationale:** Ahoi supports a real zero-tab window. The per-extension
  context menu must therefore retain generic pin/manage/options actions while
  omitting site- and tab-bound sections, and every active-tab lookup must fail
  closed instead of dereferencing a null `TabInterface`.
- **Rejected alternatives:** disabling the extensions menu in empty windows,
  converting Chromium's status pin affordance into a different toggle, adding
  a synthetic tab, or swallowing the resulting process crash.
- **Tests:** visible menu/pin/context-menu E2E on the installed zero-tab
  candidate first, followed by feature-on/off browser coverage for construction,
  pin/unpin and absence of page/side-panel controls.
- **Security/privacy impact:** site-specific permissions never appear without
  an active page; generic navigation uses the owning browser window and stays
  within Chromium's normal navigation and extension-management paths.
- **Expected rebase risk:** low-to-medium because the extension menu is being
  redesigned upstream.
- **Removal/upstream plan:** remove when upstream makes the extension context
  menu natively safe for windows with no active tab.

## `0022-ahoi-zero-tab-split-command.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the zero-tab
  extension context-menu guard.
- **Affected paths:** Chromium's central split command, command controller,
  action registry and tab-strip delegate browser test.
- **Rationale:** Ahoi supports a real zero-tab window. Invoking Split there
  previously passed `kNoTab` (`-1`) into `TabStripModel::IsTabPinned()` and
  aborted the browser. The central command now seeds one regular NTP, then
  creates the second pane through Chromium's normal split path; shortcut and
  ActionItem entry points also tolerate a missing active tab.
- **Rejected alternatives:** disabling the visible split action, swallowing
  the fatal check, or synthesizing a non-model WebContents.
- **Tests:** the installed zero-tab Split-button journey first, followed by the
  focused delegate browser regression proving two tabs with one shared split
  ID and the no-new-Crashpad-dump difference gate.
- **Security/privacy impact:** none; both panes use Chromium's ordinary local
  new-tab URLs and tab model.
- **Expected rebase risk:** low-to-medium because upstream owns split command
  registration and may eventually make the empty state native.
- **Removal/upstream plan:** remove when every upstream split entry point is
  explicitly zero-tab safe.

## `0023-ahoi-sidebar-presentation-geometry.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** the exact M152 pin above, applied after the zero-tab
  split command correction.
- **Affected paths:** Chromium's tabbed browser layout and vertical sidebar
  region implementation/header.
- **Rationale:** sidebar pixels, shadow, toolbar obstruction, docked viewport
  reservation and content-card leading gutter now consume one per-frame visible
  extent. Only the Ahoi card moves; the native region remains fixed. This
  removes the stale toolbar gap and the 8-DIP content-shadow rail/notch while
  preserving non-overlap throughout reveal and hide motion.
- **Rejected alternatives:** keeping a full-width obstruction until unmount,
  moving the complete native region layer, fading the artifact, or covering it
  with a second overlay.
- **Tests:** visible docked/floating/hidden transitions and zero-tab journey on
  the exact installed candidate first, then focused animator, geometry browser,
  ordered composition and source-binding checks.
- **Security/privacy impact:** none; presentation geometry only.
- **Expected rebase risk:** medium because Chromium owns vertical-tab layout,
  shadow and top-container sequencing.
- **Removal/upstream plan:** fold into the navigation non-overlap patch after
  runtime acceptance, or rebase onto an upstream presentation-progress API.

## Overlay-owned M152 compile corrections

The following follow-up fixes intentionally live in `overlay/chromium/src`
rather than the ordered patch series because they modify files already owned
by the overlay. Listing them as patches would apply each change twice during
deterministic composition.

- Arc Settings events use `CrLitElement.fire()` with the existing
  bubbling/composed detail contract.
- Arc journal partial I/O uses bounded `base::span` subviews.
- Arc split receipts use Chromium `raw_ptr` fields for non-owning session
  references.
- The CloudKit provider Core lifecycle is defined out of line to satisfy the
  Chromium style plugin without changing shutdown semantics.
- Sidebar split actions include M152's public tabs header, and the row paint
  unit no longer duplicates constants owned by its interaction unit.
- Sidebar presentation motion accepts the host's complete travel distance and
  exposes one clamped visibility fraction to Chromium layout and shadow code.
- Arc tests directly depend on the public session bridge API they include.

These corrections are covered by the same compile, visible runtime, focused
test, and overlay-composition gates described by the owning feature sections
above.
