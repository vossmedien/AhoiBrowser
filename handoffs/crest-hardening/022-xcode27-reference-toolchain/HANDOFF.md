# 022 – User decision: Xcode 27 is also the reference and release toolchain

Status: ready
Owner lane: desktop (toolchain, build scripts, Master text)
Base: user statement of 25 September 2026: "wir nutzen nur noch 27"

## Decision

Xcode 27.0 (27A266a) with macOS SDK 27.0 (26A425) and iOS SDK 27.0 (24A430) is
now the only toolchain, for development and for the pinned reference and
release builds. It replaces Xcode 26.6 (17F113) with SDK 26.5 (25F70 / 23F81a).
This extends the 24 September decision, which authorized Xcode 27 for
development only.

## Required changes (desktop owns them)

- `config/toolchain.json`: move the `xcode.requiredVersion` / `requiredBuild`
  and the `sdks.macOS.chromiumOfficial*` / `sdks.iOS.pinnedReference*` values
  to the Xcode 27 identifiers above, or collapse `compatibleDevelopment` into
  the reference.
- The scripts that select the pinned toolchain (`scripts/lib/common.sh`
  `ahoi_select_pinned_xcode`, `check-host.sh`, `build-upstream.sh`,
  `build-ahoi.sh` for `release`/`full-release`, `run-chromium-hooks.sh`,
  `apply-overlay.sh`) and their repository tests.
- `mac_sdk_min` in `config/build/*.gn` if the 26.5 minimum is only there for
  the old SDK. The deployment target is a separate product decision.
- Remove the "Xcode 26.6 / SDK 26.5 reference" row from the owner-gated table
  of `docs/ACTIVE_DESKTOP_CHECKPOINT.md`, and update the Master sections that
  name 26.6.
- Build receipts: `engineInputKey` already includes the toolchain identity, so
  release receipts change their key once, by design.

## Effect on crest-hardening

H3 budget verdicts no longer wait on an owner-gated toolchain. They need an
`upstream-release` control and an `ahoi-release` candidate, both built with
Xcode 27, and one measurement on a quiet host
(`docs/PERFORMANCE_METHODOLOGY.md` updated accordingly).
