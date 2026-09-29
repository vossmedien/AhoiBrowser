# Swift RED/GREEN: Crest 126/138 marker collision and Crest 144 setting values

Approved bounded run on the owner simulator CE3513BF (build-for-testing with
`-jobs 2`, then `test-without-building`; simulator shut down after each phase).

- **RED** on `54c2901` (before the provenance-bound marker fix):
  `testMarkerCollisionProjectionFrames` fails with **18 failures**, one per
  projection (2 cases × 3 Workspace × 3 Node array orders). Each shows the
  ordinary marker-like key reordered as Z,Q instead of Q,Z, exactly Crest 126.
- **GREEN** on the current commit: the same test and Crest 144's
  `testBrowserSettingValueVectors` pass, **2 tests, 0 failures**.

Commits and test-binary hashes: [receipt.json](receipt.json). The C++ side of
138 and 144, Crest 122 cross-platform output comparison, and all visible/peer
evidence are separate.
