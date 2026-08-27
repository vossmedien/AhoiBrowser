# Testing and evidence

AhoiBrowser uses four complementary test classes:

- **UNIT**: deterministic logic, models, serializers, conflict resolution,
  policy matching, and isolated UI state.
- **INTEGRATION**: Chromium services/processes, profile semantics, extensions,
  networking, media fixtures, CloudKit adapters, packaging, and updates.
- **CU_E2E**: visible Computer Use interaction with the signed app installed at
  `/Applications/AhoiBrowser.app`.
- **ASSISTED_E2E**: the same visible flow where a human must provide Touch ID,
  account login, a DRM entitlement, or another non-automatable action.

Terminal/CDP inspection may corroborate state but cannot replace visible user
interaction for CU tests. Debug builds, app bundles outside `/Applications`,
screenshots of mock UI, or another Chromium browser are not product evidence.

For local feature acceptance, install the signed `AhoiDev` candidate through
`scripts/install-dev-app.py` as documented in `docs/BUILDING.md`, then perform
the real visible Computer Use journeys before running the corresponding
programmatic suites. If implementation or later programmatic checks expose a
defect, rebuild/reinstall and repeat the affected visible journey before
recording the result. The development installation receipt binds the exact
candidate and transaction, but it is not notarized release evidence and cannot
turn a `CU_E2E` release gate into `PASS`.

## Status model

Allowed statuses are machine-readable in `config/test-statuses.json`. Only
`PASS` satisfies a release gate. `NOT_RUN` and blocked states are honest
non-success outcomes; they must include the exact missing condition, owner, and
next action. A `PASS` must not retain blocker metadata.

## Evidence layout

```text
artifacts/e2e/<version>/<test-id>/
  result.json
  steps.json
  screenshots/
  video/                 # when useful and privacy-safe
  diagnostics/           # redacted process/network/readback evidence
  environment.json
```

The strict v2 `result.json` contract records test ID and registry requirement,
executor, clean source SHA, product version, Chromium version and commit, actual
bundle marketing/build/channel/source metadata, path and hash, signing team and
authority, Hardened Runtime/notarization verification, locale, theme, device/OS,
profile type and start state, exact expected/actual results, steps, assertions,
typed evidence, artifacts, and blocker details. Secret values and private
browsing content are prohibited.

For a `CU_E2E` or `ASSISTED_E2E` PASS, `tools/evidence.py validate` re-hashes
the current `/Applications/AhoiBrowser.app` and independently rechecks its
product identity, ARM64 architecture, complete signature, Gatekeeper
assessment, and notarization staple. Booleans copied into JSON are not accepted
as proof by themselves. Visual evidence and the successful repeat-run receipt
must resolve inside the test package and match their recorded SHA-256 values.
Every PASS class, including integration tests, requires at least one real test
report; repeat receipts, reports, screenshots, videos, redacted logs, fixture
receipts, and network captures are package-local files with SHA-256 bindings.
The validator also binds the live app's version/build/channel/source stamps to
the checked-out release commit. Expected Developer ID authority and Team ID are
provided through the local release environment and never inferred from claims
inside the result JSON.

Those checks are necessary but not sufficient to prove that the installed
binary is the output of the recorded build. `config/release-evidence.json`
therefore keeps `CU_E2E` and `ASSISTED_E2E` PASS fail-closed. Even after that
reviewed gate is enabled, every PASS validation must supply both
`--release-manifest /path/to/release-manifest.json` and
`--release-public-key /path/to/release-public.pem`. The evidence validator calls
the release-chain validator with those explicit paths and
`installed_app=/Applications/AhoiBrowser.app`; it verifies the manifest
signature, trusted key ID, all bound receipts and artifacts, and the live
installed bundle. Matching receipt-name strings, plist stamps, signing
booleans, screenshots, or a valid Developer ID signature cannot enable the gate
by themselves.

## Required matrices

The master target defines PKG, UI, A11Y, TREE, WS, SPLIT, CMD, QUICK, INC, NAV,
DEFAULT, DL, AUTH, MEDIA, DRM, PERM, EXT, UBO, DEV, PRIV, SEC, SYNC, IOS,
UPDATE, CRASH, and PERF suites. Each identifier gets a registry entry before
implementation. Tests include fresh/existing profiles, German/English,
system/light/dark, glass on/off, restart/recovery, offline/error states, and
migration from the preceding supported version where applicable.

