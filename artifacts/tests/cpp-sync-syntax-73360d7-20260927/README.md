# Bounded C++ source check — 27 September 2026

Frozen source `73360d7a1c427d1a7644d5bd4895870a61f8176d`, including retention
correction `5a7d6b3` and parent fallback `61cab96`. Chromium commit, Ninja and
Clang SHA-256 pins verified; Xcode 27.0/27A266a and macOS SDK 26A425 verified.

The existing AhoiDev object rules supplied actual compiler arguments. The
checker used a Git snapshot of the overlay, with its headers first, replaced
`-c` with `-fsyntax-only`, and removed object/dependency output options. It
invoked argv directly and retained the compiler's actual exit code; the older
scratch helper's grep/head pipeline was not used as success evidence.

All five normal checks exit **0**:

- `sync_store_schema.cc`
- `sync_store_maintenance.cc`
- `tab_tree_sync_adapter.cc`
- `profile_sync_backend_shared_tabs.cc`
- `sync_workspace_merge_unittest.cc` (existing sync-unit object flags)

A copy of the schema source with `#error AHOI_CPP_SYNTAX_NEGATIVE_CONTROL`
exits **1** and contains that diagnostic. This verifies that the check does
not mistake a hidden compiler failure for success.

Three phases used at most two compiler processes. Resource samples were taken
before and between phases. The owned Simulator CE3513BF was shut down on
Root's instruction before the last phase; no foreign simulator was touched.
The owner build lock was released after all six results were verified.

`receipt.json` binds source/tool/input/log hashes. The complete argv plan and
source snapshot stay in its recorded persistent `.work/agent-queue/` path.
This is compiler syntax/type checking only: **no GN generation, overlay
refresh, Ninja build, object output, linking, unit execution, app launch or
installed-browser acceptance**. The new C++ behavioral tests remain pending
the next guarded, bounded candidate build; unchanged Swift 27/27 evidence is
not rerun or relabeled by this check.
