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
- **Affected paths:** 227 existing Chromium integration paths. They cover the
  native browser frame/sidebar and command bar, two-to-four-pane split tabs,
  sessions/startup, history/device tabs, settings, HTTP authentication,
  cookie/cache/privacy policy, extensions, autoscroll, macOS drag handling,
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
  Chromium format/checkdeps/build gates, focused native and browser tests, and
  installed-bundle visible E2E evidence. The patch is binary/full-index and is
  applied with `--whitespace=error-all` against the exact baseline.
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
- **Affected paths:** the Glic interactive-test aggregate, renderer context-menu
  implementation/test aggregates, and desktop settings implementation.
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
