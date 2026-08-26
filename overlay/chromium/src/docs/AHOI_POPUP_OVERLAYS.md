# AhoiBrowser web popup overlays

`PopupOverlayService` owns one real Chromium `WebContents` while it is shown as
an overlay. It does not create a replacement page, navigate during promotion,
or mirror web state into a DOM surface. Site isolation, renderer sandboxing,
storage partitions, extensions and the opener relationship therefore remain
Chromium-owned.

## Boundaries

- `//ahoi/browser/popup` owns eligibility, opener association, navigation
  observation and the `WebContents` ownership transfer contract.
- `//ahoi/browser/ui/popup` owns the scrim, origin row, external action rail,
  keyboard/VoiceOver state and browser adapters.
- `Browser::AddNewContents` is the only creation seam. Popups rejected by the
  conservative policy continue through Chromium's existing native window path.
- `BrowserView` supplies the existing `MultiContentsView` pane for the opener;
  the overlay tracks that pane's bounds while split dividers move. The opener
  viewport is not resized and no renderer-visible overlay is added.
- `TabStripModel` and `SplitTabCollection` remain the split source of truth.

## Ownership transfers

Promotion detaches the overlay host and inserts the exact `WebContents` into
the normal `TabStripModel`. Split promotion first checks the opener and the
four-pane limit. A full split is rejected before ownership changes. If the
subsequent Chromium split mutation unexpectedly fails, the newly inserted tab
is detached and the same `WebContents` is reattached to the overlay. This also
holds if the opener disappears during that mutation: the overlay stays usable
with an explicit unavailable-target message, never as a silent normal tab.

Authentication, payment, passkey, unsupported-scheme and fullscreen-sensitive
flows use a native popup window. A script-created `about:blank` window is
classified again after each committed main-frame navigation. Fullscreen is
forwarded only after the same `WebContents` has moved into that native window.

## Lifecycle

Escape and the close action call `WebContents::ClosePage`, so normal
before-unload processing decides whether closure proceeds. A renderer crash
keeps the crashed `WebContents` in the overlay for Chromium's normal recovery
UI. Closing the opener moves a still-live child to a native popup window.
BrowserView teardown detaches the host and clears the non-restored overlay, so
it cannot leave a phantom tab or corrupt the normal session-restore model.

Hidden overlays register no timers, polling loops or background observers
beyond the observer attached to the currently visible popup.
