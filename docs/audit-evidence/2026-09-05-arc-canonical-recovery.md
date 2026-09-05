# Arc canonical snapshot / native receipt — corrective package

The live `3d413ef` UI became available in the current continuation. The app was
opened normally and the previous session continued. Fresh Arc discovery showed
1 workspace, 36 folders, 133 pages, 3 splits, 0 degraded, 1 excluded, 0 already
present, with correctly aligned choices. The explicit Import action then ran.

## Observed failure (not a completed import)

The UI ended with the manual-recovery message. Unlike the previous FK failure,
the backup directory and owner-only manifest now exist. Read-only metadata:

- Authoritative tree: 2 workspaces, 174 nodes, 155 nested nodes, 0 FK violations.
- Journal v5: `prepared`, phase `manual_recovery_required`, planned native
  mutation true, 170 affected IDs, 7 planned native members, expected native
  structure hash present and no completed native receipt.
- All 7 planned split-member nodes retain their original modification time.
- No second import, backup deletion, tree restoration or journal edit was made.
  The backup and failed transaction are preserved in the real Default profile.

The planned-member fields do not prove native split reconstruction started:
they are computed before the backup. The stronger source diagnosis is a
pre-runtime canonical-order mismatch, not an established navigation/title race.

## Two confirmed contract errors and source corrections

1. `MergeArcImportPlan` appended new workspaces/nodes to the old vectors. Store
   export orders workspaces by `(sort_key,id)` and nodes by `id`; snapshot
   equality is order-sensitive. Therefore successful persistence could fail
   the exact post-write comparison, then also fail importer-owned rollback
   classification and enter manual recovery. The merge now exports the canonical
   snapshot through its existing validator store. Fields, undo and source-plan
   split/member ordering remain intact; comparisons are not weakened.
2. Native session receipts required the import window to be the globally active
   profile window, contradicting the runtime contract that imports must not
   steal focus or fail when another window/app is selected. Only that global
   activation check is removed; target-window membership, selected pane and
   native split structure checks remain exact.

New regressions cover Merge→occupied store→Export equality with interleaved IDs,
workspace ordering, nonempty undo, repeat no-op and preserved source ordering;
receipts with another active window retain negative membership/selection tests.
They are source changes pending the common build and candidate-bound execution.

After closing the terminal error dialog, native Computer Use again returned a
window title with no AX controls or screenshot, including after a fresh runtime
reset. The app process remained alive; this is neither a crash verdict nor a
visible success. Current manual recovery must be resolved through verified,
explicit recovery; never erase its journal to obtain a green rerun.
