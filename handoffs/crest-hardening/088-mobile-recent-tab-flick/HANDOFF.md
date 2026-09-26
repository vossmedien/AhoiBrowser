# 088 – Mobile: flick through recently used tabs (ADR 0012, section 3)

Status: ready
Owner lane: mobile (apply, regenerate the project, build, test)
Base: HEAD `2543785` (`git apply --check` passes).
Implements `MOB-FLICK-01/02`. Written by the crest-hardening lane; not built.

## Change (`088-mobile-recent-tab-flick.patch`)

- New `MobileRecentTabCycler` (`MobileRecentTabCycler.swift`, pure value type):
  - Candidates share the selected tab's mode; normal tabs also share its
    Workspace. They are ordered by `lastActiveAt`, newest first, with ties broken by ID;
    the selected tab is at index 0.
  - `step(+1)` goes to the previously used (older) tab and `step(-1)` back toward
    newer ones; both directions wrap. There is no cycler when no other candidate exists.
  - The order is captured when a flick sequence starts. `select()` refreshes
    `lastActiveAt`, so without that snapshot the order would reshuffle after every flick.
  - `isContinuation` keeps the snapshot while the next flick comes within 2 s,
    the selection is still the tab the cycler chose, and no candidate was opened
    or closed. Otherwise a fresh sequence starts from the current MRU order.
- `MobileBrowserController.switchRecentTab(direction:now:)`, in the same file as
  an extension, plus one stored `recentTabCycler` property in the controller.
- `MobileHarborDeckView`, on the address control of the control row only:
  - A `simultaneousGesture` with the rail's thresholds (≥ 72 pt, horizontal
    > vertical × 1.35, minimum distance 28), so taps still open the address
    sheet. Swiping right shows the previously used tab, as in Safari.
  - `.sensoryFeedback(.selection)` on every committed switch.
  - VoiceOver actions reuse the existing strings `browser.tabs.previous` and
    `browser.tabs.next` (de: "Vorheriger Tab" / "Nächster Tab"), so the
    `.xcstrings` file needs no change.
  - Nothing happens with one visible tab.
  - The web view is untouched, so WebKit's back/forward edge swipe stays. The
    Workspace swipe on the top rail stays; the control row is also visible when
    the deck is collapsed.
- `AhoiMobileBrowserView` passes `onSwitchRecentTab`. That file grows to 797
  lines (budget 800).
- The keyboard `switchTab` command keeps its list-order behavior (the patch doesn't touch it).

Not done: a neighbor-page preview during the drag. It needs a snapshot of an
inactive `WebPage` (pages beyond five are discarded) and is not trivial; the
switch commits at the end of the gesture.

## Project file

`project.yml` includes `Sources/AhoiMobileCore` and `Tests/AhoiMobileCoreTests`
by folder, but the checked-in `AhoiMobile.xcodeproj/project.pbxproj` lists
files explicitly. Run `xcodegen generate --spec project.yml` (docs/RELEASING.md)
after applying, so the two new files are in the project. The Swift package
(`Package.swift`) picks them up by itself.

## Tests

- New `MobileRecentTabCyclerTests` (6): MRU order and wrap; order fixed while
  flicking; new sequence after a pause, another selection or a closed tab; no
  flick with one tab; private and normal kept apart; Workspace filter; the
  controller flicks back to the previously used tab and returns.
- Checked by this lane: `AhoiMobileCore` (with `-D DEBUG`) and the full
  `AhoiMobileCoreTests` directory typecheck against the iOS 26 simulator SDK
  (`swiftc -emit-module -enable-testing` / `-typecheck`, Swift 6). Not run.
- Visible, for the owner:
  - `MOB-FLICK-01`: with three tabs in one Workspace, swipe right on the
    address field to reach the last used tab, then again to the one before it; swipe left
    to go back. An edge swipe over the page still goes back in history. A tap on
    the address field still opens the address sheet.
  - `MOB-FLICK-02`: the VoiceOver actions "Vorheriger Tab" and "Nächster Tab"
    work, and nothing happens with a single tab.
