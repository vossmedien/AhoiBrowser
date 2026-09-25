# 050 – ADR 0011 step 2: Quick Window adoption respects the hand-over

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `351f99c` (`git apply --check` passes; independent of 044/048).
By the crest-hardening lane; not compiled.

## State and gap

Already correct (checked): link routing opens a Quick Window in the target
Profile, also for a separated Workspace (`link_routing_dispatch.cc:220-243`);
the global shortcut opens it in the Profile of the last active window; and
adoption ("In normalem Fenster öffnen") stays in the Quick Window's Profile
(`MoveActiveTabToNormalWindow`), reopening the URL for a Workspace with
its own website sessions (030).

Gap: the adopting window is the Profile's most recently activated normal
window. While a separated Workspace is presented over it, that window is
hidden. Adoption then calls `Show()` + `Activate()` on it, and two
windows cover the same frame; the hand-over bookkeeping (watch, presented
pref) does not know about it. The same happens for a newly created
adopting window. Typical path: routed link with a Quick Window rule for a
main Workspace while a separated Workspace is in front, then "In normalem
Fenster öffnen".

## Change

`ShowAdoptingWindow(target)` replaces both `Show/Activate` pairs: if the
adopting window is hidden (not minimized) and a visible normal window of
another Profile exists, it calls `session::PresentProfileWindow(target
Profile, that window, DoNothing)`, i.e. the regular hand-over (same frame,
the other window hidden, watch and pref recorded). Otherwise it shows and
activates as before. `command_bar` gains the `session:isolated_profiles`
dep.

## Tests

Browser test not added: it needs a second, registered separated Profile
with a presented window, which the existing Quick Window browser test does
not set up. Suggested once a helper exists:
`QuickWindowBrowserTest.AdoptionHandsOverFromPresentedSeparatedWindow`.
Visible journey (WS-ISO-06 part): present separated Workspace S over the
main window; open a routed link whose rule targets main Workspace A in the
Quick Window; "In normalem Fenster öffnen": the main window takes S's
frame, S is hidden, the page is active in A; switching back to S restores
it. Without a hand-over (main window visible), adoption behaves as before.
