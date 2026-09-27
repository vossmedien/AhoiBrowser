# 098 – Refuse a mobile merge undo that would orphan a later child

Status: integrated 4f2b154 (source and regression cases; Swift parsing passes; XCTest RED/GREEN and visible acceptance pending host-capacity/test gates)
Owner lane: mobile (review, apply and run the focused unit tests)
Base: committed `e9d25be` / mobile implementation `e9ba41a`; the patch also
passes `git apply --check` against the current working tree containing the
owner's uncommitted 084 changes. H7, ADR 0012, `WS-MERGE-03/07`.

## Finding (high, source-confirmed; runtime test pending owner)

`undoWorkspaceMerge` in `CompanionStoreWorkspaceMerge.swift` checks versions
only for the source Workspace and the nodes in the merge receipt. Neither
`createTreeNode` nor `moveTreeNode` increments the destination parent's version.
Consequently, adding or moving an unrelated page below a merged folder leaves
all existing undo guards satisfied.

Concrete sequence:

1. Merge A into B with the default folder option.
2. Add a page under the newly created merge folder (or move an existing B page
   into it).
3. Undo the merge. The receipt has no snapshot for that page, so the current
   implementation leaves it live in B while tombstoning its parent folder.

A second case does not need the new merge folder: add a child to a moved
pre-existing folder, then undo. The folder moves back to A while its new child
keeps B. This also affects a flat merge. Both violate the live-tree relationship;
source tracing establishes the invalid state, not an installed-app observation.

## Minimal correction

Before the first record/clock mutation, scan live nodes outside the receipt.
If one has a parent inside the receipt, reject with the existing
`mergeUndoOutdated` error. The new child is preserved in its current tree;
ordinary unrelated edits at B's root still permit undo. The existing mutation
lock covers the scan and the subsequent mutation. No wire/schema/UI change.

The patch adds four XCTest methods covering five situations: new child of the
merge folder; new child of a moved folder in both merge modes; existing page
moved into the merge folder; unrelated target-root addition (undo succeeds).
Refusal cases compare the complete before/after snapshot and reload the same
store through a fresh repository to ensure no partial mutation was persisted.

## Apply and validation

Apply only `098-mobile-merge-undo-descendants.patch` from the repository root.
The `files/` path mirrors are review copies based on the **committed** source;
do not copy them over the owner's working tree, because they intentionally
exclude the uncommitted 084 additions.

Lane checks completed on 27 September:

- Patch applicability: PASS against current Mobile WIP, without applying it.
- Swift parser: PASS for both proposed files (`swiftc -frontend -parse`).
- No product files changed; no module build, XCTest/simulator, E2E or app launch.

Owner must run `CompanionWorkspaceMergeTests`: run the added tests before the
source hunk to record RED (the three refusal methods should fail), then apply
the guard and run the entire class for GREEN. A syntax parse is not a typecheck
or a behavioral pass. Visible `WS-MERGE-07` remains paused by the current user
test restriction and needs renewed authorization before execution.

Remaining scope: this guard protects the local mobile receipt against later
children; it does not claim end-to-end Sync, Desktop undo or the rest of H7.
