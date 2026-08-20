import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_PATH = ROOT / "patches/chromium/0001-ahoi-vertical-tabs-default.patch"
SERIES_PATH = ROOT / "patches/chromium/series"


class VerticalTabsPatchContractTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH_PATH.read_text(encoding="utf-8")

    def test_patch_is_first_and_documented(self):
        entries = [
            line.strip()
            for line in SERIES_PATH.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertGreaterEqual(len(entries), 1)
        self.assertEqual(PATCH_PATH.name, entries[0])
        self.assertEqual(len(entries), len(set(entries)))

        ledger = (ROOT / "patches/chromium/README.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(f"## `{PATCH_PATH.name}`", ledger)
        self.assertIn("fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae", ledger)

    def test_patch_changes_only_the_two_ahoi_defaults(self):
        touched_paths = re.findall(
            r"^diff --git a/(\S+) b/(\S+)$", self.patch, re.MULTILINE
        )
        self.assertEqual(
            [
                (
                    "chrome/browser/ui/tabs/features.cc",
                    "chrome/browser/ui/tabs/features.cc",
                ),
                (
                    "chrome/browser/ui/tabs/tab_strip_prefs.cc",
                    "chrome/browser/ui/tabs/tab_strip_prefs.cc",
                ),
            ],
            touched_paths,
        )

        changed_lines = [
            line
            for line in self.patch.splitlines()
            if line.startswith(("+", "-"))
            and not line.startswith(("+++", "---"))
        ]
        self.assertEqual(
            [
                "-BASE_FEATURE(kVerticalTabsLaunch, "
                "base::FEATURE_DISABLED_BY_DEFAULT);",
                "+BASE_FEATURE(kVerticalTabsLaunch, "
                "base::FEATURE_ENABLED_BY_DEFAULT);",
                "-  registry->RegisterBooleanPref("
                "prefs::kVerticalTabsEnabled, false);",
                "+  registry->RegisterBooleanPref("
                "prefs::kVerticalTabsEnabled, true);",
            ],
            changed_lines,
        )

    def test_horizontal_fallback_contract_remains_in_upstream_control_path(self):
        # The legacy gate remains off, the full-launch feature is still
        # externally disableable, and its existing menu toggle parameter is not
        # replaced. The exact changed-line allowlist above prevents either
        # fallback seam from being edited without making this test fail.
        changed_lines = [
            line
            for line in self.patch.splitlines()
            if line.startswith(("+", "-"))
            and not line.startswith(("+++", "---"))
        ]
        self.assertFalse(
            any("BASE_FEATURE(kVerticalTabs," in line for line in changed_lines)
        )
        self.assertFalse(
            any("kVerticalTabsToggleInTabContextMenu" in line for line in changed_lines)
        )
        self.assertNotIn("vertical_tab_strip_state_controller", self.patch)
        self.assertNotIn("browser_view", self.patch)


if __name__ == "__main__":
    unittest.main()
