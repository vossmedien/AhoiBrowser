# Implementation status

Last updated: 2026-08-26

This page separates completed, reproducible evidence from explicitly pending
gates for the active source state. A running suite is not counted as a pass,
and development signing or an app copied to `/Applications` is not release or
`CU_E2E` evidence.

## Active M152 source state

| Gate | Result | Evidence boundary |
| --- | --- | --- |
| Chromium Stable pin | `PASS` | `config/chromium.json` selects Mac Stable `152.0.7977.65` at exact commit `fc4d67f1788019a27e32511137ceccbd2fafdaaa`; `./scripts/verify-pin-online.sh` confirmed that pair against the official Mac Stable release and also confirmed the pinned depot_tools revision. This Chromium base is not Nightly or Canary. |
| Target-object hydration | `PASS` | `artifacts/build/chromium-checkout-hydration-m152.json` records all 461,863 unique target blobs present, zero remaining, and an unchanged worktree, index, `HEAD`, refs, `FETCH_HEAD`, and shallow boundary. This proves object availability, not dependency sync or a build. |
| Overlay/patch preflight | `PASS` (`3/3 applies`) | The tracked overlay and active series (`0001-ahoi-m152-integration-seams.patch`, `0002-ahoi-deterministic-platform-tests.patch`, `0003-ahoi-upstream-page-load-tracing-test-isolation.patch`) compose non-mutatingly to tree `d5d32d524f909da7043577d204d726b289a6d757`; see `artifacts/build/chromium-roll-preflight-m152-final.json`. |
| M152 development toolchain and dependencies | `PASS` | The development build completed with pinned Xcode 26.6/17F113, macOS SDK 26.5/25F70, iOS SDK 26.5/23F81a, checkout-pinned depot_tools, and verified temporary Chromium/V8 path workarounds. Upstream/release and development labels remain provenance-separated even though they select the same installation. |
| M152 focused tests | `PASS` | The complete sidebar suite passed `79/79`, including `CollapsingViewportDefersRegisteredTextfieldRecycling`. The current outer repository gate also passed 185 repository tests, 13 HTTP-auth fixture tests, 17 HTTPS fixture tests, and 36 CloudKit model tests. These focused development results are not the full product matrix. |
| M152 development build and installation | `PASS` | The component runtime staged 525 dynamic libraries and 238 resources. The ARM64 app was stamped to the current source, signed with the configured Apple Development identity, installed atomically at `/Applications/AhoiBrowser.app`, and verified against the built executable. Chromium is Stable; `config/version.json` still labels this Ahoi development product channel `nightly`, so this is not an Ahoi Stable release. |
| Installed visible compatibility smoke | `PASS` (development scope) | Computer Use visibly confirmed `chrome://version` as Chromium `152.0.7977.65` and the executable below `/Applications/AhoiBrowser.app`. Four fresh sidebar collapse/restore cycles and a native sidebar resize completed while the installed browser stayed alive, with no post-fix Crashpad dump. See `artifacts/computer-use/m152/README.md`. This scoped smoke is not the master prompt's full `CU_E2E PASS`. |

## Historical M151 recovery and previous green matrix

The following results remain immutable recovery and regression evidence for the
previous source baseline. They do not transfer to M152 and are not
current-source passes.

| Historical gate | Result | Evidence boundary |
| --- | --- | --- |
| M151 Chromium source freeze | complete | The exact Chromium `151.0.7922.170` pin at `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` composes deterministically from the former 21-entry patch series through `0021-ahoi-upstream-page-load-tracing-test-isolation.patch`, plus the tracked overlay, to tree `a3865fc6e9f89ccd9403fa888ccce1a54d67c4e4`. The portable recovery bundle is `artifacts/build/recovery/ahoi-m151-final.bundle` (SHA-256 `7e45492983029c62297941536b2830f8e06f3f25069e8c08f42ee01477bc7dc7`). Hardened non-mutating preflight reports 21/21 `applies` at `artifacts/build/chromium-roll-preflight-m151-hardened.json` (SHA-256 `c5e9d0ec97c4b61de004b7d9d6ff3e6152503cdc20f8e24419b2145ba35e9d23`). |
| M151 Ahoi Chromium binary matrix | `29/29 PASS` | All 29 focused Ahoi test binaries built and completed successfully against the former `out/AhoiDev`. This is historical programmatic development-build evidence, not M152 or installed-app evidence. |
| M151 browser/privacy policy tests | `17/17 PASS` | The focused browser policy suite, including secure component transport and browser-account network gating, completed successfully on M151. |
| M151 fresh-profile idle network audit | `PASS` | `artifacts/e2e/fresh-profile-network-audit-20260826-idle.json` records only allowlisted component update/download and Safe Browsing origins, with no unknown or prohibited origins on that historical build. |
| Historical outer repository gate | `232 PASS` | 166 repository tests, 13 HTTP-auth fixture tests, 17 general HTTPS E2E-fixture tests and 36 CloudKit model tests completed successfully. Fixture and model tests prove their local contracts only. |
| Historical native matrix | `36/36 PASS` | The completed native matrix passed all 36 checks. |
| Historical companion matrix | `33 PASS`, `2 SKIP` | The completed Companion checks passed 33 tests; two real-entitlement cases were skipped because provisioned CloudKit/device entitlements are external. Skips are not passes. |

The post-fix, retry-free Chromium `components_unittests` run executed all
52,855 tests: 52,854 passed and the randomized
`AggregatableUtilsNullReportsTest.ExpectedDistribution/1` exceeded its
statistical bound once. The upstream test itself documents a one-percent
failure probability; its fresh no-retry rerun passed `1/1`. The four prior
persistent failures are resolved, with additional post-fix repeat evidence of
`50/50` tracing and `100/100` site-data tests. The full suite is deliberately
not labeled retry-free green because the primary run had that one statistical
failure. Its machine-readable summary is
`artifacts/build/components-unittests-m151-summary.json` (SHA-256
`803b8f92de1a4c6a669d702fc9406383c6729a867ec878615ebcf7c429bba5bc`).

## Release and installed-app boundary

The M152 development bundle is built, signed, installed, and visibly smoke
tested. No release build, notarized/stapled package, signed release manifest,
updater journey, master-level `INSTALLED_PASS`, `CU_E2E PASS`, or
`ASSISTED_E2E PASS` has been recorded. An Apple Development signature provides
stable local Keychain behavior, but not Developer ID, Hardened Runtime,
notarization, stapling, or exact packaged-to-live release provenance.

A valid Developer ID Application identity is available locally. Release remains
blocked until the reviewed release environment and Team ID binding, Keychain
notary profile, exact submission/acceptance, stapling and validation receipts
exist. The normal checkout/build recommendation is 150 GiB free. An explicitly
supervised `AHOI_ALLOW_LOW_DISK=1` run is permitted only at or above the hard
120 GiB safety floor and does not itself count as release evidence. The actual
free-space gate is measured again at command start. Remaining credential,
service, entitlement, assisted-device and legal gates are listed in
`config/external-gates.json`.
