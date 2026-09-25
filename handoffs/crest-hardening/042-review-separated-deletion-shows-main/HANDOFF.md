# 042 – Review of 5fedf6e: windows after deleting a separated Workspace

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD (`git apply --check` passes). Review by the crest-hardening lane
of `5fedf6e`; also confirms 028 → `6f66a44` and 030 → `047d739`, both
byte-identical to the handed-off patches.

## Verdict

`5fedf6e` fixes both findings from the build-30 WS-ISO journey: after a
restart, deleting the presented separated Workspace now shows the main
windows again, and the sweep drops `deleting` entries without a directory.
One low finding.

## L1 (low): chained hand-over shows two windows

Deletion is only possible from the separated Workspace's own window
(`AcceptWorkspaceDialog`). With a chain main → X → Y in one run, the main
windows stay hidden behind X, and X is hidden behind Y with a
`HandOverWatch`(Y, X). Deleting Y now:

1. posts `ShowMainWindowsAfterIsolatedDeletion("Y")`, which clears the pref
   and shows every hidden main window;
2. when Y's window closes, the watch shows X and records X as presented.

Result: main and X are both visible, although the hand-over contract has one
visible window, and the restart restore then hides main behind X again.
Before `5fedf6e` only X was shown.

Fix (patch attached, 11 lines): `HandOverWatch::HasPresentedWindowOf(dir)`.
`ShowMainWindowsAfterIsolatedDeletion` still clears the pref, but it shows
the main windows only when no watch of this run exists for the deleted
Profile's windows, i.e. in the restart case that `5fedf6e` targets.

## Notes (no change requested)

- The sweep's async `DirectoryExists` reply may call `RemoveIsolatedProfile`
  for an entry that the loop above already removed. That looks idempotent;
  worth one line in the unit test if the registry has one.
- Visible check: WS-ISO journey steps 14–15 (delete after restart) plus
  one chain main → X → Y, delete Y: only X is visible.
