# AhoiBrowser Mobile E2E evidence

Candidate: `codex/ahoi-mobile-browser` based on `319bff3`  
Simulator runtime: iOS/iPadOS 26.5  
Date: 2026-08-28

## Test-order contract

The mobile implementation was exercised through visible Simulator/Computer
Use journeys before the corresponding XCTest gates. Two defects found during
this sequence demonstrate the enforced loop:

1. website tint remained on its blue fallback because WebKit's asynchronous
   JavaScript body required an explicit `return`; the same orange fixture was
   visibly repeated after the fix;
2. decorative tint overlays intercepted coordinate taps on the address and
   more controls; the affected coordinate journeys were repeated visibly
   before the UI suite was rerun.

Source compilation is only a candidate-construction step. It is not counted as
an E2E or release result.

## Simulator user journeys

| Journey | Simulator status | Evidence and boundary |
| --- | --- | --- |
| `MOB-USER-01` cold start, URL, HTTPS | `PASS_SIMULATOR` | local deterministic fixture and real `example.com`; `iphone-adaptive-orange-tint.png` and `iphone-adaptive-tint-transition.png` |
| `MOB-USER-02` search, results, navigation | `PASS_SIMULATOR` | DuckDuckGo search plus back/forward/reload; `iphone-web-search.png` |
| `MOB-USER-03` tabs, close, undo, restore | `PASS_SIMULATOR` | close/undo and process relaunch restore; `iphone-tabs-close-undo.png`, `iphone-session-restored.png` |
| `MOB-USER-04` workspace save/move | `PASS_SIMULATOR` | Example is both a permanent saved page and a normal open device tab in `Recherche`; `iphone-workspace-example-saved-page.png` |
| `MOB-USER-05` private separation | `PASS_SIMULATOR` | purple private chrome, no restore and no device-tab publication; `iphone-private-purple-polished.png`, `ipad-private-not-published.png` |
| `MOB-USER-06` external HTTP(S) | `PARTIAL` | internal cold/warm URL router and deduplication work; selecting an external HTTPS URL opens Safari because the unsigned app cannot be selected as default browser; `iphone-external-https-default-browser-boundary.png` |
| `MOB-USER-07` upload/download/share/popup | `PARTIAL` | download, share and `target=_blank` pass; upload input renders, but the simulator file picker did not open and requires physical-device repetition; `iphone-download-completed.png` |
| `MOB-USER-08` permission/external app | `PASS_SIMULATOR` | origin-scoped camera prompt and mail-app confirmation; `iphone-permission-origin.png`, `iphone-external-app-confirmation.png` |
| `MOB-USER-09` rotation/accessibility | `PARTIAL` | portrait/landscape, Dynamic Type, dark appearance and high contrast are visible; a complete VoiceOver rotor and Reduce Motion/Transparency hardware run remains open; `iphone-landscape-tabs.png`, `iphone-adaptive-tint-dark-high-contrast.png` |
| `MOB-USER-10` iPad | `PARTIAL` | native sidebar, adaptive tint and touch flows pass; full hardware keyboard and pointer matrix remains open; `ipad-adaptive-orange-sidebar.png` |
| `MOB-USER-11` failure/memory/restore | `PARTIAL` | offline retry, process relaunch restore and inactive-page discard path are covered; physical memory-pressure stress remains open |
| `MOB-USER-12` cross-device tabs | `PARTIAL` | local iPhone/iPad projection, icon, device, workspace, close and immediate reopen pass; entitled Mac-mobile CloudKit roundtrip and signed link-to-Mac remain `NOT_RUN`; `ipad-device-tab-closed.png`, `ipad-device-tab-restored-immediately.png`, `iphone-workspace-example-saved-and-published.png` |
| `MOB-USER-13` unsafe actions | `PASS_SIMULATOR` | unsafe scheme rejected with visible explanation and recoverable address field; `iphone-unsafe-scheme-rejected.png`, `iphone-address-clear-after-error.png` |
| `MOB-USER-14` 1/5/20 tabs | `PASS_SIMULATOR` | 20 real menu-created tabs without phantom rows; `iphone-twenty-tabs.png`, `iphone-twenty-tabs-switcher.png` |
| `MOB-USER-15` visual consistency | `PASS_SIMULATOR` | iPhone/iPad, normal/private, website-derived orange, fallback tint, light/dark and high contrast; `iphone-adaptive-orange-tint.png`, `ipad-adaptive-orange-sidebar.png` |

All paths above are relative to `artifacts/computer-use/mobile/`.

## Programmatic gates after visible acceptance

- Core XCTest: **48 executed, 0 failures, 2 expected skips**. The skipped
  `CKSyncEngine` tests require an entitled Apple test target. Result bundle:
  `/private/tmp/ahoi-mobile-derived/Logs/Test/Test-AhoiMobile-2026.08.28_00-55-37-+0200.xcresult`.
- First UI XCTest after the redesign: failed because decorative overlays
  intercepted taps. Result bundle retained as diagnostic evidence:
  `/private/tmp/ahoi-mobile-derived/Logs/Test/Test-AhoiMobile-2026.08.28_00-56-08-+0200.xcresult`.
- Affected visible address/menu/private flows were repeated after the fix.
- Final UI XCTest: **3 executed, 0 failures**. Result bundle:
  `/private/tmp/ahoi-mobile-derived/Logs/Test/Test-AhoiMobile-2026.08.28_00-58-21-+0200.xcresult`.

The result bundles are machine-local and intentionally not described as
device, CloudKit or TestFlight evidence.

## Adaptive website tint and visual concept evidence

The app samples `meta[name=theme-color]` first, then selected computed style
candidates. Transparent, near-white, near-black and low-saturation colors are
discarded. Only browser chrome is tinted; website pixels are never recolored.
The sampled ARGB value is local session metadata and is absent from sync wire
records. Private tabs always use their own purple treatment.

ImageGen concepts are retained under `artifacts/design-concepts/mobile/` as
design input, not runtime evidence. The shipped SwiftUI implementation uses a
smaller, native interpretation: subtle material tint, a floating rounded dock,
a nested address capsule and lightly tinted iPad sidebar/device rows.

## External release gates

The following are not satisfiable by an unsigned placeholder-bundle simulator
build and are not passed:

- all physical-device `IOS-01` through `IOS-15` assisted E2E journeys:
  `NOT_RUN`;
- Apple Managed Default Browser entitlement and default-browser selection:
  `BLOCKED_ENTITLEMENT`;
- production App ID, Team ID, provisioning profile, concrete CloudKit
  container and shared Keychain keys: `BLOCKED_CREDENTIAL` / `BLOCKED_ENTITLEMENT`;
- signed Mac-iPhone/iPad CloudKit roundtrip, offline queue and conflict run:
  `NOT_RUN`;
- signed `.xcarchive`/`.ipa`, installed physical candidate and TestFlight
  processing/installation: `NOT_RUN`.

No `DEVICE_PASS`, `CLOUDKIT_E2E_PASS`, `ARCHIVE_PASS`, `TESTFLIGHT_PASS` or
`RELEASE_PASS` is claimed.
