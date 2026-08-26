import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_ROOT = ROOT / "patches/chromium"
PATCH_PATH = PATCH_ROOT / "0001-ahoi-m152-integration-seams.patch"
SERIES_PATH = PATCH_ROOT / "series"
BRANDING_PATH = ROOT / "overlay/chromium/src/ahoi/branding/BRANDING"
M152_COMMIT = "fc4d67f1788019a27e32511137ceccbd2fafdaaa"
PLIST_PATH = "chrome/app/app-Info.plist"


def parse_branding(path: pathlib.Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, value = line.split("=", 1)
        values[key] = value
    return values


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


class ProductDataDirectoryPatchContractTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH_PATH.read_text(encoding="utf-8")

    def test_product_directory_contract_lives_in_the_m152_integration_layer(self):
        entries = series_entries()
        self.assertEqual(PATCH_PATH.name, entries[0])
        self.assertEqual(3, len(entries))
        self.assertEqual(len(entries), len(set(entries)))

        pin = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
        self.assertEqual("152.0.7977.65", pin["version"])
        self.assertEqual(M152_COMMIT, pin["commit"])

        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        self.assertEqual(1, ledger.count(f"## `{PATCH_PATH.name}`"))
        self.assertIn(M152_COMMIT, ledger)

    def test_outer_app_plist_derives_the_product_directory_from_branding(self):
        plist = file_section(self.patch, PLIST_PATH)
        self.assertTrue(plist)
        self.assertEqual(
            [
                "+\t<key>CrProductDirName</key>",
                "+\t<string>${CHROMIUM_SHORT_NAME}</string>",
            ],
            changed_lines(plist),
        )
        self.assertEqual(1, self.patch.count("+\t<key>CrProductDirName</key>"))
        self.assertNotIn("AhoiBrowser", plist)
        self.assertNotIn("chrome_paths_mac", plist)
        self.assertNotIn("--user-data-dir", plist)
        self.assertNotRegex(plist, r"\.grd(?:p)?\b|IDS_[A-Z0-9_]+")

    def test_ahoi_branding_materializes_the_plist_variable_for_both_profiles(self):
        branding = parse_branding(BRANDING_PATH)
        self.assertEqual("AhoiBrowser", branding["PRODUCT_SHORTNAME"])
        self.assertIn("${CHROMIUM_SHORT_NAME}", file_section(self.patch, PLIST_PATH))

        for profile_name in ("ahoi-dev.gn", "ahoi-release.gn"):
            profile = (ROOT / "config/build" / profile_name).read_text(
                encoding="utf-8"
            )
            with self.subTest(profile=profile_name):
                self.assertIn(
                    'branding_file_path = "//ahoi/branding/BRANDING"', profile
                )


if __name__ == "__main__":
    unittest.main()
