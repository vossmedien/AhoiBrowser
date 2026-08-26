import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]

import sys

sys.path.insert(0, str(ROOT / "tools"))

from source_line_budget import (  # noqa: E402
    MAX_SOURCE_LINES,
    SourceLineCount,
    collect_source_counts,
    tree_source_counts,
    violations,
)


class SourceLineBudgetTests(unittest.TestCase):
    def test_every_ahoi_owned_source_file_stays_within_budget(self):
        live_checkout = ROOT / ".work/chromium/src"
        counts = collect_source_counts(
            ROOT,
            chromium_src=live_checkout if live_checkout.is_dir() else None,
        )
        over_budget = violations(counts)
        self.assertEqual(
            (),
            over_budget,
            "Ahoi source exceeds the 800-line modularity budget:\n"
            + "\n".join(f"{item.lines}: {item.path}" for item in over_budget),
        )

    def test_tree_scan_ignores_non_source_and_reports_physical_lines(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-line-fixture-") as raw:
            root = pathlib.Path(raw)
            (root / "feature.cc").write_text("one\ntwo\n", encoding="utf-8")
            (root / "notes.md").write_text("ignored\n", encoding="utf-8")
            self.assertEqual(
                (SourceLineCount("fixture/feature.cc", 2),),
                tree_source_counts(root, logical_prefix="fixture"),
            )

    def test_violation_boundary_is_strictly_greater_than_800(self):
        counts = (
            SourceLineCount("ok.cc", MAX_SOURCE_LINES),
            SourceLineCount("too-large.cc", MAX_SOURCE_LINES + 1),
        )
        self.assertEqual((counts[1],), violations(counts))


if __name__ == "__main__":
    unittest.main()
