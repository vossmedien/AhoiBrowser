import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_ROOT = ROOT / "patches/chromium"
PATCH_PATH = PATCH_ROOT / "0001-ahoi-m152-integration-seams.patch"
SERIES_PATH = PATCH_ROOT / "series"
M152_COMMIT = "fc4d67f1788019a27e32511137ceccbd2fafdaaa"


def series_entries() -> tuple[str, ...]:
    return tuple(
        line.strip()
        for line in SERIES_PATH.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )


def file_section(payload: str, path: str) -> str:
    match = re.search(
        rf"^diff --git a/{re.escape(path)} b/{re.escape(path)}\n"
        r".*?(?=^diff --git |\Z)",
        payload,
        re.MULTILINE | re.DOTALL,
    )
    return "" if match is None else match.group(0)


def changed_lines(section: str) -> list[str]:
    return [
        line
        for line in section.splitlines()
        if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))
    ]


class VerticalTabsPatchContractTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH_PATH.read_text(encoding="utf-8")

    def test_vertical_tabs_contract_lives_in_the_m152_integration_layer(self):
        entries = series_entries()
        self.assertEqual(PATCH_PATH.name, entries[0])
        self.assertEqual(3, len(entries))
        self.assertEqual(len(entries), len(set(entries)))

        pin = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
        self.assertEqual("152.0.7977.65", pin["version"])
        self.assertEqual(M152_COMMIT, pin["commit"])
        self.assertEqual("Stable", pin["channel"])
        self.assertEqual("Mac", pin["platform"])

        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        self.assertEqual(1, ledger.count(f"## `{PATCH_PATH.name}`"))
        self.assertIn(M152_COMMIT, ledger)

    def test_ahoi_defaults_the_existing_vertical_tabs_profile_pref_to_true(self):
        prefs = file_section(
            self.patch, "chrome/browser/ui/tabs/tab_strip_prefs.cc"
        )
        self.assertTrue(prefs)
        self.assertEqual(
            [
                "-  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, false);",
                "+  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, true);",
            ],
            changed_lines(prefs),
        )
        self.assertNotIn("kVerticalTabsEnabledFirstTime", "\n".join(changed_lines(prefs)))
        self.assertNotIn("kVerticalTabsCollapsedState", "\n".join(changed_lines(prefs)))

    def test_m152_launch_and_horizontal_fallback_feature_gates_are_not_forked(self):
        features = file_section(self.patch, "chrome/browser/ui/tabs/features.cc")
        feature_changes = changed_lines(features)

        self.assertFalse(
            any("BASE_FEATURE(kVerticalTabs" in line for line in feature_changes)
        )
        self.assertFalse(
            any("kVerticalTabsToggleInTabContextMenu" in line for line in feature_changes)
        )
        self.assertFalse(
            any("FEATURE_ENABLED_BY_DEFAULT" in line for line in feature_changes)
        )
        self.assertFalse(
            any("FEATURE_DISABLED_BY_DEFAULT" in line for line in feature_changes)
        )


if __name__ == "__main__":
    unittest.main()
