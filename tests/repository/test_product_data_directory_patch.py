import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_PATH = ROOT / "patches/chromium/0002-ahoi-product-data-directory.patch"
SERIES_PATH = ROOT / "patches/chromium/series"
BRANDING_PATH = ROOT / "overlay/chromium/src/ahoi/branding/BRANDING"
PINNED_COMMIT = "fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae"


def parse_branding(path: pathlib.Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, value = line.split("=", 1)
        values[key] = value
    return values


class ProductDataDirectoryPatchContractTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH_PATH.read_text(encoding="utf-8")

    def test_patch_is_second_unique_and_documented(self):
        entries = [
            line.strip()
            for line in SERIES_PATH.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertGreaterEqual(len(entries), 2)
        self.assertEqual(PATCH_PATH.name, entries[1])
        self.assertEqual(len(entries), len(set(entries)))

        ledger = (ROOT / "patches/chromium/README.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(f"## `{PATCH_PATH.name}`", ledger)
        self.assertIn(PINNED_COMMIT, ledger)

    def test_patch_changes_only_outer_app_plist_product_directory_key(self):
        touched_paths = re.findall(
            r"^diff --git a/(\S+) b/(\S+)$", self.patch, re.MULTILINE
        )
        self.assertEqual(
            [("chrome/app/app-Info.plist", "chrome/app/app-Info.plist")],
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
                "+\t<key>CrProductDirName</key>",
                "+\t<string>${CHROMIUM_SHORT_NAME}</string>",
            ],
            changed_lines,
        )
        self.assertEqual(1, self.patch.count("CrProductDirName"))
        self.assertNotIn("AhoiBrowser", self.patch)
        self.assertNotIn("chrome_paths_mac", self.patch)
        self.assertNotIn("--user-data-dir", self.patch)
        self.assertNotRegex(self.patch, r"\.grd(?:p)?\b|IDS_[A-Z0-9_]+")

    def test_existing_branding_seam_materializes_ahoibrowser(self):
        branding = parse_branding(BRANDING_PATH)
        self.assertEqual("AhoiBrowser", branding["PRODUCT_SHORTNAME"])
        self.assertIn("${CHROMIUM_SHORT_NAME}", self.patch)

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
