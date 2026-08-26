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
    "0017-ahoi-product-source-freeze.patch":
        "5f00c505128e5c3c22f0c9d6d055000a12bfca49887f12dfd1d1d64fe368f4d7",
    "0018-ahoi-user-agent-client-hints-brand.patch":
        "75cd21010930a8f551253a52fec7f8337e606c90d7f773fdb325240c4adb6561",
    "0019-ahoi-deterministic-platform-tests.patch":
        "ef8294746937cd7070c89fc7fc2ca5cc3ffb5fee9895356345a6b6199fbd73b4",
    "0020-ahoi-upstream-site-data-clock-revert.patch":
        "50ec277a35046de4e6cb36cb1f90f464c7bd42db349624d499ea843daddaba39",
    "0021-ahoi-upstream-page-load-tracing-test-isolation.patch":
        "d4ccef20872a7432aec4e3dd0f1e9b7ec58f599bea82780324e8040f08b2d2e5",
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

    def test_source_freeze_patch_is_a_full_index_modification_only_delta(self):
        payload = (
            PATCH_ROOT / "0017-ahoi-product-source-freeze.patch"
        ).read_text(encoding="utf-8")
        headers = [
            line for line in payload.splitlines() if line.startswith("diff --git a/")
        ]
        index_lines = [
            line for line in payload.splitlines() if line.startswith("index ")
        ]

        self.assertEqual(242, len(headers))
        self.assertEqual(242, len(index_lines))
        self.assertTrue(
            all(
                len(line.split()[1].split("..", 1)[0]) == 40
                and len(line.split()[1].split("..", 1)[1]) == 40
                for line in index_lines
            )
        )
        self.assertNotIn("new file mode", payload)
        self.assertNotIn("deleted file mode", payload)
        self.assertNotIn("similarity index", payload)
        self.assertNotIn("rename from", payload)
        self.assertNotIn("rename to", payload)

    def test_user_agent_brand_patch_has_one_exact_product_path(self):
        payload = (
            PATCH_ROOT / "0018-ahoi-user-agent-client-hints-brand.patch"
        ).read_text(encoding="utf-8")
        headers = [
            line for line in payload.splitlines() if line.startswith("diff --git a/")
        ]
        index_lines = [
            line for line in payload.splitlines() if line.startswith("index ")
        ]

        self.assertEqual(
            [
                "diff --git a/components/embedder_support/user_agent_utils.cc "
                "b/components/embedder_support/user_agent_utils.cc"
            ],
            headers,
        )
        self.assertEqual(1, len(index_lines))
        self.assertEqual(
            [40, 40],
            [
                len(object_id)
                for object_id in index_lines[0].split()[1].split("..", 1)
            ],
        )
        self.assertIn(
            'const std::string product_brand(version_info::GetProductName());',
            payload,
        )
        self.assertIn('if (product_brand != "Chromium")', payload)
        self.assertNotIn("new file mode", payload)
        self.assertNotIn("deleted file mode", payload)
        self.assertNotIn("rename from", payload)
        self.assertNotIn("rename to", payload)

    def test_deterministic_platform_test_patch_has_two_exact_test_paths(self):
        payload = (
            PATCH_ROOT / "0019-ahoi-deterministic-platform-tests.patch"
        ).read_text(encoding="utf-8")
        headers = [
            line for line in payload.splitlines() if line.startswith("diff --git a/")
        ]
        index_lines = [
            line for line in payload.splitlines() if line.startswith("index ")
        ]

        self.assertEqual(
            [
                "diff --git a/components/autofill/core/browser/metrics/"
                "autofill_metrics_test_base.cc b/components/autofill/core/browser/"
                "metrics/autofill_metrics_test_base.cc",
                "diff --git a/components/input/web_input_event_builders_mac_unittest.mm "
                "b/components/input/web_input_event_builders_mac_unittest.mm",
            ],
            headers,
        )
        self.assertEqual(2, len(index_lines))
        self.assertTrue(
            all(
                [40, 40]
                == [
                    len(object_id)
                    for object_id in line.split()[1].split("..", 1)
                ]
                for line in index_lines
            )
        )
        self.assertIn(
            'base::Time::FromUTCString("2020-01-01T00:00:00Z", &year2020)',
            payload,
        )
        self.assertEqual(
            3,
            payload.count(
                "ui::ScopedKeyboardLayout keyboard_layout("
                "ui::KEYBOARD_LAYOUT_ENGLISH_US);"
            ),
        )
        self.assertNotIn("new file mode", payload)
        self.assertNotIn("deleted file mode", payload)
        self.assertNotIn("rename from", payload)
        self.assertNotIn("rename to", payload)

    def test_upstream_site_data_clock_revert_has_one_exact_path(self):
        payload = (
            PATCH_ROOT / "0020-ahoi-upstream-site-data-clock-revert.patch"
        ).read_text(encoding="utf-8")
        headers = [
            line for line in payload.splitlines() if line.startswith("diff --git a/")
        ]
        index_lines = [
            line for line in payload.splitlines() if line.startswith("index ")
        ]

        self.assertEqual(
            [
                "diff --git a/components/performance_manager/persistence/"
                "site_data/site_data_impl.cc b/components/performance_manager/"
                "persistence/site_data/site_data_impl.cc"
            ],
            headers,
        )
        self.assertEqual(1, len(index_lines))
        self.assertEqual(
            [40, 40],
            [
                len(object_id)
                for object_id in index_lines[0].split()[1].split("..", 1)
            ],
        )
        self.assertIn(
            "+  return base::Time::Now() - base::Time::UnixEpoch();",
            payload,
        )
        self.assertNotIn("new file mode", payload)
        self.assertNotIn("deleted file mode", payload)
        self.assertNotIn("rename from", payload)
        self.assertNotIn("rename to", payload)

    def test_upstream_page_load_tracing_fix_has_three_exact_paths(self):
        payload = (
            PATCH_ROOT
            / "0021-ahoi-upstream-page-load-tracing-test-isolation.patch"
        ).read_text(encoding="utf-8")
        headers = [
            line for line in payload.splitlines() if line.startswith("diff --git a/")
        ]
        index_lines = [
            line for line in payload.splitlines() if line.startswith("index ")
        ]

        self.assertEqual(
            [
                "diff --git a/components/page_load_metrics/browser/observers/core/"
                "uma_page_load_metrics_observer_unittest.cc b/components/"
                "page_load_metrics/browser/observers/core/"
                "uma_page_load_metrics_observer_unittest.cc",
                "diff --git a/content/public/browser/tracing_support.cc "
                "b/content/public/browser/tracing_support.cc",
                "diff --git a/content/public/browser/tracing_support.h "
                "b/content/public/browser/tracing_support.h",
            ],
            headers,
        )
        self.assertEqual(3, len(index_lines))
        self.assertTrue(
            all(
                [40, 40]
                == [
                    len(object_id)
                    for object_id in line.split()[1].split("..", 1)
                ]
                for line in index_lines
            )
        )
        self.assertIn(
            "+    content::ResetWebContentsListTrackRegistrationForTesting();",
            payload,
        )
        self.assertIn(
            "GetWebContentsListTrackRegistrationStorage().reset();",
            payload,
        )
        self.assertNotIn("new file mode", payload)
        self.assertNotIn("deleted file mode", payload)
        self.assertNotIn("rename from", payload)
        self.assertNotIn("rename to", payload)

    def test_upstream_backports_have_exact_chromium_provenance(self):
        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")

        self.assertIn(
            "7de113b6a9ebac43911cb99dbc93fba1acbe3c2d",
            ledger,
        )
        self.assertIn(
            "https://chromium-review.googlesource.com/c/chromium/src/+/8126355",
            ledger,
        )
        self.assertIn(
            "2e1143e225f92ded424380beaa9aa77df332b93a",
            ledger,
        )
        self.assertIn(
            "https://chromium-review.googlesource.com/c/chromium/src/+/8160156",
            ledger,
        )


if __name__ == "__main__":
    unittest.main()
