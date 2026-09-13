# Corrective Native Structure candidate

Exact source `a47b18510f24384ad370ab2c1b230ea33e40193d` is the complete89993af
packet plus the six diagnosed compile corrections in five Native files. Common,
Swift, wire/schema, fixtures and compiler warning policy are unchanged. Original
failure evidence remains in `../native-structure-89993af-20260913/` with build1.
Its raw Ninja log retains upstream trailing spaces deliberately; source diff
checks are clean, but the original log was not normalized to manufacture one.

The same isolated worktree `/private/tmp/ahoi-native-structure.HoBjQO/repo`
was cleanly advanced only after invocation68856 finished. No output reset or
new build tree is used. Existing incremental objects remain reusable.

At preparation CPU was65%idle and memory_pressure52%free, but disk was only
33,878,252KiB (32.31GiB), about0.31GiB above the mandatory32GiB floor. The
corrective heavy run has NOT started pending sufficient actual workspace for
compilation/link/staging. No cleanup permission was inferred. Root owns the
running installed55ab/bba UI; this worker performs no installation/app action.

After capacity is sufficient, run-build.sh uses the same guarded app-only path,
two jobs, separate immutable logs/exits, and a receipt only on actual success.
The finished copy will later use the existing Mac profile and the untouched969
manifest, not the old bba profile/scope. No signing or runtime pass is claimed.
