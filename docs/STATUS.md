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
| Branded native browser | not run | 26.6 compatibility dev path may proceed before the exact 26.5 control; release remains blocked on the control |
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
