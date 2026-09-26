# 058 – The archive's split token expires

Status: ready
Owner lane: desktop (apply with build 36)
Base: HEAD `7510ccd` (`git apply --check` passes). By the crest-hardening lane;
syntax-checked read-only against `out/AhoiDev` (0 errors), not built.

## Finding (low, from 034)

`MarkSplitsClosingForArchive` records the native split token before the
agreed pages close. `OnSplitChanged` consumes it on `kSplitTabRemoved` and
keeps the split record live (011 S2). If an agreed page never closes (hung
renderer, unload that never completes), the token stays forever. A later
manual dissolve of that same split is then taken as "closed by the
archive", and the record is not tombstoned.

Clearing the token on the next `Refresh` while the tab is still there
(the owner's first idea) would bring the S2 defect back, because Refresh
runs before the asynchronous `ClosePage()` finishes. The token therefore
gets a lifetime instead.

## Change

- `archive_closing_splits_` becomes `std::map<token, base::TimeTicks>`
  (marked when the archive starts closing).
- `kArchiveCloseGrace = 30 s` and the pure `ArchiveCloseTokenLive(marked,
  now)`.
- `OnSplitChanged`: a `kSplitTabRemoved` with a live token keeps the record
  (as before); an expired token is dropped and the removal is handled as a
  normal dissolve.
- `Refresh` prunes expired tokens.

## Tests

- `ArchiveCloseTokenTest.ExpiresAfterTheGrace` in
  `workspace_structure_split_capture_unittest.cc`.
- The split-archive journey (034) must still pass: archive → restore brings
  the split back.
