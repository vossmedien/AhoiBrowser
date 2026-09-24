# Focused Mobile Format-3 regression — 24 September 2026

Source commit: `7d7a92a83ae609c5d3ac266bf9b7337308779a94` in the clean detached test worktree at `/private/tmp/ahoi-mobile-url-policy.fdlqj6/repo`. Compared with the visibly accepted Mobile source `45330d2de8ff75e0d50bbbc88a4a3de9aaea8aa5`, there is no diff under `apps/AhoiMobile/Sources`, `apps/AhoiMobile/Config`, or `apps/AhoiMobile/AhoiMobile.xcodeproj`; this commit changes tests, not the installed product.

Toolchain: Xcode 27.0 (27A266a), iPhone 17 Pro simulator A168/iOS 27.0 (24A434), `DebugLocal` test action, one build job, parallel testing disabled. The run used the cached isolated DerivedData and a fresh result bundle with source stamp `AHOI_SOURCE_COMMIT=7d7a92a83ae609c5d3ac266bf9b7337308779a94` and test-host build number 41. The two filters were `AhoiMobileCoreTests/SharedTabCreationProvenanceTests` and `AhoiMobileCoreTests/SharedTabFrozenContractTests`; neither file was excluded from compilation.

`xcodebuild test` exited 0. The archived [result bundle](focused.xcresult) reports **6 passed, 0 failed, 0 skipped**. It covers the immutable creation register through edit/merge/restart, the Format-3 golden wire and target kinds, rejection of obsolete/missing fields, and peer-capability readiness. Both A168 and C645 were Shutdown after the run. Existing unrelated Swift warnings were emitted, but no test failure.

This is a focused unit regression, not a newly installed or visibly exercised Build41, not a CloudKit transport test, and not a Mac–iOS encrypted record roundtrip. The last visible Mobile Development candidate remains Build40/source `45330d2`.
