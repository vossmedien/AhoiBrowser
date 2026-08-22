# Implementation status

Last updated: 2026-08-22

## Verified local environment

- Apple M2 Max, ARM64, 32 GiB RAM
- macOS 26.6 (25G72)
- Installed development lane: Xcode 26.6 (17F113), macOS SDK 26.5 (25F70), iOS SDK 26.5 (23F81a); accepted for `ahoi-dev` only
- Installed M151 baseline: Xcode 26.5 (17F42), macOS SDK 26.5 (25F70), iOS SDK 26.5 (23F73)
- Valid Apple Development and Developer ID Application identities available
- Paired iPhone 16 Pro Max available for companion-device tests
- Google Chrome, Arc, and 1Password installed for migration/compatibility checks

## Phase 0 progress

| Deliverable | State | Evidence |
| --- | --- | --- |
| Product contract | complete | `outputs/AhoiBrowser-Master-Zielprompt.md` |
| Host inventory | observed, not captured | local command observation; publishable machine-readable manifest pending |
| Overlay repository foundation | complete | repository contracts and fail-closed provenance gates |
| Exact Chromium Stable pin | complete | 151.0.7922.170, fully rolled/pinnable Mac ARM64 Stable |
| Chromium ARM64 checkout | complete | 233/233 dependency closure verified twice at `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae` |
| Unmodified Chromium ARM64 build | not run | exact Xcode 26.5 baseline is installed; build gate is ready to run |
| Branded native browser | development build complete; release build not run | `out/AhoiDev/AhoiBrowser.app` builds and launches as native ARM64 Chromium; the twelve-patch product stack composes deterministically and the latest incremental `chrome` gate exited 0 |
| Native visible product slice | `PROGRAMMATIC_PASS`; installed proof pending | profile-backed sidebar with shared semantic colors/radii, compact workspace control, live tabs, nested folders, single-click selection/collapse, context menus, drag feedback, two-/three-pane split projection and a centered command bar with padded input, URLs and favicons; 7 focused sidebar plus 15 command-bar tests pass |
| Persistent nested tab tree | `PROGRAMMATIC_PASS`; installed restart proof pending | the live UI now uses an in-memory SQLite tree while complete profile-local snapshots, tombstones and undo history load/save on a dedicated `MayBlock` sequence; nested URL rebinding and saved-page command indexing are covered by 12 tab-tree plus 7 session tests |
| Installed signed dogfood | not run | depends on branded build |
| Signed release provenance | not implemented / fail-closed | Computer Use PASS disabled by `config/release-evidence.json` until build-sign-package-install binding exists |
| HTTP-auth fixture spike | fixture complete | 11 local integration tests; browser AUTH gates remain not run |
| CloudKit model spike | local model complete | 34 verified Swift tests in `docs/spikes/CLOUDKIT.md`; real entitled Mac-iOS CloudKit roundtrip remains not run |
| Extension fixtures | fixture complete | local MV3 probe plus non-allowlisted MV2 negative control; installed-browser/CWS tests remain not run |
| Glass integration spike | not run | depends on native browser integration |

The checkout completed with 233 expected and 233 actual dependencies and a
machine-readable dependency artifact. The first branded AhoiDev baseline is a
real native ARM64 application bundle and passed build, bundle, overlay,
toolchain and runtime-start checks. It is a component-build development
artifact and is therefore run in place, never copied to `/Applications` or used
as release evidence.

The first two branded compile passes exposed deprecation errors because the
initial profiles coupled the product launch requirement to the compiler target.
The corrected Chromium contract keeps `mac_deployment_target = "13.0"` while
`mac_min_system_version = "26.0"` writes AhoiBrowser's real launch requirement.
That baseline now builds and runs. The deterministic source patch stack now
also contains the first connected product UI: the profile-backed tree/session
bridge, native sidebar, local command bar and bounded native split projection.
The regular Chromium profile remains the persistent authority, and restored
tabs can rebind saved pages at any folder depth without flattening their
hierarchy. Disk access is isolated from Chromium's UI thread: a validated
in-memory database serves runtime queries, while full SQLite snapshots
including durable undo history are loaded and written on a sequenced blocking
runner. A fresh-profile HTTPS runtime smoke
remained alive through navigation updates, produced a valid tree database and
no crash report, then exited cleanly. These features are
`IMPLEMENTED` and have focused `PROGRAMMATIC_PASS` evidence; development-runtime
observations from `out/AhoiDev` do not count as `INSTALLED_PASS` or release
`CU_E2E_PASS`. A bounded Computer Use attempt could not reliably target the
newly linked isolated window and was stopped; it is deliberately not counted
as pass evidence. Packaging, `/Applications`
installation, signing/notarization binding and the broader daily-driver matrix
remain open.
