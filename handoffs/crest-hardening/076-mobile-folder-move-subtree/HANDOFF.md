# 076 – Mobile: a folder moved to another Workspace leaves its children behind

Status: integrated (mobile, 0ad015a, core tests 301/0/2)
Owner lane: mobile (apply, build, test)
Base: HEAD `fcc1c84` (`git apply --check` passes).
Severity: high (data loss). Found while researching H7 (ADR 0012 section 2).

## Finding

`LocalFirstRepository.moveTreeNode` (`CompanionStore.swift:526-556`) sets
`workspaceID` only on the moved node. Its descendants keep the source
Workspace. This has two effects:

- In the target, the folder appears empty. Its children are filtered by their own
  `workspaceID`, and `validateParent` treats a parent in another Workspace as
  invalid.
- Deleting the source Workspace afterwards (`deleteWorkspace`, `:405-446`)
  tombstones every live node whose `workspaceID` is the source, including the
  moved folder's children. The user loses pages they moved away first.

`CompanionAppModel.moveTreeNode` also enqueues only the moved node, so peers
never learn about the children either. Desktop's `MoveNode` rewrites the whole
subtree (`tab_tree_store_move_delete.cc:77-92`, recursive CTE), so the platforms
disagree.

## Change (`076-mobile-folder-move-subtree.patch`)

- `moveTreeNode` returns `(node, descendants)`. On a cross-Workspace move it
  walks the live subtree and gives every descendant the target Workspace with
  its own version (`stampLocal`), in the same persist. A move within one
  Workspace rewrites nothing else, as before.
- `CompanionAppModel.moveTreeNode` enqueues the node and every rewritten
  descendant.
- New test `testMovingAFolderToAnotherWorkspaceTakesItsSubtree` in
  `CompanionCoreTests.swift`: a two-level subtree moves along, deleting the source
  Workspace then tombstones nothing, and a move within one Workspace rewrites no
  descendants.

Checked by this lane: `AhoiMobileCore` and the patched test file typecheck
against the iOS 26 simulator SDK with `swiftc -emit-module`/`-typecheck`. Not built
or run.

## Not in the patch (owner decision)

Stores that already hold such orphans (a child whose parent lives in another
Workspace) stay as they are. A one-time repair on load could adopt the parent's
Workspace and enqueue the result. That is only safe after the sync owner
agrees, because peers may hold the same records.

## Tests for the owner

- `AhoiMobileCoreTests` (above) on the simulator.
- Visible: move a folder with a subfolder and a page to another Workspace; all
  three show up there. Delete the source Workspace; the moved pages remain.
  After sync, the desktop shows the folder with its children in the target.
