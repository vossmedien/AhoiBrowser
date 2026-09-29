# 118 – R1: real reorder/insertion must survive passive projection

Status: partly implemented in cfd0127 (real Swift move/insert/reload/undo regression passes in 34/34); all-row capture/search/replay, real Desktop allocator and opposite-platform actual-output acceptance remain open.

Follow-up review on `ce24827`: [124](../124-desktop-utf8-order-allocation/HANDOFF.md)
pinpoints the production Desktop allocator's invalid UTF-8 midpoint;
[126](../126-merge-root-marker-collision/HANDOFF.md) pinpoints a raw opaque
key that both projectors mistake for a derived segment. These source findings
keep the full 118 acceptance open after the scoped `cfd0127` Swift result.
Owner: desktop/sync and mobile; no competing product implementation
Base: 112 raw fixture SHA `4dd5370f84742aa022a6f690d075f5313db9aa25be37254780eefafa0a317dc8`.
This adds operational acceptance cases without changing that frozen fixture.

## Rule and test setup

112 pins **initial** tail placement of late nodes. It does not require a
permanent "native roots before projected roots" sort class. Once a real local
operation writes a position after/between a projected node, passive projection
must preserve that position rather than recompute a new tail beyond it.

Use 112's named frames as the initial raw authority. Aliases:

- `K` = `a2000000-0000-4000-8000-000000000010` (existing target Page).
- `X` / `Y` = IDs ending `0011` / `0012` (late source Pages).
- `F` = ID ending `0020` (folder in the subtree cases).
- `A` / `B` = Workspace IDs `a1000000-0000-4000-8000-000000000001` /
  `a1000000-0000-4000-8000-000000000002`.
- `N` = the actual ID returned by the production creation path; bind it at
  runtime, do not replace the creator with a manually fabricated record.

Call the production position allocator **and** durable mutation path. On
Mobile, the current `reorderTreeNode(_:before:)` supports an end destination
with nil successor; create/move paths must use the same real lexical writer.
On Desktop, exercise the key-writing path used by the actual tree/drop action,
not only `TabTreeStore::MoveNode` with a hand-authored key. Record the entry
point in the test evidence. No custom sorter/reference projection should
supply the product result under test.

## Required operational cases

Every row includes the repeated-projection/reload checks in the next section.
Orders below are the visible sibling order of the original fixture IDs plus N.

| Case | Seed frame | Real operation | Required result |
| --- | --- | --- | --- |
| `native_after_projected` | `single_late_root / merged`: B `[K,X]` | Reorder K to the end, after X | B `[X,K]`; K's location changes, raw X remains byte/clock-identical |
| `create_after_projected` | `single_late_root / merged`: B `[K,X]` | Create N at the displayed root end | B `[K,X,N]`; original K/X raw records unchanged |
| `create_between_projected` | `late_roots_source_order / merged`: B `[K,X,Y]` | Insert N between X/Y through the real insertion path; if creation appends, then use the real reorder-before-Y path and report both operations | B `[K,X,N,Y]`; raw X/Y unchanged |
| `projected_before_native` | `single_late_root / merged` | Explicitly reorder X before K | B `[X,K]`; X has a real new location clock and durable B membership, K unchanged |
| `projected_relative_to_projected` | `late_roots_source_order / merged` | Explicitly reorder Y before X | B `[K,Y,X]`; raw X unchanged, Y's real location position stamped |
| `native_into_projected_folder` | `late_subtree / merged`: B `[K,F]`, F `[X,Y]` | Move K into displayed F after Y | B `[F]`, F `[X,Y,K]`; F/X/Y raw authority unchanged; K's durable parent F and effective B are valid |
| `create_in_projected_folder` | `late_subtree / merged` | Create N at F's displayed end | F `[X,Y,N]`; F/X/Y raw records unchanged, N is visible under F in B |
| `deleted_anchor_then_insert` | `single_late_root / merged` | Delete K, then create N after displayed X | B `[X,N]`; repeated projection cannot move X after N; raw X unchanged |
| `rename_after_explicit_order` | Complete `native_after_projected` first | Rename K without moving it | B remains `[X,K]`; K's location clock and raw X are unchanged by the rename |

For the explicit projected-Page moves, also deliver the newer Workspace A
revival from 112's undo frame **before compaction**. The explicitly moved Page
stays in B; untouched passive Pages follow the revived A. This tests a received
undo, not a fabricated local undo receipt for a merge performed elsewhere.
The existing post-compaction resurrection gate is a separate policy.

## Persistence and no-repair-echo checks (each case)

Capture the actual wire bytes/field clocks immediately after the explicit
operation. Then require the same sibling order and unchanged raw records after:

1. two ordinary passive projections;
2. search/passive shared-tab reconciliation and recapture;
3. store persist/reopen;
4. a fresh projection context fed the resulting raw records in different
   Workspace/Node array orders (no retained local ordering cache).

The opposite platform must eventually consume the **actual** raw mutation
output, not a separately hand-built equivalent key. A same-platform replay is
useful but is not that cross-platform proof. Neither stage needs live CloudKit
just to test the production record/adapter paths; real transport acceptance
still remains separately gated.

For reorder/move, require a newer `location` field clock on explicitly touched
records; `modified_at` may also change. Unrelated groups and untouched Pages
must stay unchanged. Creation has its normal complete map; deletion has its
normal tombstone write. During passive checks, no further Page/location clock
or raw-byte change is allowed. A deliberate supported multi-node rebalancing
operation must declare its touched set and commit atomically; it must never be
hidden inside a passive projection.

## Opaque-key edge checks

Use the production UTF-8 lexical comparison, not the numeric adapter carried
alongside a Desktop wire key. Capture the effective predecessor/successor keys
at the moment of the operation and verify the written key is strictly within
those bounds (or follows the defined end rule). Do not pin the fixture to an
unpublished derived-key spelling. Cover prefix bounds, equal-key/ID-tie inputs,
non-ASCII valid UTF-8 and the existing wire-length limit. If no valid interval
exists, use the product's explicit atomic rebalance/refusal contract; never
persist an oversized key or a partially applied mutation.

## Evidence boundary

These are shared operational acceptance requirements for the owner's ongoing
implementation. No production file, key algorithm or writer was changed by
Crest. No C++/Swift/GUI test is claimed. The 112 JSON and its hash stay unchanged.
The new opaque-key code is still owner WIP; review its frozen source/results
when available. H3/H5 leases and native test/build slots remain independent.
