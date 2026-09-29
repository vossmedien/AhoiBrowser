"""The explicit per-module line budget; behavior is covered by Views tests."""

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SIDEBAR = ROOT / "overlay/chromium/src/ahoi/browser/ui/sidebar"


class SidebarBookmarkShelfContractTest(unittest.TestCase):
    def test_component_files_respect_the_line_budget(self):
        files = sorted(SIDEBAR.glob("sidebar_bookmark_*"))
        self.assertTrue(files)
        for path in files:
            with self.subTest(path=path.name):
                self.assertLessEqual(
                    len(path.read_text(encoding="utf-8").splitlines()), 800
                )


if __name__ == "__main__":
    unittest.main()
