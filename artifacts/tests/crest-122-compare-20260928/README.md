# Crest 122 direct C++/Swift merge-output comparison — **PASS**

28 September 2026, Crest lane, user-authorized; owner lock protocol held
(`build.lock` for each native step), nothing deleted.

| Fixture | Cases | Verdict | Differences |
| --- | ---: | --- | ---: |
| merge_v3.json (140 standard) | 140 | PASS | 0 |
| merge_utf8_sort_keys_v3.json (120) | 8 | PASS | 0 |
| merge_inventory_asset_v3.json (128) | 6 | PASS | 0 |
| merge_remaining_entities_v3.json (130) | 36 | PASS | 0 |
| merge_domain_groups_v3.json (140) | 24 | PASS | 0 |
| merge_sequences_v3.json (132, 104 steps) | 18 | PASS | 0 |

- **C++:** owner GREEN build of `83b6a54c` (`out/AhoiDev/ahoi_sync_unittests`,
  SHA-256 `c242c12c…723f`), `--gtest_filter=SyncMergeConformanceTest.*
  --single-process-tests`, 7/7 passed, exit 0 (`cpp/test.log`), outputs and
  receipts bound to a clean clone at `83b6a54c`; compared under the same lock
  so the binary hash still matched.
- **Swift:** Crest build-for-testing of `17aa591b` (git archive, 2 jobs) and
  `test-without-building` of six `SyncMergeConformanceTests` on CE3513BF with
  `TEST_RUNNER_AHOI_SYNC_CONFORMANCE_*`: 6/6 passed, exit 0
  (`swift/test.log`, `swift/progress.txt`); xctest binary SHA-256
  `b127315c…d884`; receipts bound to a clean clone at `17aa591b`.
- `git log 17aa591b..83b6a54c` touches no `overlay/…/sync`, `fixtures/
  sync-conformance` or `apps/AhoiMobile/Sources` path, so both sides run the
  same sync product code and fixtures. `compare/*.txt` holds the comparator
  reports. An earlier attempt (`crest-122-cpp-17aa591b-20260928`) became
  INSUFFICIENT only because the owner rebuilt the C++ binary before the
  comparison; that is the guard working as intended.
