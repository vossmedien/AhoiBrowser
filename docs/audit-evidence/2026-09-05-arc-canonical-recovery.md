# Arc canonical snapshot / native receipt — corrective package

## Latest installed readback — 4cb622a, 21:23 UTC

The corrected candidate starts and its native window can be activated with the
advertised Raise action before keyboard commands. After normal restart the
Ahoi Settings body and standard Import dialog were both rendered and readable.
The prior menu-only CUA observation is not an unresolved browser crash or a
reason to rebuild. The recovery widget lives in `people_page/ahoi_arc_import_section`,
inside the standard Import dialog's Arc selection, not in the Ahoi Settings page.

Opening the real native source picker showed Arc disabled while its process
was running. No hidden WebUI message or disabled-item bypass was used. The user
was asked to close Arc normally; process42725 was still live at21:23:43UTC.
Source picker/dialog were cancelled without starting an import. Ahoi67242 was
then quit normally; its process disappeared. No journal/backup write or recovery
action occurred in this continuation.

Read-only metadata still shows journal version5/prepared/manual_recovery_required,
170 affected IDs, 7 native members, planned mutation true and no completed native
receipt. The referenced manifest is a regular file, mode0600, owned by the current
user; its raw SHA256 equals the journal's manifest hash. This does not replace
the recovery service's full backup, exact-tree and native-session validation.
After Arc closes, select it in the same installed candidate's standard Import
dialog, use the explicit verified recovery action, then create a fresh preview.

## Historical 3d413ef failure and source diagnosis

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

## Combined follow-up source — not yet built or executed

Canonical merge and native-window receipt corrections are committed in
`ef0f965`, together with the owned folder-motion and rounded-surface package.
The installed app remains `3d413ef`; it was quit normally after its terminal
failed import. No real-profile restoration, journal deletion or repeat import
has occurred.

Full-service inspection found a further lifecycle gap: freshly opened split
members do not yet have a non-initial navigation entry that Chromium can save.
`ArcImportNavigationBarrier` now waits for those native commits before the real
SessionService receipt, with weak ownership and a bounded timeout. Open HTTP
streams do not block this barrier. Scoped metadata deferral keeps only automatic
title/URL mirroring from invalidating the importer-owned snapshot; direct user
edits remain visible and cause conservative recovery, never a rollback over
the newer title/tree.

Explicit recovery is exposed through the existing Settings import section. It
requires the unchanged prepared/manual journal, no completed native receipt,
a hash/permission-verified backup, an exact previous or expected tree, no live
affected node binding and no temporary tab in a workspace that would be removed.
The current native session is flushed/read via SessionService and checked for
the same node/workspace references; the live tree is rechecked before restore
and after persistence. The journal is restored only after the backed-up tree is
durable. Backups stay available. No native tab is closed to make recovery pass;
no retry/import runs implicitly. The helper's concrete temporary-workspace
reassignment finding was corrected at both live and native-session boundaries.

Ten new `ArcImportServiceBrowserTest` cases cover the real slow-navigation
commit/receipt/no-op chain, lost pending panes, concurrent user edits, exact
backup recovery, newer trees, bound tabs, temporary workspace tabs, changed
backups, completed receipts, and stale tokens/source changes. Source fixtures
use actual discovery/capture/parser/backup/journal/SQLite/SessionService paths.
The only fixture seam replaces locating the real Arc source; a running real
Arc causes an explicit skip, not a source-use bypass. Settings WebUI source also
covers explicit single submission, busy lock and no auto-discovery/retry after
success or refusal. These are written tests, not execution evidence.
