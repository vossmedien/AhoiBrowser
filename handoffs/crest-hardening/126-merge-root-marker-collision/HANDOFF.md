# 126 — Ordinary opaque key can imitate a projected merge-root position

Status: ready for owner design/test/fix; source review only
Owners: desktop/sync and mobile
Reviewed source: `ce24827` (R1 core `cfd0127`), 27 September 2026.

## Reproducer

R1 stores its derived root segment inside an ordinary `sort_key` spelling:
`!:ahoi-merge-root/<workspace UUID>/...`. C++ `MergeRootSuffix` in
`tab_tree_sync_adapter.cc` uses `rfind(marker)` anywhere in a raw key (lines
67–77). Swift `CompanionTreePosition.mergeRootSuffix` uses the last literal
substring anywhere in the key (`CompanionTreeReordering.swift`, lines 19–23).
Both projectors treat a positive match as evidence that the node belongs to
the rebased segment, even when its raw Workspace never participated in a merge.

Use a live Workspace B with two ordinary root Pages:

| Page | Raw `sort_key` | Raw visible order |
| --- | --- | --- |
| Q | `Q!:ahoi-merge-root/<B UUID>/x` | first |
| Z | `Z` | second |

The marker-like bytes are valid UTF-8 in an opaque key and the current readers
do not reserve or reject this spelling. C++ `ApplyMergeRootOrder` excludes Q
from the ordinary tail, chooses `Z` as tail, and rewrites **only the presented**
Q key to `Z!:ahoi-merge-root/<B UUID>/x` (lines 103–129). Swift sets
`anchoredRoots` for Q even without a merged Workspace, then applies the same
rebasing in `CompanionSnapshot.swift` (lines 106–165). The visible order becomes
Z,Q. Raw clocks/bytes remain unchanged, so repeated projection remains
deterministically wrong. Source-level analysis establishes this counterexample;
no native run is claimed.

Escaping `!` while *creating* a suffix prevents one nested source key from
containing a generated marker, but it does not authenticate a pre-existing
opaque key that already contains the marker. A last-substring lookup also
accepts arbitrary text before it. This is an ambiguity in the marker-as-key
design; exact matching of the marker at one offset reduces accidental matches
but does not fully distinguish authored positions from legacy/foreign raw keys.

## Required owner decision and evidence

Keep 112's target-tail and 118's subsequent explicit order behavior while
ensuring ordinary raw keys remain ordinary. The fix may use durable local
projection metadata, a rigorously reserved/validated key namespace with a
compatibility rule for existing opaque keys, or another cross-platform
unambiguous representation. Do not add a Format-3 field or change field clocks
solely for a passive projection without the sync contract owner's review.

Add the exact Q/Z case on both adapters and no-merge workspaces, then include
it in capture, persist/reopen, search/passive reconciliation, and a fresh peer
projection. Preserve Q's raw Page bytes and clocks throughout. Also test a
genuine explicit move into the segment followed by a new target root and
received undo, so the repair does not regress stable tail ordering. The raw
key and actual mutation output should cross to the opposite implementation.

No product files or fixtures were changed by Crest. These are source findings,
not a C++/Swift behavioral pass or live sync claim.
