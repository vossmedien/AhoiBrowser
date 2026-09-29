import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_product_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402


class ProductMergeVectorsTest(unittest.TestCase):
    def cases(self):
        return json.loads(gen.OUTPUT.read_text())["cases"]

    def test_generated_fixture_is_current_and_covers_both_orderings(self):
        self.assertEqual(gen.main(["--check"]), 0)
        mirror = ROOT / ("handoffs/crest-hardening/128-inventory-asset-field-merge/files/"
                         "overlay/chromium/src/ahoi/browser/sync/testdata/merge_inventory_asset_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())
        self.assertEqual(len(self.cases()), 6)
        self.assertEqual({c["entityType"] for c in self.cases()}, {9, 10})
        self.assertEqual({c["name"].split(".")[1] for c in self.cases()},
                         {"disjoint_name_then_enabled", "disjoint_enabled_then_name",
                          "equal_clock_name_conflict"})

    def test_disjoint_fields_require_union_and_successor_clock(self):
        for case in self.cases():
            if "disjoint_" not in case["name"]:
                continue
            with self.subTest(name=case["name"]):
                expected = case["expect"]
                self.assertEqual(expected["decision"], "mergeFields")
                self.assertEqual(expected["merged"]["enabled"], False)
                self.assertIn(expected["merged"]["name"],
                              {"Renamed extension", "Renamed CSS asset"})
                self.assertEqual(expected["merged"]["version_physical"], str(gen.gen.T2))
                self.assertEqual(expected["merged"]["version_logical"], 1)
                # Whole-record last-writer selection drops the older name.
                old, incoming = case["existing"], case["incoming"]
                selected = max((old, incoming), key=model.Stamp.of_record)
                self.assertNotEqual(selected["name"], expected["merged"]["name"])

    def test_equal_clock_conflicts_and_both_inputs_decode_validly(self):
        contract = gen.gen.contract()
        for case in self.cases():
            with self.subTest(name=case["name"]):
                self.assertIs(case["inputValid"], True)
                groups = contract[case["entityType"]][1]
                for side in ("existing", "incoming"):
                    model.check_complete(case[side], groups)
                if case["name"].endswith("equal_clock_name_conflict"):
                    self.assertEqual(case["expect"], {"decision": "invalid", "merged": None})


if __name__ == "__main__":
    unittest.main()
