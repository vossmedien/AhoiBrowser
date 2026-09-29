import json
import pathlib
import sys
import unicodedata
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))
import generate_utf8_sort_vectors as gen
import merge_model as model


class UTF8SortKeyVectorsTest(unittest.TestCase):
    def cases(self):
        return json.loads(gen.OUTPUT.read_text())["cases"]

    def test_generated_supplement_and_handoff_copy_match(self):
        self.assertEqual(gen.main(["--check"]), 0)
        mirror = ROOT / ("handoffs/crest-hardening/120-utf8-sort-key-equality/files/overlay/"
                         "chromium/src/ahoi/browser/sync/testdata/merge_utf8_sort_keys_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())

    def test_distinct_opaque_bytes_with_equal_field_clocks_must_conflict(self):
        count = 0
        for case in self.cases():
            if "equal_clock" not in case["name"]:
                continue
            old, new = case["existing"], case["incoming"]
            self.assertEqual(unicodedata.normalize("NFC", old["sort_key"]),
                             unicodedata.normalize("NFC", new["sort_key"]))
            self.assertNotEqual(old["sort_key"].encode(), new["sort_key"].encode())
            self.assertEqual(old["field_versions"], new["field_versions"])
            self.assertEqual(case["expect"], {"decision": "invalid", "merged": None})
            count += 1
        self.assertEqual(count, 4)

    def test_newer_field_clock_keeps_the_winning_original_bytes_in_both_orders(self):
        for case in self.cases():
            if "equal_clock" in case["name"]:
                continue
            self.assertEqual(case["expect"]["merged"]["sort_key"].encode(), gen.COMPOSED.encode())
            expected = "acceptIncoming" if "newer" in case["name"] else "keepExisting"
            self.assertEqual(case["expect"]["decision"], expected)

    def test_all_inputs_require_successful_wire_decode_and_complete_maps(self):
        self.assertEqual({case["entityType"] for case in self.cases()}, {1, 2})
        for case in self.cases():
            self.assertIs(case["inputValid"], True)
            groups = gen.gen.contract()[case["entityType"]][1]
            for side in ("existing", "incoming"):
                model.check_complete(case[side], groups)
                self.assertIs(case[side]["tombstone"], False)
                self.assertIn(case[side]["sort_key"], (gen.DECOMPOSED, gen.COMPOSED))


if __name__ == "__main__":
    unittest.main()
