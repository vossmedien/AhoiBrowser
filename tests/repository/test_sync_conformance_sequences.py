import copy
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import compare_runner_outputs as comparator  # noqa: E402
import generate_merge_sequences as gen  # noqa: E402
import merge_model as model  # noqa: E402


class MergeSequencesTest(unittest.TestCase):
    def document(self):
        return json.loads(gen.OUTPUT.read_text())

    def test_seed_and_mirror_are_stable(self):
        self.assertEqual(gen.main(["--check"]), 0)
        mirror = ROOT / ("handoffs/crest-hardening/132-seeded-merge-sequences/files/"
                         "overlay/chromium/src/ahoi/browser/sync/testdata/merge_sequences_v3.json")
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())
        self.assertEqual(gen.generate(), gen.generate(gen.SEED))
        self.assertNotEqual(gen.generate(), gen.generate(gen.SEED + 1))
        self.assertEqual(len(comparator.validate_fixture(self.document())), 18)

    def test_actual_reference_state_flows_through_every_step(self):
        document = self.document()
        self.assertEqual(sum(len(c["steps"]) for c in document["cases"]), 104)
        decisions = set()
        for case in document["cases"]:
            with self.subTest(name=case["name"]):
                entity = case["entityType"]
                groups = gen.gen.contract()[entity][1]
                current = copy.deepcopy(case["initial"])
                model.check_complete(current, groups)
                self.assertTrue(case["steps"])
                for step in case["steps"]:
                    self.assertIs(step["inputValid"], True)
                    self.assertEqual(step["writerDevice"], step["incoming"]["version_device"])
                    model.check_complete(step["incoming"], groups)
                    before = copy.deepcopy(current)
                    decision, merged = model.merge(entity, groups, current, step["incoming"])
                    self.assertEqual((decision, merged),
                                     (step["expect"]["decision"], step["expect"]["merged"]))
                    decisions.add(decision)
                    if decision == "invalid":
                        self.assertEqual(current, before)
                    else:
                        current = merged
                self.assertEqual(current, case["expect"]["merged"])
                self.assertEqual(case["expect"]["decision"],
                                 case["steps"][-1]["expect"]["decision"])
        self.assertEqual(decisions, {"acceptIncoming", "mergeFields", "keepExisting", "invalid"})

    def test_fixed_terminal_and_undo_sequences_have_the_expected_end_state(self):
        cases = {c["name"]: c for c in self.document()["cases"]}
        workspace = cases["sequence.workspace.merge_rename_conflict_undo"]
        self.assertEqual(workspace["steps"][2]["expect"]["decision"], "invalid")
        self.assertEqual(workspace["expect"]["merged"]["name"], "Late rename")
        self.assertIs(workspace["expect"]["merged"]["tombstone"], False)
        self.assertNotIn("merged_into", workspace["expect"]["merged"])
        command = cases["sequence.remoteCommand.terminal_status"]
        self.assertEqual(command["expect"]["merged"]["status"], 2)
        self.assertEqual(command["steps"][1]["expect"]["decision"], "keepExisting")
        archive = cases["sequence.tabArchiveEntry.terminal_delete"]
        self.assertIs(archive["expect"]["merged"]["tombstone"], True)
        self.assertEqual(archive["steps"][2]["expect"]["decision"], "keepExisting")

    def test_sequence_counterexample_is_not_just_a_last_pair(self):
        case = self.document()["cases"][0]
        expected = case["expect"]["merged"]
        last = case["steps"][-1]["incoming"]
        self.assertNotEqual(last, expected)
        # If a runner substitutes the fixture oracle for the actual prior
        # result, the step-by-step checks cannot validate path dependence.
        self.assertTrue(any(step["expect"]["decision"] == "invalid"
                            for step in case["steps"]))


if __name__ == "__main__":
    unittest.main()
