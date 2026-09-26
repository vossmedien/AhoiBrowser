# 086 – Mobile: merge Workspaces (ADR 0012, WS-MERGE-07)

Status: ready
Owner lane: mobile (apply, build, test)
Base: HEAD `e300a85`. Apply after 076: `git apply --check` passes on HEAD with 076
applied, and also on plain HEAD, since 086 touches none of 076's hunks. 076
stays required for the product: without it, moving a folder through the move
sheet still leaves its children behind.
Mobile half of ADR 0012 section 1; the desktop half is 078/080.

## Change (`086-mobile-merge-workspaces.patch`)

- **Store** (`CompanionStoreWorkspaceMerge.swift`, new):
  - `LocalFirstRepository.mergeWorkspace(_:into:intoFolder:)` runs in one
    mutation and one `persist()`.
  - The source's live roots move, in order, to the end of the target, either
    into one new folder named after the source (with its icon and accent) or
    flat with fresh `OrderKey`s after the target's last root.
  - Every live descendant takes the target Workspace, and the source is
    tombstoned.
  - It returns a `CompanionWorkspaceMergeReceipt` with every rewritten record.
    The folder comes last, so peers never see a live child under a deleted
    folder.
- **Undo** (`undoWorkspaceMerge(_:)`, one level):
  - Every node goes back to its old Workspace, parent and key, and the merge
    folder is tombstoned.
  - The source is un-tombstoned. Each change is a new version, so the undo wins
    on synced devices; mobile merges the tombstone field last-writer-wins.
  - The undo is refused with the new `LocalCompanionStoreError.mergeUndoOutdated`
    if any of these records changed after the merge, for example by sync or a rename.
- **App model** (`CompanionAppModelWorkspaceMerge.swift`, new; state in
  `CompanionAppModel.swift`):
  - `mergeWorkspace`, `undoWorkspaceMerge` and `dismissWorkspaceMergeUndo` go
    through `performLocalFirstMutation` and enqueue the returned records.
  - `pendingWorkspaceMergeUndo` drives the undo banner.
  - `connectWorkspaceMerges(to:)` moves the source's open **normal** tabs to the
    target (`MobileBrowserController.moveTabs(fromWorkspace:to:)`, new). Undo
    moves exactly those tab IDs back. It is wired once in `AppEntry.Runtime.init`,
    which covers all three runtimes.
- **Separated Workspaces** (ADR 0011): refused.
  - `canMergeWorkspace` excludes any Workspace that
    `separatedWorkspaces.isSeparatedWorkspace` reports. Such Workspaces also
    never appear in `snapshot`.
  - The menu only offers valid targets. A direct call shows "Vollständig getrennte
    Workspaces lassen sich nicht zusammenführen …".
  - Reason: a page never changes its WebKit data store (`moveTab` already
    refuses that), so a merge would have to reopen URLs per `WS-ISO-05`. Refusing
    is the smaller, safe step; reopening can follow if wanted.
- **UI** (`CompanionViews.swift`):
  - A "Zusammenführen mit …" menu in the Workspace row's context menu.
  - A confirmation with page and folder counts, offering "Als Ordner
    zusammenführen" or "Ohne Ordner zusammenführen". An empty source merges
    without asking.
  - A bottom banner with "Rückgängig" and close.
  - Eight new strings (de/en) in `Localizable.xcstrings`.
- **Xcode project:** `project.pbxproj` gains the three new files. The delta comes
  from `xcodegen generate` on a scratch copy; rerunning XcodeGen yields the
  same entries with the owner's IDs.
- **Tests:** new `CompanionWorkspaceMergeTests.swift` in `AhoiMobileCoreTests`:
  - merge into a folder, with the undo restoring the source;
  - a flat merge keeps the source order after the target;
  - the undo is refused after a rename;
  - the same or a missing target is refused;
  - the model moves open normal tabs (not private ones) and the undo moves them back.

Checked by this lane: `AhoiCloudKitSpike`, `AhoiMobileCore` (`-emit-module
-enable-testing -D DEBUG`), all `AhoiMobileCoreTests` sources and the
`AhoiMobileApp` sources (`-typecheck -parse-as-library`) typecheck against the
iOS 26 simulator SDK (`arm64-apple-ios26.0-simulator`, Swift 6) without errors.
The only warning in touched files predates the patch. Not built or run.

## Open for the owner

- The merge moves tabs through the existing `moveTab`, so each one is a normal
  shared-tab `.move` intent.
- Not checked by this lane: whether other mobile state names the source
  Workspace by ID (for example remembered selection, routing or archive
  metadata). The library view switches its selection to the target itself.
- The banner stays until closed, undone or replaced by the next merge. The undo
  is not kept across an app restart.

## Tests for the owner

- `AhoiMobileCoreTests` on the simulator.
- Visible (`WS-MERGE-07`):
  - Merge a Workspace with a folder, a page and an open tab into another one. The
    folder named after the source appears at the end, the tab shows in the
    target, and the source is gone.
  - "Rückgängig" restores all of it.
  - After sync, the desktop shows the same result.
