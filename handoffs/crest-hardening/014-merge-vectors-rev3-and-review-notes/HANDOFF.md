# 014 – Merge vectors revision 3 and review of the 010–013 intake

Status: integrated bf3f304 (vectors rev. 3; WS-DEL-09 not applicable)
Owner lanes: desktop/sync (overlay testdata), mobile (Swift rerun)
Base: `88b4875`

## 1. Vectors revision 3 (follows the Sync decision on 012)

`d2debaa` makes `MergeRecordFields` validate a union and return `invalid`
when it breaks a record invariant. The reference model now does the same for
the appearance accent rule, and the new vector
`appearance_custom_accent.cross_group_union_rejected` pins the reported case:
the Mac turns on the system accent at T2, the iPhone picks accent colour X
at T3, and the result is `invalid`. That makes 131 vectors.

Apply: copy `fixtures/sync-conformance/merge_v3.json` over
`overlay/chromium/src/ahoi/browser/sync/testdata/merge_v3.json`. Rerun
`SyncMergeConformanceTest.SharedVectors` with the next planned build, and
`SyncMergeConformanceTests` on the simulator (the new vector is appearance,
which the Swift runner does not cover). No extra build.

## 2. Review of the intake (source reading)

| Item | Commit | Result |
| --- | --- | --- |
| 010 R1 overlap | `990c7bb` | same as the proposed patch |
| 010 R2 load-to-delete | `17f5319` | computes the directory (unit test against a loaded partition) |
| 010 R3, R6 re-homing and late pages | `17f5319` | pages marked `closing_with_deleted_workspace`, unbound with `clear_workspace`, late pages closed |
| 011 S1 and rule | `8a9fc91` | move switches through `WorkspaceService`; rule in `ARCHITECTURE.md` |
| 012 option a | `d2debaa` | union validated inside `MergeRecordFields` |
| 013 I1, I2 | `8a9fc91` | Workspace menu reopens separated Workspaces after restart; a `creating` entry is finished or deleted at startup |

Residual (low): a page opened during the deletion prompt is closed with
`ClosePage()`, which may show its own before-unload prompt. If the user
cancels there, the page stays open with the deleted Workspace's partition
and no Workspace, and its data is then cleared under it. Either close such
late pages without their prompt, since the deletion was already confirmed,
or show them in the fallback as "Sitzung gelöscht" after a reload.
- **WS-DEL-09**: prompt open; open page B with a before-unload handler in
  the same Workspace; confirm the deletion; cancel B's own prompt. B is not
  left without a Workspace while still holding the deleted accounts.

Still deferred by the owner, with reasons: 010 R4 and R5, 011 S2–S8 (order
S5, S7, S6, S2, S3, S8, S4), and 013 I4.

## Owner intake (desktop/sync, 2026-09-25)

- Vectors rev. 3 copied (`bf3f304`); C++ run with build 21, Swift run on the
  simulator follows (appearance is covered by the Swift runner since `75aeea8`).
- WS-DEL-09 residual: `WebContents::ClosePage()` "causes the current page to
  be closed, including running its onunload event handler"
  (`content/public/browser/web_contents.h:670-672`); it does not dispatch
  before-unload, so a late page cannot show its own prompt or veto. No change.
