# C++ GREEN on 83b6a54c (user-approved bounded run)

Overlay applied via `apply-overlay.sh --compatible-dev-xcode`, targets built
with `AHOI_JOBS=2` (no chrome, install or E2E), owner `build.lock` held.

| Binary | Result |
| --- | --- |
| `ahoi_tab_tree_unittests` | **all tests passed** (incl. Crest 134 empty-merge undo, schema-6 upgrade, duplication) |
| `ahoi_session_unittests` | **all tests passed** (incl. 134 routing restore, 142 R1 backoff, R3 window restore) |
| `ahoi_sync_unittests` | all but one pass, incl. all nine Crest conformance tests (138 MarkerCollisionFrames GREEN, 132, 140, 144) |

The one sync failure is `SyncSecretBoundaryTest.NoRecordCarriesCredentialsOrLocalUrls`:
Bookmark records (entity 11) accept and serialize `file://`, `chrome://`,
`javascript:` and `data:` URLs (credentials are rejected). This is an open
contract conflict between the bookmark validator's "native schemes remain
metadata" rule and DoD 14; it needs a product decision, not a test edit.

The previous run on 17aa591b additionally found two crashes (Workspace
duplication INSERT missing `merged_into`, node-less undo notify), fixed in
`5e90a4e0` and verified here. `ProfileSyncServiceTest.EnabledWithoutCloudKit…`
was flaky once in that earlier run and passed here.
