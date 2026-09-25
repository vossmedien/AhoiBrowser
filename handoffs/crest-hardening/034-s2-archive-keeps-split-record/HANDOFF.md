# 034 – S2: archiving a live split keeps its split record restorable

Status: ready
Owner lane: desktop (reproduce, apply, build, test)
Base: HEAD `e59fb0b` (`git apply --check` passes). Implementation of 011 S2
by the crest-hardening lane; not compiled by the lane.

## Reproduce first (S2 was inferred from source)

On the current candidate: open a two-pane split, archive it (sidebar
"Archivieren"), then restore it from the archive. Expected defect: the
restore fails and the split record is tombstoned in the structure store.
The cause is that `ArchiveAgreedPages` closes the pages through
`GroupPageClose::ClosePages()` without the `applying` guard, and the close
is asynchronous anyway. Chromium then reports `kSplitTabRemoved`, and
`OnSplitChanged` tombstones the record as if the user had dissolved the
split. `RestoreArchived` then refuses (`…archive.cc:444-451`).

## Change

- `MarkSplitsClosingForArchive(archive_id)` records the native split tokens
  of the archive's pages right before `group->ClosePages()`.
- `OnSplitChanged` consumes a matching token on `kSplitTabRemoved`. It
  leaves the record live and only schedules a refresh, without tombstoning.
  Any other split removal keeps the old behaviour.

## Tests

- No unit harness exists for `WorkspaceStructureController`, so the lane
  cannot add a focused unit test without one. Suggested once a harness
  exists: `ArchiveLiveSplitKeepsSplitRecordRestorable`.
- Visible journey (the acceptance): the reproduction above now restores the
  split with its layout and ratios. Dissolving a split by hand still
  tombstones its record.
