# Mobile setup candidate — DebugLocal19

Source6b0121f76b5ba7b8bd2e32cbf5689b915db81e77, product-only arm64 iOS Simulator
build38455, TERMINAL EXIT0. This is the normal integrated Mobile follow-up, not
an icon-only build. It includes the approved df03d46 icon, the Home-selector
coupling correction, existing-layout host-width improvement, and typed shared
extension-setup metadata/consent UI from the same branch.

## Candidate binding

candidate.json was created with the existing mobile_release_candidate_receipt
tool against the clean own snapshot, generated project and signed app. It
confirms DebugLocal0.1(19), embedded source6b0121f, arm64 Simulator platform,
deep/strict ad-hoc signature, Xcode26.6/17F113 and Swift6.3.3.

- App tree: a120dea9e97c1dda73f9668025984da5f97d5c96b7d14d305818b65492e14d80
- Executable: 5dc78929cb7f605aa7b31d71403e23e578752082af8cdb4cd4894aac32c9bd1f
- Xcode project: 28a7a476cb56d7f27dd03284e5803ffc8314653e0f56abc6670368b443904b34
- Approved/source AppIcon-1024.png:
  690fdd145f30b7e4faaf33a475e3e3896d71f4ddd7ed42ba488b1b74f3e143d7

The icon directory is byte-identical to df03d46 in this source; native asset
compilation/linking occurred in the successful normal product build. This does
not yet prove how SpringBoard/Dock renders the icon at runtime.

Snapshot: /private/tmp/ahoi-mobile-shared-tabs.V7PCPC/repo at6b0121f.
App: /private/tmp/ahoi-mobile-shared-tabs.V7PCPC/DerivedData/Build/Products/DebugLocal-iphonesimulator/AhoiMobile.app.
The full xcodebuild invocation is at the start of build.log; build.xcresult is
retained. Existing AhoiMobile scheme/DebugLocal, generic iOS Simulator destination,
jobs2, arm64, Swift-j2 and explicit source/build stamps were used. Four product/
dependency targets only, no test suite or extra verification target.

## Preservation, resources and runtime boundary

Before reusing incremental outputs, Build18 was copied to
../mobile-browser-settings-29c42db-20260908/AhoiMobile-894c9a2.app and independently
verified against its original receipt: tree bdeb580e...a111, correct source18/
894c9a2 and signature. The installed Build18 test device remains Shutdown; it
was not replaced or launched. Existing Build17 and native6ae/4cb candidates and
the Desktop owner's protected Arc recovery were untouched.

Fresh pre-build samples:12 cores,64–66% aggregate CPU idle,44% memory headroom,
stable swapout counters,51,398,144KiB free disk. No active compiler occupied the
start sample; native Arc UI and foreign Simulator work were not interrupted.
This sample does not waive the separate120GiB Chromium-roll floor.

No UI, My-Mac host, native install, key, Portal, account or Production action
occurred. Next is a short coordinated visible icon/host-layout/metadata-section
check on THIS candidate. The previous Build18 Search/Restart/Reset result is
retained within its exact source limits and is not replayed as a full matrix.
Real CloudKit/Mac extension restoration and the full goal remain open.
