# Implementation status

Last updated: 2026-08-26

This page records only completed, reproducible evidence from the current source
state. A running suite is not counted as a pass, and development signing or an
app copied to `/Applications` is not release or `CU_E2E` evidence.

## Completed current-source gates

| Gate | Result | Evidence boundary |
| --- | --- | --- |
| Chromium source freeze | complete | The exact Chromium `151.0.7922.170` pin at `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` composes deterministically from the 21-entry patch series through `0021-ahoi-upstream-page-load-tracing-test-isolation.patch`, plus the tracked overlay, to tree `a3865fc6e9f89ccd9403fa888ccce1a54d67c4e4`. The portable recovery bundle is `artifacts/build/recovery/ahoi-m151-final.bundle` (SHA-256 `7e45492983029c62297941536b2830f8e06f3f25069e8c08f42ee01477bc7dc7`). Hardened non-mutating preflight reports 21/21 `applies` at `artifacts/build/chromium-roll-preflight-m151-hardened.json` (SHA-256 `c5e9d0ec97c4b61de004b7d9d6ff3e6152503cdc20f8e24419b2145ba35e9d23`). |
| Ahoi Chromium binary matrix | `29/29 PASS` | All 29 focused Ahoi test binaries built and completed successfully against `out/AhoiDev`. This is programmatic development-build evidence, not installed-app evidence. |
| Browser/privacy policy tests | `17/17 PASS` | The focused browser policy suite, including secure component transport and browser-account network gating, completed successfully. |
| Fresh-profile idle network audit | `PASS` | `artifacts/e2e/fresh-profile-network-audit-20260826-idle.json` records only allowlisted component update/download and Safe Browsing origins, with no unknown or prohibited origins. |
| Outer repository gate | `232 PASS` | 166 repository tests, 13 HTTP-auth fixture tests, 17 general HTTPS E2E-fixture tests and 36 CloudKit model tests completed successfully. Fixture and model tests prove their local contracts only. |
| Native matrix | `36/36 PASS` | The completed native matrix passed all 36 checks. |
| Companion matrix | `33 PASS`, `2 SKIP` | The completed Companion checks passed 33 tests; two real-entitlement cases were skipped because provisioned CloudKit/device entitlements are external. Skips are not passes. |

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

The non-mutating roll discovery has verified Mac Stable `152.0.7977.65` at
`fc4d67f1788019a27e32511137ceccbd2fafdaaa` as the next candidate. The
production pin remains M151 until that exact target object has been fetched,
the full overlay and patch stack has been dispositioned against it, and the
rebased source has passed the required M152 build and runtime matrix.

## Release and installed-app boundary

No release build, notarized/stapled package, signed release manifest, updater
journey, `INSTALLED_PASS`, `CU_E2E PASS`, or `ASSISTED_E2E PASS` has been
recorded. The portable component build can be signed with an Apple Development
identity for stable local Keychain behavior, but that signature does not provide
Developer ID, Hardened Runtime, notarization, stapling or exact packaged-to-live
bundle provenance.

A valid Developer ID Application identity is available locally. Release remains
blocked until the reviewed release environment and Team ID binding, Keychain
notary profile, exact submission/acceptance, stapling and validation receipts
exist. The canonical volume currently has roughly 120 GiB free and remains about
30 GiB below the fail-closed 150 GiB clean release-build threshold. The remaining
credential, service, entitlement, assisted-device and legal gates are listed in
`config/external-gates.json`.
