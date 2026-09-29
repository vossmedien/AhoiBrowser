# 016 – Review of ADR 0011 step 2, part 1 (window hand-over)

Status: integrated cfcc187 (H1, H2; H3 and H4 deferred)
Owner lane: desktop
Reviewed: `bc3cdc2` (`session/isolated_workspace_directory.cc`, sidebar menu
and dispatch). This is source reading only; the commit says "not yet built".

## Matches the contract

- Both kinds of window list each other's Workspaces. Choosing one presents
  the target Profile's window in the source window's frame, including the
  maximized state.
- The source window is truly hidden (`Hide()`), not minimized, so its pages
  stay loaded and switching back reloads nothing.
- `HandOverWatch` shows the hidden window again when the presented one
  closes, for example after the separated Workspace was deleted, so the user
  is never left with only hidden windows.
- The main Profile is chosen deterministically and never a separated one
  (unit-tested).

## Findings

**H1 (medium) – Fullscreen and sidebar state are not handed over.**
`CaptureFrame` hands over only the restored bounds of a fullscreen source
("the target does not enter fullscreen"). Hiding a fullscreen window on
macOS leaves its fullscreen Space, and the target appears on the desktop
Space with a Space switch. WS-ISO-04 requires that size, position,
fullscreen and sidebar state carry over. Either enter fullscreen on the
target, or record this as a deliberate deviation in ADR 0011 and WS-ISO-04.

**H2 (medium) – Restart after a hand-over.** Session restore still records
the hidden source window as open. After quit and relaunch, both Profiles'
windows can come back visible and stacked in the same frame.
- **WS-ISO-17**: switch from the main to a separated Workspace, quit, and
  relaunch. Exactly one window is visible (the last presented one), and the
  other Workspace is reachable through the menu without a reload loop.

**H3 (low) – Hidden windows keep working.** Media, WebRTC and timers in a
hidden window continue without a visible window. This matches switching
Workspaces inside one window, but there the media indicator stays reachable
in the sidebar. The hidden window's audio should stay discoverable, for
example as a media indicator next to its Workspace in the menu.
- **WS-ISO-18**: play audio in the main Workspace, switch to a separated
  one; the playing Workspace is identifiable and can be paused without
  switching back.

**H4 (info)** – WS-ISO-11 should measure the memory of hidden windows, since
every hand-over keeps the source's renderers alive.

Still open in step 2 by the owner's own list: routing, Quick Window,
import/export and a process-wide sidebar order.

## Lane note (25 September 2026)

Written in a desktop-owner session by mistake; adopted by the
`crest-hardening` lane. H1 verified at `bc3cdc2`
(`isolated_workspace_directory.cc:183-186` hands over the restored bounds and
never enters fullscreen). H2 is inferred: the hand-over code does not
exclude the hidden source window from session restore.

## Owner intake (desktop, 2026-09-25)

- H1: a fullscreen source leaves fullscreen, the presented window enters it
  (`cfcc187`, sidebar dispatch `RunHandOver`).
- H2: the last presented Profile is stored in Local State
  (`ahoi.isolated_profiles_presented_dir`); after restore the other
  Profiles' windows in its frame are hidden again (`RestoreHandOverAfterStartup`).
  Verified by WS-ISO-17 on the next candidate.
- H3 deferred: a media indicator for hidden windows belongs to the shared
  switcher's process-wide list (step 2 rest).
- H4: WS-ISO-11 memory measurement includes hidden windows (H3 lease run).
