# AhoiBrowser Mobile TestFlight build 10 evidence

Updated: 2026-09-05 (Europe/Belgrade)

## Internal-beta acceptance

The current scope is the seven-point **Internal Beta Ready** definition in
`outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`. It does not claim the
full public-release, hardware or multi-device matrix.

On 2026-09-05 the existing `ab2e709` DebugLocal simulator candidate was checked
against its original receipt, then exercised through Computer Use. It visibly
loaded `https://example.com/` and IANA, navigated Back and Reload, created a
normal tab and a private tab, kept their lists and suggestions separate, and
opened the download overview. The screenshots are `golden-01-https.png` through
`golden-07-downloads.png` beside this report. The same receipt-bound candidate
passed the Harbor collapse/reverse-scroll test and the local-only sync-status,
opt-in, restart and fail-closed UI test, each **1/1**.

After the visible smoke, its Core suite passed **213 tests**, with **zero
failures and two expected CloudKit-entitlement skips**; static analysis returned
`ANALYZE SUCCEEDED`. The initial Mobile repository suite passed 38/44. Five
receipt-fixture cases were blocked by the macOS `/var` symlink rather than their
intended inputs; the fixture now resolves its own root while leaving deliberate
symlink-negative inputs unchanged. One old sync source assertion was updated to
require the current bounded read model, retaining the anti-polling and
anti-transient-label restrictions. After CPU clearance their focused rerun
passed **7/7**, covering all six initial failures and one overlap. Thus all 44
distinct repository cases have passing evidence; the initial aggregate result
is retained as failed. No product code changed and no new app build was needed.

XcodeGen 2.46.0 regenerated the exact `ab2e709` project without any source diff;
the receipt-domain project hash stayed
`bd7c59414bfa66ef5804c7e3de95535a71dd304daef5c7a8ef45b51c725af016`.
The Mobile line budget remains at or below 800. Redacted scans of exact Mobile
source, the unchanged CloudKit package and the evidence directory found no
leaks. `3c1af83..ab2e709` has no CloudKit-package diff, so its retained **36/36**
package result is reused rather than rerun.

The original automated cold-address test stays **failed**. Rapid synthetic
typing dropped/reordered characters, also independently reproduced in Apple's
App Store through iPhone Mirroring. Individually delivered keys with intervening
observations entered the exact address and normal HTTPS loaded. This is a
host-input limitation, not a claimed green XCTest or a TLS exception.

The simulator is **DebugLocal**, ad-hoc signed, source-equivalent to Build 10's
product source. It is not the same binary/configuration as the
**TestFlightBootstrap** IPA. Retained local raw evidence and the signed IPA now
live below the canonical project:
`artifacts/e2e/0.1-10-ab2e709-internal-beta/`. This directory is intentionally
Git-ignored; the compact receipt and screenshots in this report directory are
the reviewable tracked evidence.

The disposable clean source worktree was removed after its no-op and
source-equivalence checks. It had no unique changes, only generated Python
caches; commit `ab2e709` and all retained release evidence remain in the
canonical project. Other tasks' worktrees were not touched.

App Store Connect was refreshed live on 2026-09-05 and still reports Build 10
as `Im Test` in the existing internal group. The physical iPhone still had
developer Build 9 at readback. After the user locked the device, mirroring
connected and the official TestFlight app was installed from the App Store.
At 07:15 CEST, readback still confirms developer Build 9 and installed
TestFlight 4.3.1. Its first-launch terms await the requested user confirmation;
mirroring reports that the phone was used and disconnected. Build 10's physical
installation and runtime are therefore a documented external gate, as allowed
by the internal-beta definition.

The final compact machine-readable record is
[`internal-beta-receipt.json`](internal-beta-receipt.json). It binds the green
smoke and targeted gates, retained red harness history and external boundaries.
The closure commit contains only owned Mobile test corrections and evidence;
it makes no Desktop acceptance claim.

Remaining non-blocking external gates are a compatible physical iPad,
Production CloudKit and a Mac/iPhone/iPad domain roundtrip, public TestFlight
Beta Review/link, Apple's default-browser grant and a post-grant build, and
public Store release approval. Hardware Escape in the focused simulator address
sheet remains an accepted beta limitation because the visible Cancel action
works. The full accessibility/performance/download-recovery matrix and mobile
uBlock feasibility do not reopen this beta wave.

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

A read-only `devicectl` inventory at 2026-09-04 22:57 CEST found
`AhoiBrowser 0.1 (9)` with `builtByDeveloper=true` on `Servusla` and found no
installed `com.apple.TestFlight`. The minimized, privacy-preserving result is
retained as the previous observation in `device-readback.json`; the full
installed-app inventory is not retained. The current 2026-09-05 07:15 CEST
readback confirms TestFlight `4.3.1 (681.1)` is now installed, with
`builtByDeveloper=false`. AhoiBrowser is still developer Build 9 while the
first-launch terms await user approval. This does not establish a physical
TestFlight Build 10 install.

This receipt proves archive, export, server validation, upload, processing and
internal TestFlight distribution. It does not claim a physical TestFlight
installation, a Production CloudKit Mac-iPhone-iPad roundtrip, external Beta
Review, a public TestFlight link, the managed default-browser grant, or a
post-grant build.
