# ADR 0012 visible checks on iOS — 30 September 2026

Candidate: source `9e493762` (branch `codex/desktop-core-feature-wave-20260830`),
scheme `AhoiMobile` (DebugLocal), Xcode 27.0 (27A266a), own simulator
"Ahoi E2E iPhone 17 ADR12" (`5741180E-A297-4A2E-8552-E873A6767F22`,
iPhone 17, iOS 27.0 24A434). Built with `-jobs 4` into
`.work/mobile-adr12-e2e/DerivedData`; result bundle
`.work/mobile-adr12-e2e/results/final.xcresult` (local, not committed).
No physical device was used.

Final run: 8 tests, 7 passed, 1 expected failure, 0 failures.
`AhoiMobileCoreTests` on the same build: 387 tests, 0 failures, 2 skipped.

| ID | Result | Test |
| --- | --- | --- |
| MOB-FLICK-01 bar flick | PASS | `MobileRecentTabFlickUITests.testFlickSwitchesToLastUsedTabAndBackKeepingPageHistory` |
| MOB-FLICK-01 preview during drag | PASS | `testDragShowsNeighborPreviewBeforeCommit` |
| MOB-FLICK-01 page history untouched | PASS (Back button) | same as bar flick, step 5 |
| MOB-FLICK-01 web view edge swipe back | OPEN | `testWebViewEdgeSwipeGoesBackInHistory` (expected failure) |
| MOB-FLICK-02 no flick with one tab | PASS | `testNoFlickInAWorkspaceWithOneTab` |
| MOB-FLICK-02 VoiceOver actions | PARTIAL | `testVoiceOverActionsSwitchTabs` |
| WS-MERGE-07 merge + one-level undo | PASS | `MobileWorkspaceMergeUITests` |
| MOB-EXT-01 spike | PASS | `MobileWebExtensionSpikeUITests` (2 tests) |

## What each check shows

- Flick: T1 selected with use order T1, T0, T2. A rightward flick on the
  address control goes to T0 (last used), after the 2 s sequence window
  the same flick returns to T1, two quick flicks walk the captured order
  T1 → T0 → T2, and right, right, left inside one sequence ends on T0
  (re-sorting would end on T1). Tab count stays 3.
- Preview: a 50 pt drag shows `browser.tabs.flick-preview` ("Ahoi Scale 2",
  the tab the flick would reach) while the finger is down, the preview
  goes away on release, and the short drag keeps the current tab.
- History: after all flicks T0 is still on page B, Back reaches page A and
  Forward becomes enabled.
- Edge swipe (OPEN): XCUI's left-edge drag pops a pushed library
  NavigationStack in the same app, but over the web view WebKit only
  starts its back swipe and cancels it; Back works. Whether a real finger
  completes it needs a physical-device check (the owner's iPhone was not
  touched).
- One tab: flicks in both directions change nothing and do not open the
  address editor.
- VoiceOver (via `XCUIDevice.voiceOverService`, iOS 27): VoiceOver reaches
  "Adresse und Suche", and its swipe down walks the actions
  "Nächster Tab", "Vorheriger Tab", "Aktivieren (Standard)"
  (`screenshots/flick-02-voiceover-*.txt`). Performing the chosen action
  was not possible from XCUI: synthesized taps and the VO+Space key pass
  to the page instead of VoiceOver (`flick-02-voiceover-activation.txt`).
  The action handler is the flick's `switchRecentTab`, unit-tested.
- Merge: Workspaces A and B created in the library, a public page saved to
  A from the browser. "Zusammenführen mit …" → B asks ("1 Seiten und 0
  Ordner …"), "Als Ordner zusammenführen" removes A, B shows folder "A"
  with the page, the open tab's device row moves to B, and the undo banner
  appears. Undo restores A with the page, B is empty again, the tab row is
  back in A.
- Spike: with `-AhoiWebExtensionSpike` the bundled MV3 extension's content
  script writes `content-script=ran`, a same-origin control script loads,
  the script whose URL matches the `declarativeNetRequest` rule is blocked
  (`rule=blocked`), and `storage.local` counts across page loads. Without
  the argument no report appears. The report exists only on pages with
  `?ahoi-spike-check` (fixture change in `WebExtensionSpike/content.js`).
  Popup/action UI, Files import and permission prompts were not driven.

## Product fixes found by the journeys (commit `9e493762`)

1. Every flick on the address control also opened the address editor (the
   Button's tap fired on release inside the control). The flick drag is now
   a high-priority gesture.
2. The ADR's neighbor preview during the drag was missing; added with unit
   tests (`MobileRecentTabCyclerTests`).

Screenshots and VoiceOver transcripts are in `screenshots/` (local only,
not committed).
