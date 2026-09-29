# Merge-root projection: bounded C++ source checks

Frozen source `cfd0127008ef7b28c999677ee301ad2e3e1d5b69` passes actual pinned
Clang `-fsyntax-only` analysis of `tab_tree_sync_adapter.cc`,
`sync_workspace_merge_unittest.cc` and the new
`sync_workspace_projection_conformance_unittest.cc`. All three exit 0.

The recorded AhoiDev flags from the earlier `73360d7` check are reused with
unchanged Clang, Ninja and generated-args hashes; source and overlay-header
inputs come from this new Git snapshot. Its immutable command plan and source
hashes are bound in the receipt. No GN regeneration or shared-overlay refresh
was performed. Unchanged schema/maintenance files and the already proven
compiler-error control were not repeated.

Resource samples precede both phases and follow completion; at most two
compiler processes ran concurrently. The owner lock was released. CE3513BF
was already shut down at the final check; no foreign simulator was touched.

This proves source syntax/type compatibility only. No object or browser binary
was built/linked, no C++ test was executed, and no app was installed/launched.
In particular, shared 112 vectors, actual native ordering/mutation/capture,
retention and 114 fallback require behavioral execution on a guarded candidate.
Full private plan/logs remain in `.work/agent-queue/cpp-syntax-cfd0127-20260927/`.
