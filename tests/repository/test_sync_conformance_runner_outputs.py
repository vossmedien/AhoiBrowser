import copy
from decimal import Decimal
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import contextlib
import io

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))
import compare_runner_outputs as compare
import record_runner_receipt as recorder


class RunnerOutputComparisonTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.fixture = self.root / "vectors.json"
        self.document = {"schemaVersion": 1, "cases": [
            {"name": "random.123.001.workspace", "entityType": 1,
             "existing": {"name": "old"}, "incoming": {"name": "new"},
             "expect": {"decision": "acceptIncoming", "merged": {"name": "new", "n": 1}}},
            {"name": "equal-clock", "entityType": 2, "inputValid": True,
             "expect": {"decision": "invalid", "merged": None}},
        ]}
        self.write(self.fixture, self.document)
        self.binary = self.root / "test-binary"
        self.binary.write_bytes(b"synthetic unit-test artifact; not native evidence")
        self.outputs = {}
        for implementation in ("cpp", "swift"):
            self.outputs[implementation] = {
                "schemaVersion": 1, "kind": "ahoi-sync-merge-output", "complete": True,
                "implementation": implementation, "runId": implementation + "-unit-test-only",
                "fixtureName": self.fixture.name,
                "fixtureSha256": compare.sha256(self.fixture.read_bytes()),
                "cases": [{"name": case["name"], "entityType": case["entityType"],
                           "outcome": "invalid" if case["expect"]["decision"] == "invalid" else "accepted",
                           "rejectionStage": "merge" if case["expect"]["decision"] == "invalid" else None,
                           "decision": case["expect"]["decision"],
                           "payload": copy.deepcopy(case["expect"]["merged"])}
                          for case in self.document["cases"]]}
        self.save()

    def write(self, path, value):
        path.write_text(compare.json_text(value))

    def save(self):
        for implementation, output in self.outputs.items():
            path = self.root / (implementation + ".json")
            self.write(path, output)
            receipt = {"schemaVersion": 1, "kind": "ahoi-sync-merge-run",
                       "implementation": implementation, "runId": output["runId"],
                       "fixtureSha256": compare.sha256(self.fixture.read_bytes()),
                       "outputSha256": compare.sha256(path.read_bytes()),
                       "completed": True, "exitCode": 0, "sourceTreeClean": True,
                       "sourceCommit": "a" * 40,
                       "binaryArtifacts": {self.binary.name: compare.sha256(self.binary.read_bytes())}}
            self.write(self.root / (implementation + "-receipt.json"), receipt)

    def result(self):
        return compare.compare(self.fixture, self.root / "cpp.json", self.root / "swift.json",
                               self.root / "cpp-receipt.json", self.root / "swift-receipt.json")

    def test_matching_complete_outputs_pass(self):
        self.assertEqual(self.result()["verdict"], "PASS")

    def test_equal_wrong_results_fail_against_fixture(self):
        for output in self.outputs.values():
            output["cases"][0]["payload"]["name"] = "same wrong result"
        self.save()
        result = self.result()
        self.assertEqual(result["verdict"], "FAIL")
        self.assertIn("swift/fixture payload $/name", result["differences"][0]["issues"])

    def test_unicode_equivalence_does_not_hide_different_bytes(self):
        self.outputs["cpp"]["cases"][0]["payload"]["name"] = "é"
        self.outputs["swift"]["cases"][0]["payload"]["name"] = "e\u0301"
        self.save()
        self.assertIn("cpp/swift payload $/name", self.result()["differences"][0]["issues"])

    def test_decode_rejection_is_not_merge_conflict_evidence(self):
        self.outputs["swift"]["cases"][1]["rejectionStage"] = "decode"
        self.save()
        self.assertEqual(self.result()["verdict"], "FAIL")

    def test_unpinned_decode_or_merge_rejection_stages_can_differ(self):
        self.document["cases"][1].pop("inputValid")
        self.write(self.fixture, self.document)
        for output in self.outputs.values():
            output["fixtureSha256"] = compare.sha256(self.fixture.read_bytes())
        self.outputs["swift"]["cases"][1]["rejectionStage"] = "decode"
        self.save()
        self.assertEqual(self.result()["verdict"], "PASS")

    def test_cpp_decision_is_actual_and_checked(self):
        self.outputs["cpp"]["cases"][0]["decision"] = "duplicate"
        self.save()
        self.assertEqual(self.result()["verdict"], "FAIL")

    def test_coverage_and_harness_failures_never_pass(self):
        original = copy.deepcopy(self.outputs["swift"])
        mutations = [
            lambda d: d["cases"].pop(),
            lambda d: d["cases"].append(copy.deepcopy(d["cases"][0])),
            lambda d: d["cases"].append(dict(d["cases"][0], name="extra")),
            lambda d: d["cases"][0].update(outcome="error"),
            lambda d: d["cases"][0].update(outcome="skipped"),
            lambda d: d["cases"][0].update(outcome=[]),
            lambda d: d["cases"][1].update(rejectionStage=[]),
            lambda d: d["cases"][0].update(entityType=True),
            lambda d: d["cases"][0].update(entityType=8),
            lambda d: d["cases"][0].update(payload=None),
            lambda d: d["cases"][0].pop("rejectionStage"),
            lambda d: d.update(complete=False),
            lambda d: d.update(complete=1),
            lambda d: d.update(schemaVersion=True),
            lambda d: d.update(fixtureName="other.json"),
            lambda d: d.update(fixtureSha256="0" * 64),
            lambda d: d.update(implementation="cpp"),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.outputs["swift"] = copy.deepcopy(original)
                mutate(self.outputs["swift"])
                self.save()
                self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_receipt_exit_source_identity_and_hashes_are_required(self):
        path = self.root / "cpp-receipt.json"
        original = json.loads(path.read_text())
        mutations = {"completed": False, "exitCode": 1, "sourceTreeClean": False,
                     "sourceCommit": "short", "binaryArtifacts": {}, "runId": "stale",
                     "fixtureSha256": "0" * 64, "outputSha256": "0" * 64}
        for key, value in mutations.items():
            with self.subTest(key=key):
                self.write(path, dict(original, **{key: value}))
                self.assertEqual(self.result()["verdict"], "INSUFFICIENT")
        self.write(path, dict(original, exitCode=False))
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_changed_binary_or_missing_artifacts_never_pass(self):
        self.binary.write_bytes(b"changed")
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")
        self.binary.unlink()
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_malformed_binary_receipt_never_crashes_or_passes(self):
        path = self.root / "cpp-receipt.json"
        value = json.loads(path.read_text())
        value["binaryArtifacts"] = {"test-binary": 1}
        self.write(path, value)
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_missing_output_never_passes(self):
        (self.root / "swift.json").unlink()
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_partial_json_or_duplicate_keys_are_rejected(self):
        for text in ('{"cases":', '{"schemaVersion":1,"schemaVersion":1}',
                     '{"cases":NaN}', '{"cases":Infinity}', '{"cases":"\\ud800"}'):
            (self.root / "swift.json").write_text(text)
            self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_json_semantics_preserve_types_order_missing_and_unicode(self):
        for a, b in ((True, 1), (False, 0), ({}, {"x": None}),
                     ([1, 2], [2, 1]), ("é", "e\u0301"), ("1", 1)):
            self.assertNotEqual(compare.canonical(a), compare.canonical(b))
        self.assertEqual(compare.canonical({"b": 2, "a": 1}),
                         compare.canonical({"a": Decimal("1.000"), "b": 2}))
        self.assertEqual(compare.canonical(0), compare.canonical(Decimal("-0.000")))
        huge = 1234567890123456789012345678901234567890
        self.assertEqual(compare.canonical(huge), compare.canonical(Decimal(str(huge) + ".00")))
        self.assertNotEqual(compare.canonical(huge), compare.canonical(huge + 1))

    def test_preservation_keeps_exact_inputs_and_seed_and_refuses_overwrite(self):
        self.outputs["swift"]["cases"][0]["payload"]["n"] = 2
        self.save()
        result = self.result()
        target = self.root / "failure"
        compare.preserve_failures(self.fixture, result, target)
        self.assertEqual((target / "source-fixture.json").read_bytes(), self.fixture.read_bytes())
        regression = json.loads((target / "regression-candidate.json").read_text())
        self.assertEqual(regression["cases"], [self.document["cases"][0]])
        with self.assertRaises(FileExistsError):
            compare.preserve_failures(self.fixture, result, target)

    def test_preservation_requires_behavioral_failure_and_unchanged_fixture(self):
        with self.assertRaises(compare.EvidenceError):
            compare.preserve_failures(self.fixture, self.result(), self.root / "not-a-failure")
        self.outputs["swift"]["cases"][0]["payload"]["n"] = 2
        self.save()
        result = self.result()
        self.fixture.write_bytes(self.fixture.read_bytes() + b"\n")
        with self.assertRaises(compare.EvidenceError):
            compare.preserve_failures(self.fixture, result, self.root / "stale")

    def test_cli_exit_codes_distinguish_differences_from_missing_evidence(self):
        argv = ["--fixture", str(self.fixture), "--cpp", str(self.root / "cpp.json"),
                "--swift", str(self.root / "swift.json"),
                "--cpp-receipt", str(self.root / "cpp-receipt.json"),
                "--swift-receipt", str(self.root / "swift-receipt.json")]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(compare.main(argv), 0)
            self.outputs["swift"]["cases"][0]["payload"]["n"] = 9
            self.save()
            self.assertEqual(compare.main(argv), 1)
            (self.root / "cpp-receipt.json").unlink()
            self.assertEqual(compare.main(argv), 2)

    def test_receipt_writer_preserves_nonzero_process_exit(self):
        with mock.patch.object(recorder.subprocess, "check_output", side_effect=["b" * 40, ""]):
            value = recorder.receipt(self.root / "cpp.json", self.fixture, self.root,
                                     [self.binary], self.root / "cpp-receipt.json", 7)
        self.assertEqual(value["exitCode"], 7)
        self.assertEqual(value["sourceCommit"], "b" * 40)
        self.write(self.root / "cpp-receipt.json", value)
        self.assertEqual(self.result()["verdict"], "INSUFFICIENT")

    def test_failed_native_assertion_still_preserves_observed_regression(self):
        self.outputs["swift"]["cases"][0]["payload"]["n"] = 9
        self.save()
        path = self.root / "swift-receipt.json"
        receipt = json.loads(path.read_text())
        receipt["exitCode"] = 1
        self.write(path, receipt)
        result = self.result()
        self.assertEqual(result["verdict"], "FAIL")
        self.assertEqual(result["testProcessFailures"], {"swift": 1})
        compare.preserve_failures(self.fixture, result, self.root / "actual-regression")
        self.assertTrue((self.root / "actual-regression/regression-candidate.json").exists())

    def test_receipt_writer_rejects_dirty_source_and_duplicate_binaries(self):
        for status, binaries in ((" M file.swift", [self.binary]),
                                 ("", [self.binary, self.binary])):
            with mock.patch.object(recorder.subprocess, "check_output", side_effect=["b" * 40, status]):
                with self.assertRaises(compare.EvidenceError):
                    recorder.receipt(self.root / "cpp.json", self.fixture, self.root,
                                     binaries, self.root / "receipt.json", 0)

    def test_receipt_writer_refuses_existing_receipt(self):
        args = ["--output", str(self.root / "cpp.json"), "--fixture", str(self.fixture),
                "--source-tree", str(self.root), "--binary", str(self.binary),
                "--receipt", str(self.root / "cpp-receipt.json"), "--exit-code", "0"]
        original = (self.root / "cpp-receipt.json").read_bytes()
        with mock.patch.object(recorder.subprocess, "check_output", side_effect=["b" * 40, ""]):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(recorder.main(args), 2)
        self.assertEqual((self.root / "cpp-receipt.json").read_bytes(), original)

    def test_all_committed_merge_cases_have_valid_oracles(self):
        for name, count in (("merge_v3.json", 140), ("merge_utf8_sort_keys_v3.json", 8)):
            _, document = compare.read_json(ROOT / "fixtures/sync-conformance" / name)
            self.assertEqual(len(compare.validate_fixture(document)), count)


if __name__ == "__main__":
    unittest.main()