The native updater has deterministic unit coverage for config validation,
stable/beta/nightly visibility, status transitions and de/en/en-GB strings.
Repository tests additionally reject the vulnerable Sparkle pin, dependency
material tampering, insecure/foreign-channel appcasts, malformed signatures,
unsigned content after the signed-feed trailer, missing production feed/key
policy, and incomplete Sparkle signing roles. These are static gates only;
UPDATE-01 through UPDATE-10 still require the installed signed application and
real HTTPS appcasts/artifacts.

The SPLIT suite has explicit model/browser integration cases and visible
installed Computer Use cases. Integration covers two-/three-/four-member collection
invariants, canonical layout serialization, Session/Tab Restore migration and
extension `splitId` behavior. Computer Use then performs real vertical-sidebar
drag/drop, two-, three-, and four-pane layout including 2×2, divider, focus/security, media/PiP,
permissions, DevTools, incognito, crash/restart, accessibility, localization,
and performance journeys. Upstream Chromium's two-pane tests do not substitute
for Ahoi's macOS drag or installed multi-pane evidence.

## Real services and local fixtures

Deterministic local HTTPS fixtures exercise uploads/downloads, Basic/Digest
realms, WebRTC permissions, media/PiP, redirects, certificate failures, CSP/CORS,
cookies, extension behaviors, and crash recovery. Real-site tests verify OAuth,
1Password/Bitwarden, one major conferencing flow, Netflix plus a second licensed
DRM service if entitled, and representative developer sites. No test flag may
disable the sandbox or certificate validation.

The authentication-specific fixture lives at `fixtures/http-auth/`. Its thirteen
self-tests run as part of `scripts/test-repository.sh` and prove only the local
server and test-client contract. The installed AhoiBrowser, Keychain,
incognito, persistence, localization, accessibility, and visible account
chooser remain separate AUTH release gates.

The general cluster at `fixtures/e2e/` provides three CA-signed loopback HTTPS
origins and deterministic journeys for Range download/resume, upload and DnD,
three-pane split state, redirects/popups, synthetic OAuth, explicitly simulated
passkey plumbing, H.264/AAC/MSE/PiP, WebRTC/capture and permission prompts,
cookies/CHIPS/GPC, storage/service-worker/cache behavior, CSP/CORS/header echo,
developer injection controls, and artificial login forms. Its machine-readable
receipts retain signal presence and public hashes but never secret/query/cookie
values or uploaded bytes. Certificate generation never changes trust; the
documented macOS user-keychain install and exact-fingerprint removal commands
both require explicit confirmation. The self-tests validate TLS against only
their temporary CA and mock every trust-changing command, leaving system trust
untouched. See `fixtures/e2e/README.md` for the mandatory install/remove order
and the boundaries that remain installed `CU_E2E` or `ASSISTED_E2E` work.
The fixture README also records the media asset's observed encoder metadata,
unknown copyright/license provenance and replacement recipe; its presence is
not evidence of redistribution clearance for H.264/AAC.

The CloudKit model spike under `spikes/cloudkit/` is also part of the repository
test run. Its local Swift tests cover model convergence, sync-boundary policy,
and remote-command validation. They do not replace an entitled CloudKit
roundtrip between signed, installed Mac and iOS applications.

`fixtures/extensions/mv3-smoke/` provides a no-host-access unpacked MV3 control,
while `fixtures/extensions/mv2-denied-control/` is a negative control that must
remain blocked. Neither counts as Chrome Web Store, vendor-extension, or uBlock
Origin evidence.

## Performance

Release comparison uses the pinned unmodified Chromium build on the same machine
and fixtures. Budgets cover cold/warm launch, tab activation, workspace switch,
command-bar latency, scroll/drag responsiveness, idle CPU/wakeups, memory per
tab/process, media playback, extension overhead, and developer tooling disabled.
A visually pleasant build that regresses interaction or idle behavior is not
release-ready.
