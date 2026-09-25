# 006 – Group before-unload for multi-tab closes, archive and popup overlays (H5, H2)

Status: ready
Owner lane: desktop
Base: `f811604`

## Findings (source reading, not yet observed)

1. **Archive** (`overlay/chromium/src/ahoi/browser/session/workspace_structure_controller_archive.cc:233-271`)
   writes the archive entry first, then closes the tabs one by one with
   `CLOSE_NONE`. It relies on the earlier `CanArchiveTab` check. A page that
   adds a before-unload handler after that check can veto the close, which
   leaves an archive entry for a tab that is still open (two writers of the
   same fact).
2. **"Alle temporären Tabs schließen"**
   (`ui/sidebar/browser_sidebar_host_runtime_actions.cc:645-669`) closes tabs
   one by one. A veto from the second tab does not stop tabs one and three
   from closing.
3. **Popup overlays** are not in the tab strip. `PopupOverlayService::RequestClose`
   calls `ClosePage` (`popup/popup_overlay_service.cc:75-80`), but when the
   service is destroyed (`:20`) the overlay's page is discarded without
   before-unload. Quitting or closing the window could therefore skip the
   page's prompt.

Crest's reference (`crest_chrome_host.mm:2070-2112`) asks every page of a group
first. It closes nothing unless all pages agree, and rejects the close if a
page navigated or disappeared in between.

## Requested change

- One group-close helper used by archive, "close all temporary tabs",
  Workspace deletion (handoff 003) and closing a split. It asks all pages
  first, commits the semantic change (archive entry, tree update) only after
  every page agreed, then closes. A veto leaves every tab and every record
  unchanged.
- Window close and quit include open popup overlays in Chromium's unload
  handling, or close them with `ClosePage` first.

## Acceptance cases

- **CLOSE-GRP-01**: Three temporary tabs, the second with a before-unload
  handler. Run "close all temporary tabs" and cancel. All three stay open.
- **CLOSE-GRP-02**: Archive a split where one pane adds a before-unload handler
  after eligibility, then cancel. There is no archive entry, and both panes
  stay open.
- **CLOSE-GRP-03**: A popup overlay with a before-unload handler, then ⌘Q.
  The prompt appears, and cancelling keeps the overlay and the window.
- **CLOSE-GRP-04**: Group close without any handler behaves as today.
