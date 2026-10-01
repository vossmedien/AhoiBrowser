# Frozen Build 60 sidebar failure, isolated

Source d0688d88, exact generated binary, 1 job, 0 retries, 285 seconds idle
at the actual launch gate, no competing Ahoi UI/locks. The single native menu
delete case fails again at line 362: the shelf no longer owns the menu after
the pre-run hook/event loop. Direct exit 1. This contradicts the earlier
parallel-interference-only hypothesis. The native test was executed; the full
sidebar suite and installer were not run. The installed app remains Build 59.

The next source package changes this regression to establish owner activation,
then post its action from the pre-run hook (matching Chromium's interaction
observer). It must assert a live, showing native MenuController and execute
the real delete command before checking model/button effects. Those assertions
are preserved and strengthened; no native green or product closure is claimed
until the next exact binary executes this correction and its whole sidebar suite.
