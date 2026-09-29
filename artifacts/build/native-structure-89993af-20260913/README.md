# Native Structure product candidate

Frozen source: `89993af004d3c73a202422fe9c950310e149ac92`.
Isolated source worktree: `/private/tmp/ahoi-native-structure.HoBjQO/repo`.
Canonical dirty documentation/registry work is excluded; the previous receiver
worktrees, 55ab product/signing/install receipts, Device24 and bba remain intact.

This is the single app-only guarded candidate after the combined Native
split/archive/Home, search and confirmed-delete source package. No test target,
pin roll, dependency update, new release branch or checkout reset is requested.
The existing Chromium .65 checkout and incremental out/AhoiDev are reused only
through apply-overlay/build-ahoi verification. No installation or UI start is
part of this build script.

Capacity at preparation: 12 cores/32 GiB, two CPU samples 44/53% idle; memory
pressure reports47% free, with no swap-out increase in the sampled interval.
Existing foreign photoanalysis/Chrome/Docker/pytest work is untouched. Two Ninja
jobs leave capacity for those workloads. About41 GiB disk is available: the
documented low-disk override applies below64 GiB, retaining the32 GiB hard floor.

`run-build.sh` retains separate overlay/build exits and copies the canonical
build receipt only on actual success. Keep-going collects independent compiler
failures in the same chrome graph without suppressing warnings or adding tests.
Build completion, scoped969 preparation/signing, installation and visible
acceptance remain separate gates and are not implied by this preparation note.

During hooks/post-hook verification, free disk briefly reached32,808,056KiB
(31.29GiB), below the32GiB hard floor; sampled CPU was21%idle and the unrelated
Virtualization VM occupied16GiB. Only the verified build shell16141 (parent12554)
was SIGSTOPped before compiler launch. Free space subsequently rebounded to
33.8GiB, then stabilized at33.5GiB with42.5%CPU idle and no additional swap-outs.
After exact PID/parent/command plus hard-floor revalidation,16141 was CONT-resumed;
hooks had completed successfully. Handle68856 remains the same active run, not
failed/completed or restarted at that point. No foreign process, installation,
profile or retained candidate was changed.

Final result: invocation68856 TERMINAL EXIT1; overlay.exit0/build.exit1. The
193-action chrome graph executed every reachable action (169 completed/failed
before dependent links became unreachable). Six independent compiler causes
were collected in five Native source files: archive type forward declaration,
the pinned cstring_view SQL API, full menu enum header, unsafe raw-array policy
indexing, one shadowed restore callback identifier and lvalue DictValue chaining.
The DeleteArchivedPages declaration mismatch is consequential to the first
header error, not a separate API mismatch. Original logs/exit files are retained.
No successful app receipt, copy/sign preparation, installation or UI evidence
exists for89993af. The corresponding prepare-sign.sh was never executed.
