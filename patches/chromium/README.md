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
