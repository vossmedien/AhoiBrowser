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
