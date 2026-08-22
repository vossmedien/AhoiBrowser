# Implementation status

Last updated: 2026-08-23

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
| Branded native browser | development build complete; release build not run | `out/AhoiDev/AhoiBrowser.app` builds and launches as native ARM64 Chromium; the fifteen-patch product stack composes deterministically and the latest incremental `chrome` gate exited 0 |
| Native visible product slice | `PROGRAMMATIC_PASS`; development-runtime interaction observed; installed proof pending | profile-backed sidebar with saved/temporary sections, fixed New Tab/Downloads/History/Settings dock, group styling/actions and searchable recent-link hover card, direct centered five-result command bar, URL-copy action, split-aware paired rows and row/column layouts; the incremental build and runtime restart journey pass while native pointer drag/resize still require manual dogfood confirmation |
| Persistent nested tab tree | `PROGRAMMATIC_PASS`; development restart proof observed; installed proof pending | profile-local SQLite snapshots, tombstones, undo history, icon/accent schema migration and nested grouping remain authoritative; runtime acceptance covered group customization, duplication plus `Cmd+Z`, collapse, recent-link activation and persisted stacked split membership/orientation |
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
That baseline now builds and runs. The deterministic fifteen-patch source stack now
also contains the first connected product UI: the profile-backed tree/session
bridge, native sidebar, local command bar and bounded native split projection.
The regular Chromium profile remains the persistent authority, and restored
tabs can rebind saved pages at any folder depth without flattening their
hierarchy. Disk access is isolated from Chromium's UI thread: a validated
in-memory database serves runtime queries, while full SQLite snapshots
including durable undo history are loaded and written on a sequenced blocking
runner. A fresh-profile HTTPS runtime smoke remained alive through navigation
updates, produced a valid tree database and no crash report, then exited
cleanly. A later isolated development-profile interaction used native mouse and
keyboard input to verify temporary-to-saved drag, saved-split-to-temporary
roundtrip, both tab context menus, favicons, the five-result Cmd+T surface and
its previously crashing Enter path. The exact process remained alive without a
crash report; an `about:blank` idle sample with four test tabs measured about
0.2% aggregate CPU. These features are `IMPLEMENTED` and have focused
`PROGRAMMATIC_PASS` plus development-runtime observation; `out/AhoiDev` is not
an installed artifact and therefore does not count as `INSTALLED_PASS` or
release `CU_E2E_PASS`. Packaging, `/Applications` installation,
signing/notarization binding and the broader daily-driver matrix remain open.
