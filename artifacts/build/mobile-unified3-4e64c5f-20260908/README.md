# Unified Mobile product build — first compile

- Exact clean source: `4e64c5f2f4e6052c1a4aefb2d6b9c6617cf76963`.
- DebugLocal 0.1 (16), generic arm64 iOS Simulator, Xcode 26.6/17F113.
- Reused isolated source/DerivedData: `/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/`.
- Product-only `xcodebuild build`, two build/Swift jobs; four product graph
  targets, no test target or host. Full invocation is in `build.log`.
- Session `1526` is terminal **EXIT 65 / BUILD FAILED**. No installation.
- Classification: product compiler error. `MobileBrowserController.swift:724`
  retained the old direct `noteExplicitSharedNavigation` call for external links.
  The new helper is deliberately private/commit-driven, not an API to expose
  again merely to make compilation succeed.
- Exact correction `bba0b86ad2a4b67fe0c6ff0ca763a3ca1e6bfabb` changes only that
  route to prepare its navigation intent before `page.load`. No warning or
  assertion is weakened. The next incremental product build uses a fresh result
  bundle; this red log/result is retained.
- No app start, simulator/My-Mac host, test, CloudKit, portal, profile or key
  mutation was performed. This is not a new runnable-candidate or Sync pass.

Pre-build aggregate resource checks had 50.29% CPU idle, 39% pressure-reported
available memory and no new swap-outs over the confirming interval. The old
single-process 80% / special Chromium priority rule was not used.

## Corrected product candidate

- Exact `bba0b86ad2a4b67fe0c6ff0ca763a3ca1e6bfabb`, same Build16/configuration/
  toolchain/isolated snapshot and two-job limits; no changed unrelated target.
- Session `11655`: terminal **EXIT 0 / BUILD SUCCEEDED**, linked and ad-hoc signed.
  Full invocation/output: `correction-bba0b86.log`; result bundle alongside it.
- Embedded source, Build16 and DebugLocal independently read back. App codesign
  `--verify --deep --strict` succeeded. The existing receipt tool bound the clean
  source and exact app in `candidate-bba0b86.json`.
- App tree artifact hash:
  `153d0d7d5b339c12424aa22d427ce9b7716c45e779b8fcd8e30bb68b1e250fad`.
  Receipt file hashes are domain-separated by the existing evidence tool and
  deliberately differ from raw `shasum` digests.
- AppIntents metadata extraction reported its existing no-framework warnings;
  no Swift/compiler warnings were suppressed. No tests were built as an
  installation prerequisite or executed.
- Not installed/launched yet. Visible acceptance awaits the explicitly requested
  Simulator UI slot; Desktop/My-Mac/profile/CloudKit/portal/keys remain untouched.
