# 046 – Review of 663398a (042 reworked): record the window that comes back

Status: integrated 5be0782 (build 32; unit tests green; visible check pending)
Owner lane: desktop (apply, build, test)
Base: HEAD (`git apply --check` passes). Review by the crest-hardening lane.

## Verdict

The rework is better than 042's proposal: it shows exactly the windows
hidden behind the deleted Profile's windows and keeps the main-window
fallback for the restart case. Agreed. One low finding.

## L1 (low): the shown window is not recorded as presented

`ShowMainWindowsAfterIsolatedDeletion` clears `kPresentedProfileDirPref`
(it named the deleted Y) and then shows X from the watch list. It does not
record X. The watch's own `OnPresentedClosed` would have recorded it
(`:167`), but it only does so for a window that is still hidden, and X is
already visible by then. In the chain main → X → Y, deleting Y leaves the
pref empty while the main windows are still hidden behind X. After a
restart `RestoreHandOverAfterStartup` finds no presented Profile, session
restore shows every window, and main and X are both visible.

Fix (attached, 3 lines): `RecordPresentedProfile(browser->GetProfile())`
right after `window->Show()` in the watch branch. For the simple case
main → Y, this records the main Profile, as the watch did before.

## Visible check

Chain main → X → Y, delete Y, quit, restart: only X is visible.
