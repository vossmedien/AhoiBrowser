# Crest 126/134/138/140 owner changes: bounded C++ syntax check

Pinned Chromium Clang `-fsyntax-only` on the overlay of `fc29437e`
(snapshot via `git archive`, included ahead of the Chromium tree), with
compile flags taken from existing `out/AhoiDev` objects of the same GN
target (`ninja -t commands -s`). Three new test files without an object yet
reuse a sibling test of their target as flag template. At most two compiler
processes ran; the owner `build.lock` was held and released; no other lock
was present. CPU idle was 51 % before and 60 % after.

Result: **13/13 files pass** after one fix. The first run found a missing
`PrefService` include in `session_bridge_workspace_merge_unittest.cc`;
`f05ac14e` adds it and the rerun of that file exits 0.

Covered: tab-tree store, merge, undo, queries, snapshot and schema-6 migration
(Crest 134), session prefs and merge bridge routing receipt (134), the
provenance-bound merge-root projection (126), and the 132/138/140 C++ runners.

This proves syntax/type compatibility only. Nothing was compiled to objects,
linked, run or installed; GN was not regenerated, so BUILD.gn wiring of the
new tests is unverified. 138 RED/GREEN, the unit tests themselves and all
visible/peer evidence remain open. Receipt: [receipt.json](receipt.json);
private plan/logs under `.work/agent-queue/cpp-syntax-fc29437e-20260928/`.
