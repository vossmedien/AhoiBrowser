# Crest 122 C++ merge-output export — source `17aa591b`

Run by the Crest lane on 28 September 2026 at 12:55 CEST (user-authorized),
holding the owner `build.lock` only for the test process; nothing was built.
Binary: owner GREEN build `out/AhoiDev/ahoi_sync_unittests`
(`cpp-green-17aa591b-20260928b`), SHA-256
`4da3d5d08f6e076d711d40ef38061ad38120c102e4410279b5773fd71c064035`.
Command: `ahoi_sync_unittests --gtest_filter=SyncMergeConformanceTest.*
--single-process-tests` with `AHOI_SYNC_CONFORMANCE_OUTPUT_DIR` and
`AHOI_SYNC_CONFORMANCE_RUN_ID=crest-122-cpp-17aa591b-125554`; **7/7 passed,
exit 0** (`test.log`). Six outputs `cpp-<fixture>.json` with receipts
`cpp-<fixture>.receipt.json` from `record_runner_receipt.py` against the
owner checkout at `17aa591b` (`BrowserSettingValueVectors` has no merge
export by design). The Swift counterpart and `compare_runner_outputs.py`
comparison are pending.
