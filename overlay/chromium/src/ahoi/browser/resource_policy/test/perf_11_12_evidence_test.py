#!/usr/bin/env python3

import copy
import unittest

from perf_11_12_evidence import evaluate


def _snapshot(tab_count: int = 100) -> dict:
    return {
        "memory_saver_enabled": True,
        "browser_tree_rss_kib": 800_000,
        "tabs": [
            {
                "tab_handle": index,
                "url": f"https://example.test/{index}",
                "navigation_entry_count": 3,
                "state": "awake",
                "block_reason": "active-pane" if index == 0 else "none",
            }
            for index in range(tab_count)
        ],
    }


class PerfEvidenceTest(unittest.TestCase):
    def test_accepts_memory_reclaim_with_state_preservation(self) -> None:
        before = _snapshot()
        after = copy.deepcopy(before)
        after["browser_tree_rss_kib"] = 600_000
        after["tabs"][1]["state"] = "sleeping"
        report = evaluate(before, after)
        self.assertTrue(report["passed"], report)

    def test_rejects_discarded_protected_tab(self) -> None:
        before = _snapshot()
        after = copy.deepcopy(before)
        after["browser_tree_rss_kib"] = 600_000
        after["tabs"][0]["state"] = "sleeping"
        report = evaluate(before, after)
        self.assertFalse(report["checks"]["critical_tabs_protected"]["passed"])

    def test_rejects_lost_history_or_identity(self) -> None:
        before = _snapshot()
        after = copy.deepcopy(before)
        after["browser_tree_rss_kib"] = 600_000
        after["tabs"][1]["state"] = "sleeping"
        after["tabs"][1]["navigation_entry_count"] = 1
        after["tabs"].pop()
        report = evaluate(before, after)
        self.assertFalse(report["checks"]["tab_identity_retained"]["passed"])
        self.assertFalse(
            report["checks"]["navigation_history_retained"]["passed"]
        )


if __name__ == "__main__":
    unittest.main()
