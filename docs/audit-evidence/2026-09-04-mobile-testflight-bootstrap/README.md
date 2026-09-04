# AhoiBrowser Mobile TestFlight bootstrap evidence

Date: 2026-09-04 (Europe/Belgrade)

## Candidate binding

- Source commit: `95241c6efa56d5a90c3aa105ac8e3d7de71b5c0e`
- Shared branch remote head after publication: `41c6856`
- Configuration: `TestFlightBootstrap`
- Version/build: `0.1 (8)`
- Team: `248AJ5BN47`
- Bundle: `app.ahoibrowser.AhoiBrowser`
- Exported IPA SHA-256: `5452d16810a2dc872ef4947f1f26c1ac571864527968bc255d1f381a4074abbf`

The release archive and App Store export completed successfully. The exported
app passed strict deep code-signature verification and was signed by `Apple
Distribution: Christian Voss (248AJ5BN47)`. Its signed entitlements contain
Production APNs and CloudKit, `iCloud.app.ahoibrowser.AhoiBrowser`, the dedicated
sync and command Keychain groups, and `get-task-allow=false`. The source stamp
and build-mode stamp match the values above.

The packaging correction in this candidate adds the required marketing/build
versions to the embedded core framework. Export inspection reported:

- `AhoiMobileCore.framework`: `0.1 (8)`
- `AhoiCloudKitSpike_58C48334F00BB1AD_PackageProduct.framework`: `1.0 (1)`

## App Store Connect result

The upload completed at 2026-09-04 21:25 CEST with `Upload succeeded` and was
subsequently verified in the visible App Store Connect UI:

- App Store Connect app ID: `6808754773`
- Processed build ID: `e8a175b4-1ab8-4bc2-a8c9-b65410a2b5e5`
- Build status in the internal group: `Bereit zum Testen`
- Internal group: `AhoiBrowser Intern`
- Internal group ID: `eecba011-68ca-4fe2-9c3a-12dd859ee78f`
- Automatic distribution: enabled
- Assigned builds: 1 (`0.1 (8)`)
- Invited internal tester: `christian@vossmedien.de`

## Remaining external boundaries

This receipt proves archive, export, upload, processing and internal
distribution readiness. It does not claim a physical TestFlight installation,
a Production CloudKit Mac-iPhone-iPad roundtrip, external Beta Review, a public
TestFlight link, the managed default-browser grant, or a post-grant build.
