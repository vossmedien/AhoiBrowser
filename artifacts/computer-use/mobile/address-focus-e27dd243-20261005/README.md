# Mobile address focus fix e27dd243 — 5 October 2026

Own simulator A058D204 (iOS 27.0, headless), MacbookPro2026.local, DebugLocal.

| Candidate | Run | Result |
| --- | --- | --- |
| cc8bef2b | plain launch / launch with the four `AHOI_MOBILE_E2E_*` variables | Home renders, main thread idle (`plain-launch-15s.png`, `env-launch-12s.png`) |
| cc8bef2b | Home journey, fresh device | FAIL: "address retry lost input after prefix ht. Actual value: h." |
| cc8bef2b | Home journey, keyboard-onboarded device | same FAIL (`focus-loss-frames-cc8bef2b.png`: keyboard drops, sheet falls to medium detent after the first character) |
| e27dd243 | Home journey `testSavedPageHomeAddressReturnSetAndRestore` | **PASS** (272.8 s, host load 300–500 from foreign work) |
| e27dd243 | `testUnsafeSchemeIsExplainedAndRejected`, `testLibraryClosesWithDoneAfterCreatingWorkspace` | PASS |
| e27dd243 | `testHardwareEscapeDismissesFocusedAddressPresentationWhenXCUIDeliversIt` | FAIL at the focus precondition (line 117) |
| cc8bef2b | same Escape test (baseline) | identical FAIL: pre-existing, not caused by e27dd243; retest at normal host load |

Candidate receipt `candidate-receipt.json` (clean source e27dd243, binary
sha256 `211d1332…`, receipt sha256 `de64485b…`). Exact xctestrun adds the
receipt as `AHOI_MOBILE_CANDIDATE_RECEIPT_BASE64`. Earlier 4 October white-
launch hang ran while the Mac console was locked; xcodebuild's post-failure
wait was `simctl diagnose` (600 s), avoided with `-collect-test-diagnostics never`.

## Follow-up on 5bf8a565 (test-only change)

`5bf8a565` makes the Escape test's focus precondition accept
`hasKeyboardFocus` (UIFocusSystem `hasFocus` is false for an iPhone text
field without hardware-keyboard focus navigation). Clean candidate
`5bf8a565` (receipt `candidate-receipt-5bf8a565.json`):

| Test | Result |
| --- | --- |
| Home journey | **PASS** (266.7 s) |
| Escape on address sheet | focus precondition now passes; FAIL: sheet not dismissed (line 122) |
| same corrected test against the cc8bef2b app and receipt | identical FAIL: pre-existing |
| Escape on tab sheet (`onKeyPress(.escape)`, independent path) | FAIL (line 143) |

Two independent handlers (UIKeyCommand in the responder chain and SwiftUI
`onKeyPress`) fail the same way, the recordings show the software
keyboard, so XCUI's Escape key event is most likely not delivered on this
headless iOS 27 simulator without a connected hardware keyboard. The shared
Simulator.app `ConnectHardwareKeyboard` preference is not changed (it also
affects foreign simulators). Hardware Escape stays OPEN for an iPad/iPhone
with a real keyboard.
