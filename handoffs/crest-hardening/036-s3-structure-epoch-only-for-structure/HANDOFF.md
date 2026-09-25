# 036 – S3: only structure changes cancel a structure commit

Status: integrated aa1058f (BUILD.gn context refreshed; unit tests on build 33; visible stress reproduction not run, audio-cancel case by journey pending)
Owner lane: desktop (reproduce, apply, build, test)
Base: HEAD `472c4af` with 028, 030, 032 and 034 applied in that order
(`git apply --check` passes on that stack; the `BUILD.gn` hunk needs 032).
Implementation of 011 S3 by the crest-hardening lane; not compiled by the
lane.

## Reproduce first (S3 was inferred from source)

`LocalAuthority()` captures `scope_->epoch`; every `OnNativeChanged()` bumps
it. The bump currently also happens for events that change no structure:

- `OnTabStripModelChanged` with `kSelectionOnly` (`session_bridge_observers.cc:463-465`);
- `OnTabTreeChanged` with `MutationKind::kRenamed`, which the store emits for
  `RenameNode`, `UpdateFolderPresentation`, `UpdateSavedPageMetadata` (title
  and URL of a loading saved page) and `SetSavedPageHome` (`:546-548`);
- every resource-status callback (`workspace_structure_controller.cc:74-80`),
  e.g. loading, audio or visibility changes.

When one of them arrives while `CommitWorkspaceStructureState` is on the
persistence runner, the guarded authority fails
(`session_bridge_structure_persistence.cc:147-149`), the commit returns
`kCancelled`, and `ArchiveAgreedPages` rolls the entry back and reports
failure; the pages stay open. A cancelled refresh commit is also not
retried until the next event (`Refresh` returns while `persisting_`).

Repro on the current candidate: in a Workspace with a few saved pages that
are still loading (or one playing audio in another Workspace), archive a
temporary group repeatedly. Expected defect: some archives report failure
and leave the pages open, without any user action in between.

## Change

- Three pure classifiers next to `ClassifySplitCapture`:
  `TabStripChangeInvalidatesStructure` (all but `kSelectionOnly`),
  `TreeChangeInvalidatesStructure` (all but `kRenamed`),
  `ResourceChangeInvalidatesStructure(was, now)` (only the transition into
  protection, i.e. `CanArchiveTab` turning false).
- `OnNativeMetadataChanged()` reschedules a refresh without bumping the
  epoch. The bridge observers and the resource callback use it for the
  non-structural cases.
- `protected_tabs_` (`std::set<tabs::TabHandle>`) remembers the last
  protection state per tab; closed tabs are pruned in `Refresh()`. A page
  that newly starts audio, capture or an unsaved form still cancels a pending
  archive or remote dissolve, as before.
- The post-apply tree notification (`session_bridge_sync_persistence.cc:224-227`)
  and split changes keep bumping; both are structure.

Not changed: whether a failed refresh commit should reschedule itself (a
retry loop on a database error needs its own backoff decision), and the
`closed` result of the group-close path, which is computed right after the
asynchronous `ClosePage()`; check its visible result together with 034.

## Tests

- New `workspace_structure_invalidation_unittest.cc` in
  `ahoi_session_unittests`: `SelectionOnlyKeepsStructureAuthority`,
  `ArchiveSurvivesUnrelatedTitleChange`, `OnlyNewProtectionCancels`.
- Visible journey: the reproduction above archives every time; starting
  audio in a page of the group during the archive still cancels it.
