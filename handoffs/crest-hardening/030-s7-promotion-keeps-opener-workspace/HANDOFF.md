# 030 – S7: promoted popups join the opener's Workspace; Quick Window never mixes sessions

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `3a9fb13` (`git apply --check` passes). Implementation of 011 S7
by the crest-hardening lane; not compiled by the lane.

## Change

- **Popup overlay** (`ui/popup/popup_overlay_controller.cc`): before
  `PromotePopupToTab` and `SplitPopupWithOpener` insert the popup,
  `SelectOpenerWorkspace` selects the opener tab's Workspace in the window.
  The popup keeps the opener's website session (ADR 0011), and
  `TrackRuntimeTab` now binds the new tab to that same Workspace instead of
  whichever Workspace the window shows.
- **Quick Window** (`command_bar/quick_window_chromium.cc`): Quick Windows
  always run in the Profile's shared website session. The routing core
  already enforces this. When `MoveActiveTabToNormalWindow` targets a
  window whose active Workspace has its own website sessions, the page is
  reopened there (`OpenGURL`, loads in that Workspace's session) and the
  Quick Window tab is closed. It is not moved with the shared session. Shared
  Workspaces keep the existing move, which preserves the WebContents.

## Tests

- New browser test
  `QuickWindowWebsiteSessionBrowserTest.ReopensInsteadOfMovingIntoOwnWebsiteSessionWorkspace`:
  the reopened tab is a new WebContents with the same URL and is bound to the
  own-session Workspace. The existing `MovesExactPageIntoNormalWindowAndClosesPopup`
  still covers the shared case.
- Popup promotion: covered by the visible journey (no popup browser-test
  fixture exists). From a page in Workspace B, open a popup overlay, switch
  to Workspace A through the keyboard, then promote. The tab appears in B,
  B is selected, and it is logged in with B's account.

## Note

The reopened Quick Window page loses its in-page state (form input, scroll).
That follows ADR 0011's rule that sessions never cross levels.
