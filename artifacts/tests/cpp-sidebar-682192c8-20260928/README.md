# ahoi_sidebar_tree_unittests on 682192c8 (first native run of this target)

Bounded owner run (`AHOI_JOBS=2`, owner lock). 168 tests.

**Passing and relevant to this package:** Crest 124 allocator
`DropKeysRemainValidAcrossUnicodeAndLengthBounds` (all three bounds, after a
harness fix: the test opened a second exclusive SQLite connection), Crest 142 R6
`SplitMoveWithUnboundMemberIsRefused`, and
`FailedSavedSplitExtractionRollsBackOrdinaryTargetMove` (stale fixture predating
saved-page Home, fixed in `682192c8`).

**Pre-existing drift, not touched by this package** (this target was not run
since early September; none of these files changed here), see
[failures.txt](failures.txt):
- 11 failures: split/row geometry and folder animation expectations (row
  heights 40 vs 120, reduced motion, fold/reopen), split layout heights, and
  bookmark shelf overflow focus.
- 2 timeouts: bookmark shelf native context actions.
- 1 crash: `SidebarTabDensityTest` passes plain `views::View` panes into
  `CreateOpenTabSplitRowView`, whose `CHECK(SetOpenTabSplitSegmentPresentation)`
  requires `OpenTabRowView`s; product callers pass real rows.

These need a separate Desktop sidebar test-maintenance package.
