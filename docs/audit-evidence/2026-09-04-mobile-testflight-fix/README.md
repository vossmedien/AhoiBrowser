# AhoiBrowser Mobile TestFlight build 10 evidence

Date: 2026-09-04 (Europe/Belgrade)

## Candidate binding

- Source commit: `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`
- Configuration: `TestFlightBootstrap`
- Version/build: `0.1 (10)`
- Team: `248AJ5BN47`
- Bundle: `app.ahoibrowser.AhoiBrowser`
- Exported IPA SHA-256: `d77e810b8f3c2a49617519ff09d7948c256c46ead85940da6d0664e3e0b5c7b1`

The archive and App Store export completed successfully. The exported app
passed strict deep code-signature verification and is signed by `Apple
Distribution: Christian Voss (248AJ5BN47)`. Its signed entitlements contain
Production APNs and CloudKit, `iCloud.app.ahoibrowser.AhoiBrowser`, the
dedicated sync and command Keychain groups, and `get-task-allow=false`.

The App Store package inspection verified both the app and the embedded core
framework before upload:

- `AhoiMobile.app`: `0.1 (10)`, source stamp `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`
- `AhoiMobileCore.framework`: `0.1 (10)`
- build mode stamp: `TestFlightBootstrap`

There are no Mobile runtime-source or CloudKit package changes between the
physical Development-container E2E pass at `afa5cb6` and this release
candidate. The only Mobile release correction after that pass is the
regenerated Xcode project carrying the already-declared framework
marketing/build versions.

## Rejected build 9 and correction

The first upload attempt used source `afa5cb6`, version/build `0.1 (9)` and IPA
SHA-256 `0c48f2590fb4809acae773d465f8f330fd572482a3ddc7e2478dce0105c0b65c`.
App Store Connect rejected it with validation code `90057` because the stale
generated project omitted `CFBundleShortVersionString` from
`AhoiMobileCore.framework`.

`ab2e709` regenerates the checked-in Xcode project from `project.yml`. A
pre-archive build-settings check and inspection of the archive and exported
IPA all independently reported `AhoiMobileCore.framework` as `0.1 (10)`.

## App Store Connect result

The corrected upload completed at 2026-09-04 22:44 CEST with `Upload
succeeded` and was then verified in the visible App Store Connect UI:

- App Store Connect app ID: `6808754773`
- Processed build ID: `84cf0b2e-c1b9-4ff7-b104-8d99cce8ae9f`
- Internal status: `Im Test`
- Internal group: `AhoiBrowser Intern`
- Internal group ID: `eecba011-68ca-4fe2-9c3a-12dd859ee78f`
- Assigned builds in the group: 2 (`0.1 (8)` and `0.1 (10)`)
- Invited internal tester: `christian@vossmedien.de`

## Runtime evidence and remaining boundaries

Before release packaging, the same Mobile product sources passed a real
Development-container CloudKit journey on physical iPhone 16 Pro Max/iOS
26.6.1: encrypted active record write/read, privacy-field assertion, encrypted
tombstone write/read and authenticated cleanup. The focused recovery and
persistence suites then passed 9/9.

This receipt proves archive, export, server validation, upload, processing and
internal TestFlight distribution. It does not claim a physical TestFlight
installation, a Production CloudKit Mac-iPhone-iPad roundtrip, external Beta
Review, a public TestFlight link, the managed default-browser grant, or a
post-grant build.
