# Mobile browser settings / search choice — Build18

Product-only arm64 iOS Simulator build; no tests, simulator launch, My-Mac host,
native Ahoi installation, profile, key, Portal or Production operation.

## Runs and exact candidate

- Source29c42dbfafb095da7e091668ad728dd4b6ad64a1: build session95119 ended EXIT65.
  Product/API failure: CompanionBrowserSettingStore called an Array helper
  private to CompanionProductStore.swift. The three Swift diagnostics have one
  root cause. Original build.log and build.xcresult remain unchanged; no candidate
  pass is inferred from this run.
- One-file correction894c9a2ac3f21711a3f9ed3eba702d16413bdf9c explicitly finds the
  stable record ID and updates/appends locally. No assertion, warning, schema or
  test gate changed. The same clean snapshot and incremental outputs were reused
  only after the first build was terminal.
- Corrected product build session48865 ended EXIT0, including native app/framework
  linking, ad-hoc signing and validation. Logs: corrected-build.log and
  corrected-build.xcresult. Four product/dependency targets, no XCTest target.
- candidate.json was created by the existing candidate-binding tool against the
  clean894c9a2 source, generated Xcode project and resulting application.
  It confirms DebugLocal0.1(18), source/plist agreement and deep/strict ad-hoc
  signature. App tree artifact SHA256:
  bdeb580e666a4462a5cd541e45d99ab7c41402510f264b1d6eab93243c70a111.

Existing own snapshot: /private/tmp/ahoi-mobile-shared-tabs.V7PCPC/repo.
DerivedData and candidate:
/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/DerivedData/Build/Products/DebugLocal-iphonesimulator/AhoiMobile.app.

The exact command is retained at the start of each log. Both used the existing
AhoiMobile scheme, DebugLocal, generic/platform=iOS Simulator, jobs2, arm64,
OTHER_SWIFT_FLAGS=$(inherited) -j2, CODE_SIGN_IDENTITY=- and explicit source/build
stamps. Xcode26.6/17F113 and Swift6.3.3 were independently recorded by the receipt.
Before starting, overall CPU was approximately62–69% idle on12 cores, memory
headroom45%, no sustained new swapout pressure. The corrective boundary had
57–64% idle and stable swapout counters. No foreign process was touched.

## Preservation and next boundary

Before overwriting incremental outputs, the old Build17 app was copied to
../mobile-shared-intents-7b706a7-20260908/AhoiMobile-7b706a7.app and independently
verified byte-identical against its original receipt (tree d84c6358...ab87).
Its existing local Save/Restart proof remains scoped to Build17.

The delayed07:08 Simulator grant was already consumed and explicitly returned.
The own old simulatorF8253C50-E423-4424-8EE3-5F152C593A31 was freshly read as
Shutdown and was not started. A NEW short Search/Restart/Reset UI window has been
requested through coordination01a080b0-f630-7690-a7a5-4d46edc1f366, with Desktop
informed in01a080b0-f66a-70f0-ade9-5e41f3437e74. The previous cross-project
Simulator-window targeting incident requires actual surface isolation, not an
assumption that a new device ID alone owns the shared Simulator app window.

No visible Build18, Mac/iOS CloudKit roundtrip, native C++ search adapter,
extension restoration or full sync acceptance is claimed by this build.
The changed visible journey comes before the necessary focused tests.
