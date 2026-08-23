import hashlib
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_ROOT = ROOT / "patches" / "chromium"

EXPECTED_PATCHES = {
    "0001-ahoi-vertical-tabs-default.patch":
        "b80085cda395720dfb0846b851125c758b30345df3d8499914751bf4257af90c",
    "0002-ahoi-product-data-directory.patch":
        "0d51b451b3ad1ae5868a1500c84fd24c7289d5ac7ab3e8d7fb89ac3ccaf0016e",
    "0003-ahoi-nested-tab-tree.patch":
        "7ef8ff7064628165663fa3631c23465cb55f7f8c2b22ed428473781481e81002",
    "0004-ahoi-workspace-navigation.patch":
        "8f6d8820d8e955f445f0cf9f984f45d68d3ceda4e4904a959c861d8a3dd27fc6",
    "0005-ahoi-session-bridge.patch":
        "f766762a7271843173ea857e4cfc50bdd0bd28944c37ef63b022b541f1e90376",
    "0006-ahoi-sidebar-tree-model.patch":
        "e7a66af71585c19a06a9d0df471df4d1d3e8634da8ae5532fc416a17aaf9eb95",
    "0007-ahoi-native-command-bar.patch":
        "c313e7f39bd5305549c3f8b726f23f3e91f9e4ad225baefdc108e112d76a46ce",
    "0008-ahoi-three-pane-split.patch":
        "86d6ba7c8edc0416638c04180b5757a8ed73b5d48c5ac47ae2b2bc2e77f94173",
    "0009-ahoi-sidebar-views.patch":
        "867a180f6991517ad8fcc14a4651529012056f7b4be17aa2f9f2925e35fe44e0",
    "0010-ahoi-live-sidebar-integration.patch":
        "5665ce99b3af2566ff2e4fbf3d33c5fad7d5d2c027256baade522da2173be8ec",
    "0011-ahoi-drag-and-tree-persistence.patch":
        "07d1aa647639ddaf37b76f17cdd7c7064fdd4590788d5b43a18a7d9b2db795c0",
    "0012-ahoi-visual-language-and-nested-search.patch":
        "e6b779668c4cd975b1af1392b8217c286d671b4aa67d74a690a6cb2becb03da4",
    "0013-ahoi-group-actions-and-localization.patch":
        "95acf7680dd94c3bf9e87145b47cb2e38c20df79bcef645487d2d4909b31be49",
    "0014-ahoi-live-tab-lifecycle-and-bidirectional-drag.patch":
        "af3d19046845ea89e123c244dd5e3c3dd894342b8479573646e91ee99542324f",
    "0015-ahoi-save-shortcut-and-drag-feedback.patch":
        "548fa9715844505d56b4227d03632dc7e45bdbdeb723cbcaa756c9c3cd1b0010",
    "0016-ahoi-modular-ui-auth-and-native-drag.patch":
        "af1ad74015825edb48181485bfe58909b0ee69ef4e24b544e78f47a637064bbc",
}


class ProductPatchStackTests(unittest.TestCase):
    def test_series_order_and_patch_hashes_are_exact(self):
        entries = [
            line.strip()
            for line in (PATCH_ROOT / "series").read_text(
                encoding="utf-8"
            ).splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertEqual(list(EXPECTED_PATCHES), entries)

        for filename, expected_hash in EXPECTED_PATCHES.items():
            with self.subTest(patch=filename):
                payload = (PATCH_ROOT / filename).read_bytes()
                self.assertEqual(expected_hash, hashlib.sha256(payload).hexdigest())
                self.assertIn(b"diff --git a/", payload)

    def test_every_product_patch_has_a_ledger_entry(self):
        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        for filename in EXPECTED_PATCHES:
            with self.subTest(patch=filename):
                self.assertEqual(1, ledger.count(f"## `{filename}`"))


if __name__ == "__main__":
    unittest.main()
