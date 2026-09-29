# C++ RED: Crest 126/138 marker collision on the pre-fix parent

`ahoi_sync_unittests` built from `54c29011` (overlay applied by
`apply-overlay.sh --compatible-dev-xcode`, `AHOI_JOBS=2`, owner lock held),
binary SHA-256 `80dac8e14ad5e1838a0151639a29206a607e6407c0424818d891e5cb7e168d1f`.
`--gtest_filter=SyncWorkspaceProjectionConformanceTest.*`:
**MarkerCollisionFrames FAILED with 18 equality mismatches**, one per projection,
each ordering the ordinary marker-like key as Z,Q instead of Q,Z. This matches
the Swift RED ([mobile-redgreen](../mobile-redgreen-126-138-144-20260928/README.md)).

The same RED build also failed to compile an unrelated test
(`session_bridge_workspace_merge_unittest.cc` missing a `PrefService`
include, fixed later in `f05ac14e`); the sync binary had already linked.
C++ GREEN on `32d6e400` is pending: the host guard refused it at 56.5 GiB free
(64 GiB required). Log: [red-ahoi_sync_unittests.log](red-ahoi_sync_unittests.log).
