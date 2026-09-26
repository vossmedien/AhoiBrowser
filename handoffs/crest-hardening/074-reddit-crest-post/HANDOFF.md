# 074 – Crest user post: what Ahoi already has, what to plan

Status: ready (product decisions for desktop and mobile owners)
Owner lane: desktop, mobile (shared sync for item 2)
Source: [SOURCE.md](SOURCE.md) (r/CrestBrowser, "Two features and a question",
26 Sep 2026, no comments yet). The user asked for a comparison and a plan.
Captured by Codex in the user's logged-in Chrome; this lane checked the
code at `84e5ff4`.

| Request in the post | Ahoi today | Proposal |
| --- | --- | --- |
| Mobile gestures for page history back/forward | **Present.** `MobileWebPageView.swift:36` and `MobileLinkPreview.swift:190` enable `webViewBackForwardNavigationGestures(.enabled)` (WebKit edge swipe). | Nothing to build. Name the edge swipe in the first-run tips. Don't add two-finger history gestures: iOS reserves multi-finger swipes (three fingers: undo/redo, text editing) and VoiceOver uses them. |
| Mobile gesture for switching tabs ("three fingers") | **Partly present.** A horizontal drag on the Harbor deck switches **Workspaces** (`MobileHarborDeckView.swift:148`). The mobile contract (`AhoiBrowser-Mobile-Zielprompt.md` "Schneller Tabwechsel") also asks for a horizontal flick through recent **tabs** that doesn't block WebKit's back/forward swipe; no code for that yet. | Mobile: implement the contract's recent-tabs flick on the bottom bar (one finger, horizontal, outside the web view so the edge swipe keeps working), with a VoiceOver action. |
| Move folders of tabs between spaces | **Present on both platforms.** Desktop: the sidebar context menu's "Move to" is built for every node, folders included (`browser_sidebar_host_context_menu.cc:509`, `BuildMoveToMenu`), and drag and drop works between Workspaces (master contract, tree section; test `SPLIT-06`). Mobile: tree moves carry a target Workspace (`CompanionTreeMoveTarget.workspaceID`, `WorkspaceDetailView.onMoveNode`). Into a `Vollständig getrennt` Workspace, the URLs are reopened with the logins note (`WS-ISO-05`). | No new feature. Check discoverability: the Crest user didn't find it there, so check it in Ahoi's onboarding and the command bar ("Move folder to Workspace …" as a command, `CMD-04`). |
| Merge Workspaces | **Missing.** Only the importer knows "zusammenführen" as a conflict strategy for same-named Workspaces (master contract, import section). | New feature, see the sketch below. Needs a product decision first. |
| Extensions on mobile, like Safari | **Excluded by contract**: "Mobile erhält … keine Chromium-Extensions" (`AhoiBrowser-Mobile-Zielprompt.md`). WebKit's `WKWebExtension` (iOS 18.4+) would allow Safari-style Web Extensions in a WKWebView app. | User/owner decision. If wanted: a time-boxed spike (load one MV3 extension, e.g. a content blocker, into `WKWebExtensionController`; check the App Review, privacy manifest and update path), then a contract change. Until then, the mobile content blocker stays the answer. |

## Sketch: "Merge Workspaces" (if accepted)

- Action on a Workspace: "Zusammenführen mit …", with the target picked from a list. The
  source's top-level nodes move into the target in their order, as one
  folder named after the source (default) or flat (option). Pins, folders,
  saved pages and splits are kept. The source is then deleted through the
  normal deletion path (R6 re-homing does not apply, since it's already empty).
- One structure transaction under the single-writer rule (H2): one
  commit and one epoch, with undo that restores the source Workspace with its IDs.
- Isolation (ADR 0011): a merge between a shared and a `Vollständig
  getrennt` Workspace can't carry logins. Either refuse it, or reopen the URLs in
  the target with the `WS-ISO-05` note. Two separated Workspaces never
  merge their profiles.
- Sync: a merge is a node move plus a Workspace tombstone. The existing merge
  vectors (H1) cover moves; add one vector for "source deleted while
  a peer adds a node to it". That node must land in the target, not be lost.
- Tests: unit test for the transaction and its undo; journeys desktop and mobile;
  a cross-device sync case.

## Suggested order

1. Mobile recent-tabs flick (already in the contract, smallest).
2. Make folder moves discoverable (command bar entry).
3. Merge Workspaces after the product decision.
4. Mobile Web Extensions spike only after a contract change.
