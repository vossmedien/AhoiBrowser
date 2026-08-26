# PERF-11/12 evidence protocol

`ResourcePolicyService::CollectPerformanceEvidence()` records the stable tab
handle, visible URL, navigation-entry count, lifecycle state, primary block
reason, auto-discardable state, and Chromium's pre-discard memory estimate.
It performs no polling and does not influence discard selection.

For a release candidate, capture two JSON snapshots around Chromium Memory
Saver's normal idle interval while a 100-tab corpus is open. Add the resident
memory of the browser process tree as `browser_tree_rss_kib`. Each snapshot has
this shape:

```json
{
  "memory_saver_enabled": true,
  "browser_tree_rss_kib": 800000,
  "tabs": [
    {
      "tab_handle": 1,
      "url": "https://example.test/",
      "navigation_entry_count": 3,
      "state": "awake",
      "block_reason": "none"
    }
  ]
}
```

Evaluate the pair without starting a browser or background workload:

```sh
python3 ahoi/browser/resource_policy/test/perf_11_12_evidence.py \
  --before before.json --after after.json --output report.json
```

The gate requires at least 100 stable tab identities, lower process-tree RSS,
at least one newly sleeping eligible tab, unchanged URL and navigation-entry
counts, and zero sleeping critical tabs. Chromium remains the authority for
the actual discard and history restoration; the harness only verifies the
observable acceptance contract.
