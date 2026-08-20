# Implementation status

Last updated: 2026-08-20

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
| Branded native browser | in progress | two compile passes completed 13,650 Ninja edges before exposing the profile-level deployment-target mismatch; profiles now keep Chromium's compile target at 13 while the app remains macOS-26-only via `LSMinimumSystemVersion`; full `chrome` rebuild pending |
| Installed signed dogfood | not run | depends on branded build |
| Signed release provenance | not implemented / fail-closed | Computer Use PASS disabled by `config/release-evidence.json` until build-sign-package-install binding exists |
| HTTP-auth fixture spike | fixture complete | 9 local integration tests; browser AUTH gates remain not run |
| CloudKit model spike | local model complete | verified Swift suite in `docs/spikes/CLOUDKIT.md`; real entitled Mac-iOS CloudKit roundtrip remains not run |
| Extension fixtures | fixture complete | local MV3 probe plus non-allowlisted MV2 negative control; installed-browser/CWS tests remain not run |
| Glass integration spike | not run | depends on native browser integration |

The checkout completed with 233 expected and 233 actual dependencies and a
machine-readable dependency artifact. More than 200 GiB remained available at
the completion check, above the 150 GiB build threshold. No Chromium build is
claimed until the separate control and branded build gates finish.

The first two branded compile passes exposed deprecation errors in separate
Chromium subsystems because the initial profiles incorrectly coupled the
product launch requirement to the compiler deployment target. Chromium's own
M151 build contract distinguishes these values: the upstream core remains at
`mac_deployment_target = "13.0"`, while
`mac_min_system_version = "26.0"` writes AhoiBrowser's real macOS-26 launch
requirement. The temporary source workaround was removed instead of collecting
one-off deprecation patches across Chromium. Generated actions remain reusable,
but compiler command changes intentionally invalidate affected objects. This
is not yet a complete app-build or runtime claim.
