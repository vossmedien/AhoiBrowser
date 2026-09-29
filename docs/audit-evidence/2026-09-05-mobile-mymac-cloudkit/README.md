# Single My-Mac CloudKit attempt — 2026-09-05

Result: **TEST NOT EXECUTED — destination-selection error**, not a CloudKit,
Chromium, provisioning or product-failure verdict. The renewed explicit runtime
window allowed one focused attempt and was returned after that attempt. No
automatic retry, rebuild, re-signing or installation was performed by Mobile.

## Exact input and preflight

- Intended test:
  `AhoiMobileCloudKitE2ETests/testRealContainerTwoLogicalDevicesMergePagesTabsAndDeletion`.
- Existing inert host:
  `/private/tmp/ahoi-mobile-21de889-cloudkit-real-e2e-derived/Build/Products/CloudKitDevelopment-iphoneos/AhoiMobile.app`.
- Host source `9395a9cf2851e2785eeba78afff4902c5d8f3a62`, version `0.1 (11)`,
  bundle `app.ahoibrowser.AhoiBrowser`, built for iPhoneOS 26.5.
- Deep strict code-signature verification passed. Actual authority:
  `Apple Development: Christian Voss (2265UJB5KF)`; Team `248AJ5BN47`.
  Signed CloudKit environment was exactly `Development`, container exactly
  `iCloud.app.ahoibrowser.AhoiBrowser`; APNs was development.
- Runner:
  `/private/tmp/ahoi-mobile-21de889-cloudkit-real-e2e-derived/Build/Products/AhoiMobile-CloudKitE2E_iphoneos26.5-arm64.xctestrun`.
  Its `AHOI_CLOUDKIT_E2E_HOST_MODE=1` and exact hosted-test path were verified.
- Prepared token `9C8AAE2C-E9C0-435F-B48F-7141E60FD038`; intended zone
  `AhoiBrowserCloudKitE2E.9c8aae2ce9c0435fb48f7141e60fd038`.
  The host guard would probe zone/subscription freshness before provider
  construction. That guard was not reached in this attempt.
- Fresh process checks immediately before starting showed no native
  `/Applications/AhoiBrowser.app`/helpers, Chromium build or competing compiler
  workload. No existing Desktop, payload/command key, profile or account was
  modified. A read-only destination query offered My Mac with the intended
  `Designed for [iPad,iPhone]` variant.

## Actual invocation and cause

The command used `test-without-building`, the exact existing runner above,
parallel testing disabled and only the one named test. Its destination was
`platform=macOS,arch=arm64,id=00006021-001C31491E87401E`.

**The agent omitted the variant from the actual invocation.** The result log
explicitly lists four matching destinations and says it picked the first:
native macOS, followed by Mac Catalyst, DriverKit and Designed for iPad/iPhone.
The native macOS launcher could not execute the iOS host:
`IDELaunchErrorDomain 20`, LaunchServices `-10661`,
`kLSExecutableIncorrectFormat`. The log records `sdk_variant=macos`.

This is a concrete invocation mistake, not evidence that the correctly selected
Designed-for-iPad/iPhone target or CloudKit is unavailable. No second invocation
was made under the one-attempt authorization. A future attempt must select the
exact variant explicitly and obtain a new runtime window first.

`xcodebuild` PID `88055`, tool session `84564`, ended with exit code **65**.
The result summary reports one launcher/infrastructure failure, zero passes and
zero skips. The named domain test never started; this is not one failed domain
assertion and not a zero-step success.

## Cleanup and handback

Post-run process inventory found no exact host, runner, `AhoiMobile` or `xctest`
process remaining. `xcodebuild` was terminal; no signal/kill was needed and no
bundle-name-wide stop was used. The app failed before test execution, so its
test-zone/local-store creation and authenticated cleanup code never ran.
There was no test-created zone requiring deletion from this attempt. The server
was not independently queried afterward, and global absence is not claimed.

Host and test-bundle executable SHA-256 values were unchanged before/after:

| Executable | Plain SHA-256 |
| --- | --- |
| Mobile inert host | `702638bea83b03da022ec08da8657d93c5287f94b7cbf7c053e40168be69bdbf` |
| Hosted test bundle | `2ee6a00abfe9a15b698f6cbed359f787f24cc2625aa54649043a23945ad2759e` |
| Installed Desktop `3d413ef` | `ab4d0a7664fb8ec871391be1130ee002e79ef8bc4084ff49877d4042e387aa99` |

The Desktop checksum matches its previously recorded installed candidate.
No `/Applications` write, Desktop profile access, key mutation, Apple prompt
acceptance, Production action or shared-checkout operation was performed.
The runtime was explicitly returned to the Desktop owner through `codex queue`;
handback message `01a07125-0523-76a1-98d6-5faa8eb1dd53`. Mobile retains no
UI/build/install reservation while recording this evidence.

## Artifacts and acceptance boundary

Original artifacts:
`/private/tmp/ahoi-mobile-mymac-cloudkit.buxw9Z/domain-mymac.{log,xcresult}`.
Canonical retained copies:
`artifacts/e2e/mobile-mymac-cloudkit-9395a9c/`.
Original and retained log SHA-256 match:
`726feed1822945cbb9b771b610bfb994cfbd26eec9323bd97365d88f6f3de6c3`.
The original and retained xcresult tree hashes were also compared and matched.

The pending test would exercise the real Development service through two
Mobile-domain repositories and CKSyncEngines with a run-local key and simulated
Mac identity. It would still not establish Chromium execution, cross-device
Keychain bootstrap, Bookmark-v2 behavior or Production Mac–iPhone–iPad sync.
Those scopes remain explicitly open.
