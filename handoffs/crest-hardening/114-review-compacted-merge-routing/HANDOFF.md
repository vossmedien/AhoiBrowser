# 114 – Review of retained merge routing after compaction

Status: integrated in 2bd75cb; adapter and strengthened merge tests pass pinned-Clang source analysis at cfd0127; native retention/fallback execution pending.
Owner: desktop/sync; base `5a7d6b3`, checkpoint `31f592f`
Scope: 108 retention follow-up. Review only; no product files modified or tests run.

## Source conclusions

The normal retained-route path is consistent with the intended local metadata:

- `CompactExpiredTombstones` retains only source/target UUIDs inside the same
  transaction as the deletion watermark and payload/tombstone removal. The
  existing retention/outbox selection is unchanged. An ordinary deletion clears
  any route row in that transaction.
- The getter joins Workspace deletion watermarks, checks canonical valid UUIDs
  and rejects self-targets. A dangling route without the matching entity-type
  watermark is not returned.
- `ResolveMergeTarget` follows mixed present and compacted chains with a visited
  set; a present live or ordinary-deleted record takes precedence. The backend
  passes the local map into native reconciliation under its existing authority
  checks. No wire payload, clock, remote delete or second sync authority is added.
- Existing compaction semantics remain unchanged: local writes with a deletion
  watermark conflict; incoming resurrection/newer replacements are quarantined.
  The new local table does not itself authorize an undo after compaction. Do not
  infer such a behavior from the resolver's ability to prefer a supplied live row.

## R1: stale native fallback can shadow a known compacted source

Source-confirmed adapter edge case, **not an observed installed-browser loss**:

1. There is no active Workspace in the authoritative `workspaces` input.
2. The native snapshot still has old live A, and retained metadata says A→B.
3. Before calling `ResolveMergeTarget`, the `active.empty()` fallback loop
   inserts native A into `result.workspaces` and `workspace_indexes` as live.
4. The resolver now returns A immediately, shadowing A→B. A late Page stays at A.

`SharedTabState().projection_ready` checks authority/initial fetch, not that an
active Workspace row exists. The new compaction tests use an empty native
snapshot and therefore do not exercise this fallback. Product reachability and
visible effects still require the owner's normal candidate tests.

Minimal proposal: exclude known compacted merge **sources** from the native
fallback candidates. If no other known live fallback exists, return nullopt
and defer projection as the adapter already does; do not invent a destination.
If the native snapshot has live B as well, skip old A and retain the existing
native fallback behavior for B. This does not change generic recovery for
unrelated missing Workspaces, retention policy or incoming clock gates.

The accompanying patch adds that one condition and two pending regressions:
`CompactedSourceIsNotResurrectedAsNativeFallback` and
`NativeFallbackSkipsCompactedSourceForKnownTarget`. Both should fail on the
unpatched adapter by source tracing; no RED/GREEN execution is claimed.

## R2: full raw-Page preservation needs a stronger assertion

The new test checks only `raw_page.workspace_id` and `raw_page.version`.
C++ `SyncVersion` contains model version and top stamp; `field_versions` is a
separate member of `TreeNodeRecord`. Thus the test currently does not prove
that every field clock or the complete wire payload stayed unchanged.

The patch captures the **stored** Page immediately after `PutLocalRecord`
(which normalizes field maps), then compares full `TreeNodeRecord` equality
and same-codec serialized bytes after compaction. Comparing against the input
helper's empty field map would be the wrong baseline. Native projection/capture
loop behavior still requires its own integration coverage (112).

## Apply and evidence limits

Apply `114-compacted-source-fallback.patch`; `files/` mirrors the two proposed
files at `5a7d6b3` for review. Do not replace a newer owner's full file with a
mirror. `git apply --check` passes against the observed current source without
applying the patch. No compiler, native binary, simulator, XCTest, GUI or test
suite was started during the delayed 1159 coordination.

Next owner run: the full `SyncWorkspaceMergeTest` suite on the regular frozen
candidate, including the two original compaction tests and these additions.
Keep R1 **root ordering** from 108 distinct from R1 **fallback** in this review;
root ordering, Mobile compaction, shared 112 apply coverage, backend recapture
and actual native retention acceptance remain open.
