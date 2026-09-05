# Active Mobile checkpoint

Last updated: 2026-09-05. Owner: thread
`01a044d6-1545-7532-8394-6b7df1144bb1`.

The accepted scope is the **Internal Beta Ready** definition in
`outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`. That release-first
contract supersedes the former full-matrix execution order. Do not restart the
old download, performance, iPad or uBlock feature waves.

## Ownership

- Work only in `apps/AhoiMobile`, Mobile-only fixtures/tests, Mobile outputs,
  and this checkpoint.
- Do not stage or edit the active Desktop Arc, AnyChat, or uBlock files.
- Desktop and bookmarks work concurrently on the same branch. Their latest
  checkpoints explicitly leave Mobile source and this checkpoint to this thread.
- Shared registries remain untouched; the internal-beta evidence does not
  silently mark the full public-release matrix green.
- uBlock on Mobile remains a documented feasibility boundary, not a beta gate.

## Exact current boundary

- Branch: `codex/desktop-core-feature-wave-20260830`
- Mobile source: `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`, version `0.1 (10)`.
- Build 10 is processed and distributed to the existing internal TestFlight
  group. App Store Connect app `6808754773`, build
  `84cf0b2e-c1b9-4ff7-b104-8d99cce8ae9f`.
- Disposable build outputs remain under
  `/private/tmp/ahoi-mobile-ab2e709-e2e.1QKjtT/` (`DerivedData`,
  `candidate-receipt.json`). The clean `repo` worktree was retired after its
  source-equivalence and XcodeGen checks; its full source is retained at
  `ab2e709` in the canonical history. No new product build is needed.
- Dedicated simulator: `41FF3C64-92FF-44D9-9C32-29F9B9D0D9B0`,
  `AhoiMobile Build10 E2E Fresh`, iPhone 17 Pro Max / iOS 26.5.
- Exact test runner:
  `DerivedData/Build/Products/AhoiMobile_iphonesimulator26.5-arm64-e2e-fresh.xctestrun`
  below the existing build directory.
- Installed app was rechecked against the receipt: app-tree hash
  `ef0d813072dd3b772f3ea2d8c68f1272baf1d3842a161f72d07a3e54e2d1bdc5`;
  executable receipt-domain hash
  `7912f7d27bfe4f158a7c6ef6102be9d3210690fabd37873ac1a4251e703a09b9`.
  These are the domain-prefixed hashes from `mobile_evidence_artifacts`, not
  plain `shasum` values. The simulator app is DebugLocal, not the signed IPA.
- Current product and CloudKit source are unchanged since `ab2e709`.
  `afa5cb6..ab2e709` changes only generated project version settings, so the
  real Development CloudKit pass on signed build 9 remains applicable to that
  same sync implementation; it does not prove Production transport.
- Device readback at 2026-09-05 07:15 CEST still finds `Servusla` on developer
  build `0.1 (9)` (`builtByDeveloper=true`). Official TestFlight `4.3.1 (681.1)`
  is now installed; its initial terms approval and physical Build 10 install
  remain external gates. Mirroring currently reports the phone is in use.
  The iPad (6th generation) is below the required OS version.

## Current bounded acceptance

- `e2e-harbor-collapse-build10-astra.xcresult`: 1/1 passed on the exact
  simulator candidate; deliberate document scroll collapses the deck and
  reverse scroll restores it.
- `e2e-cold-launch-build10-astra.xcresult`: failed because synthetic address
  entry lost characters and its chunked retry lost the editor. Keep this result
  red. Direct Computer Use reproduced loss under rapid synthetic key injection;
  individually delivered keys with intervening observations entered the exact
  address, and normal `https://example.com/` plus IANA navigation loaded visibly.
- Current manual Golden Smoke has confirmed launch, HTTPS/visible lock and
  origin, second-page navigation, Back, Reload, an additional normal tab, a
  private tab without normal suggestions, and separate normal/private tab lists.
- Screenshots are retained under
  `docs/audit-evidence/2026-09-04-mobile-testflight-fix/golden-*.png`.
- The download overview and exact-candidate sync-status UI test are now green;
  `golden-sync-status.xcresult` passed 1/1 including restart and fail-closed
  local-only behavior.
- After the visible smoke, `internal-beta-core.xcresult` passed 213 tests with
  two expected entitlement skips; `internal-beta-analyze.log` ends with
  `ANALYZE SUCCEEDED`.
- The repository suite passed 38/44. Five receipt-fixture failures came from
  macOS's `/var` symlink; the root is now canonicalized in the fixture. One
  stale sync test expected the old zero-argument call; it now checks the
  bounded durable read model. Production source remains unchanged. The focused
  rerun passed **7/7** in 1.026 seconds, covering all six original failures and
  one already-passing test. The initial red run is retained, not relabelled.
- XcodeGen 2.46.0 generated the exact source project without a diff; its hash
  remains `bd7c59414bfa66ef5804c7e3de95535a71dd304daef5c7a8ef45b51c725af016`.
  Redacted Mobile-source, CloudKit-package and evidence scans found no leaks.
  The unchanged CloudKit package reuses its verified 36/36 result.
- Raw results/logs and the signed IPA have been copied to canonical, ignored
  `artifacts/e2e/0.1-10-ab2e709-internal-beta/` for durable local inspection.
- The user locked `Servusla`; mirroring now connects and the official TestFlight
  app has been installed. Its first-launch terms await explicit user approval
  requested through the asynchronous question. Do not accept them without the
  answer. Build 10 itself is not yet installed.
- Follow-up readback at 07:15 CEST still finds developer Build 9. Mirroring
  reports that the iPhone was used and disconnected; reconnect only when the
  device is available. No approval of the TestFlight terms has been received.
- Lightweight final readbacks passed: both receipt JSON files parse, owned
  diffs have no whitespace errors, and all Mobile Swift sources/tests remain
  at or below 800 lines. The shared branch's latest Desktop commits are outside
  Mobile ownership; the index was empty at the latest check.

## Internal-beta handoff

The earlier CPU gate cleared at 07:13 CEST; the remaining targeted checks then
passed. No Mobile build or test remains running. The closure changes consist
only of the two Mobile repository test corrections and Mobile acceptance
documentation/screenshots. The closing DCO-signed Mobile commit on the shared
branch is limited to these paths; Desktop acceptance remains owned by its thread.

The local acceptance is complete. Do not reopen the full matrix or create a
successor build without beta feedback or a new release-blocking finding.

Remaining external work is explicit and does not block this internal-beta
scope: user approval of TestFlight's first-launch terms, physical Build 10
install and short HTTPS smoke; a compatible physical iPad; Production CloudKit
and a Mac/iPhone/iPad domain roundtrip; public TestFlight review/link;
default-browser grant and post-grant build; public Store release approval.
The receipt and report are in
`docs/audit-evidence/2026-09-04-mobile-testflight-fix/`.
