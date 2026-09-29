import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_domain_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402

HANDOFF = ROOT / "handoffs/crest-hardening/140-domain-group-merge-vectors"
HOME_KEYS = ("home_url", "home_target_kind", "home_local_scheme")
TARGET_KEYS = ("url", "target_kind", "local_scheme")

# Decisions derived by hand from sync_field_merge.cc and ValidateRecord, not
# from merge_model: a newer group wins, an older one loses, equal clocks with
# different values and a union breaking a cross-group invariant are invalid.
EXPECTED = {
    "workspace.archive_policy_newer": "acceptIncoming",
    "workspace.archive_policy_rename_union": "mergeFields",
    "workspace.archive_policy_equal_clock_conflict": "invalid",
    "workspace.accent_icon_union": "mergeFields",
    "workspace.accent_removal_newer": "acceptIncoming",
    "workspace.modified_at_follows_newer_edit": "mergeFields",
    "tree.home_target_newer_atomic": "acceptIncoming",
    "tree.home_target_title_union": "mergeFields",
    "tree.home_target_newer_clear": "acceptIncoming",
    "tree.home_target_stale_clear_loses": "keepExisting",
    "tree.home_target_equal_clock_conflict": "invalid",
    "tree.dormant_home_on_temporary_union": "mergeFields",
    "tree.page_target_newer_atomic": "acceptIncoming",
    "tree.page_target_title_union": "mergeFields",
    "tree.page_target_equal_clock_conflict": "invalid",
    "tree.temporary_page_saved_newer": "acceptIncoming",
    "tree.new_tab_union_requires_temporary": "invalid",
    "tree.accent_icon_union": "mergeFields",
    "setting.extension_desired_newer_disable": "acceptIncoming",
    "setting.extension_desired_older_uninstall_loses": "keepExisting",
    "setting.extension_desired_equal_clock_conflict": "invalid",
    "setting.extension_storage_newer_reset": "acceptIncoming",
    "setting.extension_storage_stale_reset_loses": "keepExisting",
    "setting.extension_storage_equal_clock_conflict": "invalid",
}


def cases():
    return {c["name"]: c for c in json.loads(gen.OUTPUT.read_text())["cases"]}


def contract():
    return json.loads((ROOT / "config/sync-format.json").read_text())


class DomainGroupVectorsTest(unittest.TestCase):
    def test_generated_file_is_current_and_complete(self):
        self.assertEqual(gen.main(["--check"]), 0)
        self.assertEqual({n: c["expect"]["decision"] for n, c in cases().items()}, EXPECTED)

    def test_inputs_are_complete_and_contract_valid(self):
        structure = contract()["workspaceStructure"]
        setup = contract()["browserSetupSettings"]
        kinds = contract()["sharedTargets"]["kinds"]
        schemes = set(contract()["sharedTargets"]["localSchemes"])
        records = gen.gen.contract()
        for name, case in cases().items():
            groups = records[case["entityType"]][1]
            for side in ("existing", "incoming"):
                with self.subTest(name=name, side=side):
                    payload = case[side]
                    self.assertIs(case["inputValid"], True)
                    model.check_complete(payload, groups)
                    self.assertTrue(model.union_valid(case["entityType"], payload))
                    if case["entityType"] == 1:
                        self.assertIn(payload["archive_policy"],
                                      structure["archive"]["policy"].values())
                        self.assertTrue(payload["modified_at"].isdigit())
                    elif case["entityType"] == 2:
                        self.assertEqual(payload["node_kind"], 1)
                        for key in HOME_KEYS:  # three mandatory keys, null when absent
                            self.assertIn(key, payload)
                        home = tuple(payload[k] for k in HOME_KEYS)
                        if home != (None, None, None):
                            self.assertIn(home[1], structure["home"]["allowedKinds"])
                        kind = payload["target_kind"]
                        self.assertIn(kind, kinds.values())
                        if kind == kinds["localOnly"]:
                            self.assertEqual(payload["url"], "")
                            self.assertIn(payload["local_scheme"], schemes)
                        else:
                            self.assertNotIn("local_scheme", payload)
                        if kind == kinds["newTab"]:
                            self.assertIs(payload["is_temporary"], True)
                    elif case["entityType"] == 8:
                        value = json.loads(payload["value_json"])
                        self.assertIs(payload["tombstone"], False)
                        if payload["setting_id"].endswith(".desired"):
                            self.assertEqual(set(value), set(setup["extensionDesired"]["valueFields"]))
                            self.assertIn(value["source"], setup["extensionDesired"]["sources"])
                            if not value["installed"]:
                                self.assertIs(value["enabled"], False)
                        else:
                            self.assertIn(value, (True, False, None))

    def test_multi_key_groups_move_atomically(self):
        for name, case in cases().items():
            merged = case["expect"]["merged"]
            if merged is None or case["entityType"] != 2:
                continue
            for keys, group in ((HOME_KEYS, "home_target"), (TARGET_KEYS, "url")):
                winner = next(side for side in ("existing", "incoming")
                              if case[side]["field_versions"][group] ==
                              merged["field_versions"][group])
                with self.subTest(name=name, group=group):
                    self.assertEqual([merged.get(k) for k in keys],
                                     [case[winner].get(k) for k in keys])

    def test_named_outcomes(self):
        c = cases()
        dormant = c["tree.dormant_home_on_temporary_union"]["expect"]["merged"]
        self.assertIs(dormant["is_temporary"], True)
        self.assertEqual(dormant["home_url"], "https://example.com/home")
        self.assertNotIn("accent_argb", c["workspace.accent_removal_newer"]["expect"]["merged"])
        modified = c["workspace.modified_at_follows_newer_edit"]["expect"]["merged"]
        self.assertEqual((modified["name"], modified["archive_policy"], modified["modified_at"]),
                         ("Renamed workspace", 2, str(gen.gen.T2)))
        self.assertEqual(c["setting.extension_storage_newer_reset"]["expect"]["merged"]["value_json"],
                         "null")
        for name, case in c.items():
            if case["expect"]["decision"] == "mergeFields":
                self.assertEqual(case["expect"]["merged"]["version_logical"], 1, name)
                self.assertEqual(case["expect"]["merged"]["version_physical"], str(gen.gen.T2), name)

    def test_new_tab_union_is_rejected_although_each_side_is_valid(self):
        case = cases()["tree.new_tab_union_requires_temporary"]
        self.assertEqual((case["existing"]["target_kind"], case["existing"]["is_temporary"]),
                         (1, True))
        self.assertEqual((case["incoming"]["target_kind"], case["incoming"]["is_temporary"]),
                         (0, False))
        self.assertEqual(case["expect"], {"decision": "invalid", "merged": None})

    def test_owner_testdata_mirror_and_patch(self):
        mirror = HANDOFF / "files/overlay/chromium/src/ahoi/browser/sync/testdata/merge_domain_groups_v3.json"
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())
        patch = (HANDOFF / "140-domain-group-merge-vectors.patch").read_text()
        self.assertIn('CheckVectors("merge_domain_groups_v3.json")', patch)
        self.assertIn('try checkVectors("merge_domain_groups_v3.json")', patch)
        self.assertIn('"testdata/merge_domain_groups_v3.json"', patch)


if __name__ == "__main__":
    unittest.main()
