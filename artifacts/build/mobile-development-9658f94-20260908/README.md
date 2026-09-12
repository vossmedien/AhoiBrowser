# Unsigned iOS Development counterpart

## Development signing completed — 12 September 2026

The preserved Build22 app now has a separate Apple-Development-signed copy,
`AhoiMobile-9658f94-development-signed.app`. Its tree hash is
`c28c15105060d9004433e5d91881fc9a694e928ed71e64e1e8ec3e58053953c5`.
Both frameworks and the two embedded dylibs were signed before the app;
`codesign --verify --deep --strict` passes. Exact signed entitlements match
`Development22.entitlements.plist`, and the existing valid Ahoi Development
profile authorizes this certificate, the connected Servusla device, Development
CloudKit/push and both separate Keychain groups. No new build, account login,
Portal provisioning or key access was needed.

The unsigned original was independently rehashed unchanged. The signed copy's
Info.plist is byte-identical and all seven runtime values match the shared
scope. The new receipt is `development-signing-receipt-20260912.json`; the
original unsigned `candidate.json` remains unchanged. Raw file hashes in the
new receipt differ in convention from the tagged file hashes in that original.

No installation, app launch, Keychain or CloudKit work has run. Servusla is
paired/reachable with Developer Mode enabled, but the current read-only device
query reports `passcodeRequired: true` and unavailable DDI services. The host
has a usable compatible 17F113 DDI. The targeted `devicectl device info ddiServices`
diagnostic resolves CoreDeviceError12040 to `0xe80000e2
kAMDMobileImageMounterDeviceLocked`: the device lock prevents mounting. A real runtime continuation requires device
unlock and the coordinated matching Mac candidate; this is not a transport pass.

## Original unsigned-build evidence

Exact9658f945d7c0a80b6b5b331d6fecb4be3e40bb10 / CloudKitDevelopment0.1(22)
completed the ordinary four-product-target iOS-device build48197 with EXIT0.
It is NOT a signed, installable or runtime-accepted CloudKit candidate.
The archive copy is byte-identical to the build output; candidate.json binds
its source, actual iPhoneOS Mach-O platform, SDK, hashes and embedded seven-value
Development tuple. No app was launched, and no keys/Portal/cloud were accessed.

The original77061ad/21 run48954 failed at the real Mobile preflight: the normal
payload-key-only constraint rejected our already agreed isolated family. Its
unchanged log, input and XCResult remain in ../mobile-development-77061ad-20260908/.
Fix9658f94 accepts only an explicit UUIDv4-matched zone/subscription/account in
CloudKitDevelopment; public identity, groups, service, key version, source and
environment checks remain. Three narrow preflight CLI tests passed in4.453s.
No app/domain/CloudKit test suite was added as a build prerequisite.

The same existing own Mobile snapshot/DerivedData was reused. Generating the
project in the temporary directory changes only external-resource display-group
IDs because its basename is repo; that own generated-only delta was removed,
and the build used the portable committed project in a clean exact snapshot.
Before reuse, UI Build20 was archived byte/signature-identically. It remains
the separate, unstarted Simulator journey and was not replaced by22.

Fresh start:82.99–84.29% aggregate CPU idle/12 cores,66% memory headroom,
unchanged swapout count,32,324,440KiB free and no foreign compiler detected.
The limited cached Mobile build does not waive the separate Chromium floor.
Two normal AppIntents metadata warnings were retained; no warnings were disabled.

Next actual gate: coordinate Development signing of a COPY for an eligible
iOS device and a matching Native Mac candidate using this exact shared scope,
then visible opt-in/link and real cross-device data transfer. Existing Apple
account/group boundaries and the actual shared-key bootstrap remain required;
never manually copy key bytes or provision separate independent peer keys.
No native checkout/out, installed Mac app or old profile/Arc journal changed.
