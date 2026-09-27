import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_marker_collision_vectors as gen  # noqa: E402
import generate_workspace_projection_vectors as projection  # noqa: E402
import merge_model  # noqa: E402


class MarkerCollisionFixtureTest(unittest.TestCase):
    def document(self):
        return json.loads(gen.OUTPUT.read_text())

    def test_fixture_and_owner_copy_are_current(self):
        self.assertEqual(gen.main(["--check"]), 0)
        mirror = ROOT / ("handoffs/crest-hardening/138-marker-collision-conformance/files/"
                         "overlay/chromium/src/ahoi/browser/sync/testdata/"
                         "workspace_merge_marker_collision_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())
        self.assertEqual(len(self.document()["cases"]), 2)

    def test_ordinary_key_is_opaque_even_when_it_contains_marker_bytes(self):
        for case in self.document()["cases"]:
            frame = case["frames"][0]
            with self.subTest(name=case["name"]):
                self.assertEqual(frame["rawRecordSetSha256"],
                                 projection.raw_set_hash(frame["workspaces"], frame["nodes"]))
                for workspace in frame["workspaces"]:
                    merge_model.check_complete(workspace, gen.merge_vectors.contract()[1][1])
                for node in frame["nodes"]:
                    merge_model.check_complete(node, gen.merge_vectors.contract()[2][1])
                q = next(row for row in frame["nodes"] if row["id"] == projection.KEEP)
                z = next(row for row in frame["nodes"] if row["id"] == projection.Y)
                marker = "!:ahoi-merge-root/" + projection.B + "/"
                self.assertEqual(q["sort_key"], "Q" + marker + "x")
                self.assertLess(q["sort_key"].encode(), z["sort_key"].encode())
                self.assertEqual(frame["expect"]["siblingOrder"][0]["nodeIds"][:2],
                                 [projection.KEEP, projection.Y])
                self.assertEqual(frame["expect"]["preserveRaw"]["fieldVersions"], "exact")

    def test_real_late_node_is_appended_without_reclassifying_q(self):
        cases = {case["name"]: case for case in self.document()["cases"]}
        first = cases["ordinary_key_without_merge"]["frames"][0]
        second = cases["ordinary_key_with_real_late_node"]["frames"][0]
        self.assertEqual(len(first["workspaces"]), 1)
        self.assertEqual(first["expect"]["siblingOrder"][0]["nodeIds"],
                         [projection.KEEP, projection.Y])
        self.assertEqual(second["expect"]["siblingOrder"][0]["nodeIds"],
                         [projection.KEEP, projection.Y, projection.X])
        self.assertEqual(second["expect"]["workspaceRoutes"][0]["classification"],
                         "resolved-merge")
        # The raw Q/Z records are identical in both frames; only the unrelated
        # merge and late X arrive. Passive projection may not rewrite Q.
        by_id = lambda frame: {row["id"]: row for row in frame["nodes"]}
        self.assertEqual(by_id(first)[projection.KEEP], by_id(second)[projection.KEEP])
        self.assertEqual(by_id(first)[projection.Y], by_id(second)[projection.Y])


if __name__ == "__main__":
    unittest.main()
