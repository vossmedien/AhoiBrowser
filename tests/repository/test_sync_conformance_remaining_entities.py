import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_remaining_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402


class RemainingMergeVectorsTest(unittest.TestCase):
    def cases(self):
        return json.loads(gen.OUTPUT.read_text())["cases"]

    def test_all_six_entities_have_the_same_six_scenarios(self):
        self.assertEqual(gen.main(["--check"]), 0)
        cases = self.cases()
        self.assertEqual(len(cases), 36)
        self.assertEqual({c["entityType"] for c in cases}, {0, 3, 4, 11, 12, 13})
        scenarios = {"identical", "newer", "delayed_older_group", "disjoint",
                     "disjoint_reversed", "equal_clock_conflict"}
        for entity in (0, 3, 4, 11, 12, 13):
            self.assertEqual({c["name"].split(".")[1] for c in cases
                              if c["entityType"] == entity}, scenarios)

    def test_valid_input_maps_and_hand_derived_union_decisions(self):
        records = gen.gen.contract()
        for case in self.cases():
            with self.subTest(name=case["name"]):
                self.assertIs(case["inputValid"], True)
                groups = records[case["entityType"]][1]
                for side in ("existing", "incoming"):
                    model.check_complete(case[side], groups)
                suffix = case["name"].split(".")[1]
                expected = case["expect"]
                if suffix == "identical":
                    self.assertEqual(expected["decision"], "duplicate")
                elif suffix == "newer":
                    self.assertEqual(expected["decision"], "acceptIncoming")
                elif suffix == "equal_clock_conflict":
                    self.assertEqual(expected, {"decision": "invalid", "merged": None})
                else:
                    self.assertEqual(expected["decision"], "mergeFields")
                    self.assertEqual(expected["merged"]["version_logical"], 1)
                    self.assertEqual(expected["merged"]["version_physical"], str(gen.gen.T2))

    def test_shape_sensitive_capability_split_bookmark_and_presence_inputs(self):
        for case in self.cases():
            entity = case["entityType"]
            for side in ("existing", "incoming"):
                payload = case[side]
                if entity == 12:
                    self.assertEqual(payload["version_device"], payload["device_id"])
                    self.assertEqual({stamp["device"] for stamp in
                                      payload["field_versions"].values()}, {payload["device_id"]})
                    self.assertEqual(payload["features"], sorted(set(payload["features"])))
                elif entity == 13:
                    self.assertEqual(len(payload["topology"]["member_ids"]), 2)
                    self.assertIn(payload["topology"]["axis"], (0, 1))
                    self.assertLessEqual(payload["ratios"]["primary"], 1_000_000)
                elif entity == 11:
                    self.assertEqual(("root_kind" in payload) + ("parent_id" in payload), 1)
                    self.assertEqual(payload["kind"], 1)
                elif entity == 4:
                    self.assertIs(payload["is_incognito"], False)
                    self.assertEqual(payload["target_kind"], 0)
                    self.assertTrue(payload["tree_node_id"])

    def test_owner_testdata_mirror_is_byte_identical(self):
        mirror = ROOT / ("handoffs/crest-hardening/130-remaining-entity-merge-vectors/files/"
                         "overlay/chromium/src/ahoi/browser/sync/testdata/merge_remaining_entities_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())


if __name__ == "__main__":
    unittest.main()
