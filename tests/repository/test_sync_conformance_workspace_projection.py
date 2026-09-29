import copy
import json
import pathlib
import sys
import unittest
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))
import generate_workspace_projection_vectors as gen
import merge_model as model


def fixture():
    return json.loads(gen.OUTPUT.read_text())


def by_name():
    return {case["name"]: case for case in fixture()["cases"]}


class ProjectionFixtureTest(unittest.TestCase):
    def validate_payload(self, row, entity):
        self.assertEqual(str(uuid.UUID(row["id"])), row["id"])
        groups = gen.merge_vectors.contract()[entity][1]
        model.check_complete(row, groups)
        self.assertIs(type(row["tombstone"]), bool)
        self.assertGreater(len(row["sort_key"]), 0)
        self.assertLessEqual(len(row["sort_key"]), 1024)
        for field in ("created_at", "modified_at", "version_physical"):
            self.assertIsInstance(row[field], str)
            self.assertTrue(row[field].isdigit())
        if entity == 1:
            self.assertIs(type(row["archive_policy"]), int)
            if "merged_into" in row:
                self.assertIs(row["tombstone"], True)
                self.assertNotEqual(row["merged_into"], row["id"])
                self.assertEqual(str(uuid.UUID(row["merged_into"])), row["merged_into"])
        else:
            self.assertEqual(str(uuid.UUID(row["workspace_id"])), row["workspace_id"])
            if "parent_id" in row:
                self.assertEqual(str(uuid.UUID(row["parent_id"])), row["parent_id"])
                self.assertNotEqual(row["id"], row["parent_id"])
            if row["node_kind"] == 0:
                self.assertEqual(row["url"], "")
                self.assertNotIn("target_kind", row)
                self.assertIs(row["is_temporary"], False)
            else:
                self.assertEqual(row["node_kind"], 1)
                self.assertEqual(row["target_kind"], 0)
                self.assertTrue(row["url"].startswith("https://example.com/"))

    def test_generated_fixture_and_owner_mirror_are_current(self):
        self.assertEqual(gen.main(["--check"]), 0)
        mirror = ROOT / ("handoffs/crest-hardening/112-workspace-merge-projection-fixture/files/"
                         "overlay/chromium/src/ahoi/browser/sync/testdata/"
                         "workspace_merge_projection_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())

    def test_all_raw_records_have_complete_valid_wire_shapes(self):
        for case in fixture()["cases"]:
            for frame in case["frames"]:
                for key, entity in (("workspaces", 1), ("nodes", 2)):
                    rows = frame[key]
                    self.assertEqual(len(rows), len({row["id"] for row in rows}))
                    for row in rows:
                        with self.subTest(case=case["name"], frame=frame["name"], record=row["id"]):
                            self.validate_payload(row, entity)

    def test_input_guard_distinguishes_a_live_merge_target_and_future_field_clock(self):
        frame = by_name()["single_late_root"]["frames"][0]
        workspace = copy.deepcopy(frame["workspaces"][0])
        workspace["tombstone"] = False
        with self.assertRaises(AssertionError):
            self.validate_payload(workspace, 1)
        node = copy.deepcopy(frame["nodes"][0])
        node["field_versions"]["location"]["physical"] = str(gen.T3)
        with self.assertRaises(model.Invalid):
            self.validate_payload(node, 2)

    def test_expected_live_graph_and_order_cover_every_constrained_node_once(self):
        for case in fixture()["cases"]:
            for frame in case["frames"]:
                expected = frame["expect"]
                nodes = {row["id"]: row for row in frame["nodes"]}
                workspaces = {row["id"]: row for row in frame["workspaces"]}
                effective = {row["id"]: row for row in expected["effectiveNodes"]}
                unconstrained = set(expected["unconstrainedNodeIds"])
                deleted = set(expected["notLiveNodeIds"])
                self.assertFalse(set(effective) & unconstrained)
                self.assertFalse((set(effective) | unconstrained) & deleted)
                self.assertEqual(set(nodes), set(effective) | unconstrained | deleted)
                seen = []
                for group in expected["siblingOrder"]:
                    for identity in group["nodeIds"]:
                        self.assertEqual(effective[identity]["workspaceId"], group["workspaceId"])
                        self.assertEqual(effective[identity]["parentId"], group["parentId"])
                        seen.append(identity)
                self.assertCountEqual(seen, list(effective))
                for identity, item in effective.items():
                    self.assertFalse(nodes[identity]["tombstone"])
                    self.assertFalse(workspaces[item["workspaceId"]]["tombstone"])
                    if item["parentId"]:
                        parent = effective[item["parentId"]]
                        self.assertEqual(parent["workspaceId"], item["workspaceId"])
                        self.assertEqual(nodes[parent["id"]]["node_kind"], 0)

    def test_root_end_expectation_exposes_the_current_raw_sort_key_bug(self):
        frame = by_name()["single_late_root"]["frames"][0]
        raw_order = [node["id"] for node in sorted(frame["nodes"],
                    key=lambda row: (row["sort_key"], row["id"]))]
        self.assertEqual(raw_order, [gen.X, gen.KEEP])
        self.assertEqual(frame["expect"]["siblingOrder"][0]["nodeIds"], [gen.KEEP, gen.X])
        self.assertEqual(by_name()["late_roots_equal_keys"]["frames"][0]["expect"]["siblingOrder"][0]["nodeIds"],
                         [gen.KEEP, gen.X, gen.Y])

    def test_all_four_resolved_invalid_parents_are_root_without_recovery(self):
        for suffix in ("missing", "deleted", "cross_workspace", "nonfolder"):
            expected = by_name()["parent_" + suffix]["frames"][0]["expect"]
            node = next(row for row in expected["effectiveNodes"] if row["id"] == gen.X)
            self.assertEqual(node, {"id": gen.X, "workspaceId": gen.B, "parentId": None})
            order = next(group for group in expected["siblingOrder"]
                         if group["workspaceId"] == gen.B and group["parentId"] is None)
            self.assertEqual(order["nodeIds"][-1], gen.X)

    def test_valid_rehomed_and_already_moved_parents_remain(self):
        for case in ("late_subtree", "known_moved_parent", "late_nested_subtree"):
            node = next(row for row in by_name()[case]["frames"][0]["expect"]["effectiveNodes"]
                        if row["id"] == gen.X)
            self.assertEqual(node["parentId"], gen.FOLDER)

    def test_delivery_groups_have_identical_final_raw_sets_and_expected_views(self):
        groups = {}
        for case in fixture()["cases"]:
            if "convergenceGroup" in case:
                frame = case["frames"][-1]
                actual = (frame["rawRecordSetSha256"], frame["expect"])
                self.assertEqual(groups.setdefault(case["convergenceGroup"], actual), actual)
        self.assertEqual(set(groups), {"late_delivery", "undo_and_explicit_move"})

    def test_passive_undo_preserves_page_bytes_and_explicit_move_changes_only_location(self):
        before, after = by_name()["undo_returns_passive_node"]["frames"]
        self.assertEqual(before["nodes"], after["nodes"])
        self.assertEqual(after["unchangedNodeIdsFromPreviousFrame"], [gen.KEEP, gen.X])
        before, moved, after = by_name()["explicit_move_before_undo"]["frames"]
        old = next(row for row in before["nodes"] if row["id"] == gen.X)
        new = next(row for row in moved["nodes"] if row["id"] == gen.X)
        changed_groups = {key for key, value in old["field_versions"].items()
                          if value != new["field_versions"][key]}
        self.assertEqual(changed_groups, {"location", "modified_at"})
        self.assertGreater(model.Stamp.from_field(new["field_versions"]["location"]),
                           model.Stamp.from_field(old["field_versions"]["location"]))
        self.assertEqual(new["parent_id"], gen.FOLDER)
        self.assertEqual(moved["nodes"], after["nodes"])
        final = {row["id"]: row["workspaceId"] for row in after["expect"]["effectiveNodes"]}
        self.assertEqual((final[gen.X], final[gen.Y]), (gen.B, gen.A))

    def test_permutations_share_one_raw_fingerprint_and_preservation_contract(self):
        for case in fixture()["cases"]:
            for frame in case["frames"]:
                ws, ns = frame["workspaces"], frame["nodes"]
                variants = [(workspaces, nodes)
                            for workspaces in (ws, list(reversed(ws)), ws[1:] + ws[:1])
                            for nodes in (ns, list(reversed(ns)), ns[1:] + ns[:1])]
                for workspaces, nodes in variants:
                    self.assertEqual(gen.raw_set_hash(workspaces, nodes), frame["rawRecordSetSha256"])
                raw = frame["expect"]["preserveRaw"]
                self.assertEqual(raw["nodeIds"], sorted(row["id"] for row in ns))
                self.assertEqual(raw["workspaceIds"], sorted(row["id"] for row in ws))
                self.assertEqual(raw["serializedBytes"], "before-equals-after-with-same-codec")
                self.assertEqual(raw["fieldVersions"], "exact")

    def test_unresolved_classification_does_not_pin_platform_recovery(self):
        required = {"missing_merge_target": "missing-target", "merge_cycle": "cycle",
                    "ordinary_deleted_target": "deleted-target", "ordinary_deleted_source": "ordinary-deletion",
                    "missing_source_workspace": "missing-source"}
        for name, classification in required.items():
            expected = by_name()[name]["frames"][0]["expect"]
            route = next(row for row in expected["workspaceRoutes"] if row["workspaceId"] == gen.A)
            self.assertEqual(route, {"workspaceId": gen.A, "classification": classification})
            self.assertEqual(expected["unconstrainedNodeIds"], [gen.X])
            self.assertNotIn(gen.X, {row["id"] for row in expected["effectiveNodes"]})


if __name__ == "__main__":
    unittest.main()
