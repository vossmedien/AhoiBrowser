# Native GREEN on ba9f26cb (user-approved bounded runs)

- **C++** (`apply-overlay.sh --compatible-dev-xcode`, `AHOI_JOBS=2`, no chrome,
  install or E2E): `ahoi_tab_tree_unittests`, `ahoi_session_unittests` and
  `ahoi_sync_unittests` — **all tests passed**. This includes the bookmark
  secret boundary after the DoD 14 owner decision (non-portable bookmarks
  stay local), Crest 134 undo/schema 6, Crest 142 R1/R3, Workspace
  duplication and all nine Crest conformance runners (126/138 GREEN).
- **Swift** (`build-for-testing`, `-jobs 2`, `test-without-building` on
  CE3513BF, shut down afterwards): **61/61 passed** across bookmark relay/wire/
  integration, Format-3 conformance, Workspace retention/merge and the Files
  Web Extension spike. List: [swift-tests.txt](swift-tests.txt).

Both runs held the owner `build.lock`. No visible journey, installation or
CloudKit round trip is claimed.
