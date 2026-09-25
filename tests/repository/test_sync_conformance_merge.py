import filecmp
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_merge_vectors as gen  # noqa: E402
import merge_model as m  # noqa: E402

VECTORS = ROOT / "fixtures/sync-conformance/merge_v3.json"
CPP_COPY = (ROOT / "handoffs/crest-hardening/009-sync-merge-conformance/files/overlay/"
            "chromium/src/ahoi/browser/sync/testdata/merge_v3.json")


def cases():
    return {c["name"]: c for c in json.loads(VECTORS.read_text())["cases"]}


class MergeVectorFileTest(unittest.TestCase):
    def test_committed_vectors_are_current(self):
        self.assertEqual(gen.main(["--check"]), 0)

    def test_handoff_copy_matches(self):
        if CPP_COPY.exists():
            self.assertTrue(filecmp.cmp(VECTORS, CPP_COPY, shallow=False))

    def test_every_decision_occurs(self):
        decisions = {c["expect"]["decision"] for c in cases().values()}
        self.assertEqual(decisions, {"duplicate", "keepExisting", "acceptIncoming",
                                     "mergeFields", "invalid"})

    def test_inputs_are_complete_except_the_incomplete_cases(self):
        groups = {entity: g for entity, (_, g) in gen.contract().items()}
        for name, case in cases().items():
            if name.endswith("incomplete_field_map"):
                continue
            for side in ("existing", "incoming"):
                self.assertEqual(set(case[side]["field_versions"]),
                                 set(groups[case["entityType"]]), f"{name} {side}")


class MergeModelSemanticsTest(unittest.TestCase):
    """Hand-derived expectations from sync_field_merge.cc, independent of the model."""

    def expect(self, name, decision):
        self.assertEqual(cases()[name]["expect"]["decision"], decision, name)
        return cases()[name]["expect"]["merged"]

    def test_generic_decisions(self):
        for entity in ("workspace", "tree_folder", "device_session", "appearance_custom_accent",
                       "permitted_setting_glass_enabled", "archive_single_page",
                       "remote_command_open_shape_only"):
            self.expect(f"{entity}.identical", "duplicate")
            self.expect(f"{entity}.incoming_newer", "acceptIncoming")
            self.expect(f"{entity}.incoming_older", "keepExisting")
            self.expect(f"{entity}.disjoint_union", "mergeFields")
            self.expect(f"{entity}.equal_clock_conflict", "invalid")
            self.expect(f"{entity}.incomplete_field_map", "invalid")
            self.expect(f"{entity}.field_clock_after_record_clock", "invalid")

    def test_larger_device_id_wins_a_tie(self):
        merged = self.expect("workspace.device_tiebreak", "acceptIncoming")
        self.assertEqual(merged["name"], "Renamed workspace (other)")
        self.assertEqual(merged["field_versions"]["name"]["device"], gen.PHONE)

    def test_union_takes_both_changes_and_a_successor_clock(self):
        merged = self.expect("workspace.disjoint_union", "mergeFields")
        self.assertEqual((merged["name"], merged["icon"]), ("Renamed workspace", "star"))
        self.assertEqual((int(merged["version_physical"]), merged["version_logical"],
                          merged["version_device"]), (gen.T2, 1, gen.PHONE))

    def test_successor_carries_over_a_full_logical_counter(self):
        merged = self.expect("workspace.logical_overflow_successor", "mergeFields")
        self.assertEqual((int(merged["version_physical"]), merged["version_logical"]),
                         (gen.T1 + 1, 0))

    def test_keep_existing_still_advances_the_record_clock(self):
        merged = self.expect("workspace.incoming_older", "keepExisting")
        self.assertEqual(merged["name"], "Renamed workspace")
        self.assertEqual(int(merged["version_physical"]), gen.T2)

    def test_location_moves_atomically(self):
        merged = self.expect("tree_folder.move_is_atomic", "acceptIncoming")
        self.assertEqual((merged["parent_id"], merged["sort_key"]),
                         ("a2000000-0000-4000-8000-0000000000ff", "D"))

    def test_immutable_groups_reject(self):
        for entity in ("workspace", "tree_folder", "device_session",
                       "remote_command_open_shape_only", "permitted_setting_glass_enabled"):
            self.expect(f"{entity}.immutable_change", "invalid")

    def test_command_status_is_monotonic_and_terminal(self):
        kept = self.expect("remote_command_open_shape_only.terminal_status_kept", "keepExisting")
        self.assertEqual(kept["status"], 2)
        self.expect("remote_command_open_shape_only.status_never_regresses", "keepExisting")

    def test_archive_deletion_is_terminal_but_state_merges(self):
        merged = self.expect("archive_single_page.archive_deletion_is_terminal", "mergeFields")
        self.assertIs(merged["tombstone"], True)
        self.assertIs(merged["restored"], True)

    def test_absent_optional_key_equals_null(self):
        base = gen.fixture_payloads()["workspace"]
        with_null = dict(base, accent_argb=None)
        groups = gen.contract()[1][1]
        self.assertEqual(m.merge(1, groups, base, with_null)[0], "duplicate")


class MergeConvergenceTest(unittest.TestCase):
    """Order independence: both devices must reach the same state."""

    def test_random_pairs_converge_in_either_order(self):
        groups = {entity: g for entity, (_, g) in gen.contract().items()}
        checked = 0
        for case in gen.random_cases(seed=7, count=300):
            entity = case["entityType"]
            forward = m.merge(entity, groups[entity], case["existing"], case["incoming"])
            backward = m.merge(entity, groups[entity], case["incoming"], case["existing"])
            self.assertEqual(forward[0] == "invalid", backward[0] == "invalid", case["name"])
            if forward[0] == "invalid":
                continue
            checked += 1
            self.assertEqual(m.projected(entity, forward[1], groups[entity]),
                             m.projected(entity, backward[1], groups[entity]), case["name"])
            self.assertEqual(m.Stamp.of_record(forward[1]), m.Stamp.of_record(backward[1]))
        self.assertGreater(checked, 200)

    def test_merge_is_idempotent(self):
        groups = {entity: g for entity, (_, g) in gen.contract().items()}
        for case in gen.random_cases(seed=11, count=100):
            entity = case["entityType"]
            decision, merged = m.merge(entity, groups[entity], case["existing"], case["incoming"])
            if decision == "invalid":
                continue
            again = m.merge(entity, groups[entity], merged, case["incoming"])
            self.assertIn(again[0], {"duplicate", "keepExisting"}, case["name"])


if __name__ == "__main__":
    unittest.main()
