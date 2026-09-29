# Integrated Mobile candidate — DebugLocal20

Product-only build28517 on exact clean bbe2c53b21f0911c0074b97a606468268a7387e9
completed **EXIT0**. Four product/dependency targets; no tests, Simulator boot,
app install/launch, Keychain access, CloudKit request or native signing action.

The regular candidate includes the existing approved icon/host layout,
reviewed extension-storage metadata packet2b19ca6 and matching Swift first-use
key commitment/cancellation changes. It is not an icon-only or test-only build.
Only two expected AppIntents metadata-extraction warnings occurred; no build
error or warning suppression. No native C++ code was compiled by xcodebuild.

## Exact candidate

`candidate.json` was created by the existing candidate-receipt tool against
the actual clean source, generated project, plist, signed app and toolchain.

- Source: bbe2c53b21f0911c0074b97a606468268a7387e9
- DebugLocal0.1(20), arm64 iOS Simulator, ad-hoc local signature only
- App tree: b8d840b241c9634794617c09aef355feab656d5b3e048f448b5db06cbff05e0b
- Executable: f6f301d61fd26115ac876c38d81a2e24607271cb3cc4727620e26d62cb7ff4c4
- Project: ac24726dfd1a3b05187e11732a2ff6c66323a3da4046b12a742c2e8b5e9c5104
- Xcode26.6/17F113, Swift6.3.3, iOS Simulator SDK26.5

App: `/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/DerivedData/Build/Products/DebugLocal-iphonesimulator/AhoiMobile.app`.
The same own clean snapshot is now at bbe2c53. The exact xcodebuild invocation
is recorded at the start of `build.log`; `build.xcresult` is retained. Generic
Simulator destination, jobs2/Swift-j2 and explicit source/build stamps, no host.

## Preservation and resource boundary

Before advancing the snapshot, Build19 was copied to
`../mobile-setup-6b0121f-20260908/AhoiMobile-6b0121f.app` and independently verified
against its ORIGINAL receipt: tree a120dea9...d80, exact6b0121f/19/project/signature.
Earlier Build18/17 archives and installed/shut-down own devices are unchanged.
No existing app, profile, cloud record or key was overwritten or removed.

Fresh pre-build samples showed12 cores,59–68% aggregate idle,58% memory
headroom, no new swapouts or competing compilers,47,945,224KiB free disk. That
supported this two-job incremental product compile. It does not waive the
separate120GiB Chromium-roll floor or any UI ownership requirement.

## Next acceptance

The requested short visible icon/origin/metadata journey still needs a real
shared-window handoff. Build20 is available as its integrated successor;
Build19 remains preserved. No old Simulator grant has been reopened. Build18's
search/restart/reset evidence stays within its original scope. Compiler success
does not prove key bootstrap, provider consent, extension apply or real
cross-device CloudKit. Native compilation is with Desktop; signed matching
devices/fresh isolated cloud and key configuration are separate next gates.
