import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import requirement_audit  # noqa: E402


CHAIN_NOT_READY = ["fixture release chain is not ready"]


def registry_entry(test_id: str):
    registry = json.loads(
        (ROOT / "config/test-registry.json").read_text(encoding="utf-8")
    )
    return next(entry for entry in registry["tests"] if entry["id"] == test_id)


def write_registry(path: pathlib.Path, entries):
    path.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "source": "fixture",
                "generatedAt": "2026-08-26T00:00:00Z",
                "tests": entries,
            }
        ),
        encoding="utf-8",
    )


class RequirementAuditTests(unittest.TestCase):
    def build(self, evidence_root: pathlib.Path, **kwargs):
        with mock.patch.object(
            requirement_audit.evidence,
            "validate_release_evidence_chain",
            return_value=CHAIN_NOT_READY,
        ):
            return requirement_audit.build_audit(
                evidence_root=evidence_root,
                **kwargs,
            )

    def test_current_registry_is_covered_exactly_once_with_complete_summaries(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            audit = self.build(pathlib.Path(directory))

        registry = json.loads(
            (ROOT / "config/test-registry.json").read_text(encoding="utf-8")
        )["tests"]
        expected_ids = {entry["id"] for entry in registry}
        expected_count = len(expected_ids)
        actual_ids = [entry["id"] for entry in audit["requirements"]]
        self.assertEqual(expected_count, len(actual_ids))
        self.assertEqual(expected_count, len(set(actual_ids)))
        self.assertEqual(expected_ids, set(actual_ids))
        self.assertEqual(expected_count, audit["summary"]["total"])
        self.assertEqual(
            expected_count,
            sum(
                item["count"]
                for item in audit["summary"]["byPrimaryClass"].values()
            ),
        )
        self.assertEqual(
            expected_count,
            sum(item["count"] for item in audit["summary"]["bySuite"].values()),
        )
        self.assertEqual(
            expected_ids,
            {
                test_id
                for item in audit["summary"]["byStatus"].values()
                for test_id in item["ids"]
            },
        )

    def test_json_and_markdown_are_deterministic_and_list_every_requirement(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-a-") as first_dir:
            with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-b-") as second_dir:
                first = self.build(pathlib.Path(first_dir))
                second = self.build(pathlib.Path(second_dir))

        first_json = requirement_audit.serialize_json(first)
        first_markdown = requirement_audit.render_markdown(first)
        self.assertEqual(first_json, requirement_audit.serialize_json(second))
        self.assertEqual(first_markdown, requirement_audit.render_markdown(second))
        self.assertEqual("<evidence-root>", first["evidenceRoot"])
        self.assertNotIn(first_dir, first_json + first_markdown)
        self.assertNotIn(second_dir, first_json + first_markdown)
        for entry in first["requirements"]:
            self.assertEqual(
                1,
                sum(
                    line.startswith(f"| {entry['id']} |")
                    for line in first_markdown.splitlines()
                ),
                entry["id"],
            )

    def test_repo_internal_paths_are_repo_relative_and_validator_paths_are_redacted(self):
        evidence_root = ROOT / "artifacts/e2e/portable-audit-fixture"
        manifest = ROOT / "artifacts/release/missing-manifest.json"
        absolute_error = f"release manifest is missing: {manifest}"
        with mock.patch.object(
            requirement_audit.evidence,
            "validate_release_evidence_chain",
            return_value=[absolute_error],
        ):
            audit = requirement_audit.build_audit(
                evidence_root=evidence_root,
                release_manifest=manifest,
            )

        rendered = requirement_audit.serialize_json(audit)
        self.assertEqual("artifacts/e2e/portable-audit-fixture", audit["evidenceRoot"])
        self.assertIn("artifacts/release/missing-manifest.json", rendered)
        self.assertNotIn(str(ROOT), rendered + requirement_audit.render_markdown(audit))

    def test_claimed_pass_is_not_accepted_when_evidence_validator_rejects_it(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            root = pathlib.Path(directory)
            registry_path = root / "registry.json"
            write_registry(registry_path, [registry_entry("SEC-01")])
            result_path = root / "SEC-01" / "result.json"
            result_path.parent.mkdir()
            result_path.write_text(
                json.dumps(
                    {
                        "testId": "SEC-01",
                        "status": "PASS",
                        "executedBy": "claimant",
                        "steps": [{"claimed": True}],
                        "evidence": {"testReports": ["claimed.xml"]},
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=["forged evidence rejected"],
            ) as validator:
                audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

        validator.assert_called_once()
        result = audit["requirements"][0]
        self.assertEqual("NOT_RUN", result["status"])
        self.assertEqual("INVALID", result["evidence"]["state"])
        self.assertEqual("PASS", result["evidence"]["declaredStatus"])
        self.assertIn("forged evidence rejected", result["evidence"]["validationErrors"])
        self.assertTrue(result["attempted"])

    def test_visible_pass_is_rejected_even_if_result_validator_is_stubbed_green(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            root = pathlib.Path(directory)
            registry_path = root / "registry.json"
            write_registry(registry_path, [registry_entry("AUTH-27")])
            result_path = root / "AUTH-27" / "result.json"
            result_path.parent.mkdir()
            result_path.write_text(
                json.dumps(
                    {
                        "testId": "AUTH-27",
                        "status": "PASS",
                        "executedBy": "claimant",
                        "steps": [{"claimed": True}],
                        "evidence": {"testReports": ["claimed.xml"]},
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=[],
            ):
                audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

        result = audit["requirements"][0]
        self.assertEqual("NOT_RUN", result["status"])
        self.assertEqual("INVALID", result["evidence"]["state"])
        self.assertIn(
            "CU_E2E and ASSISTED_E2E PASS require a validated release chain",
            result["evidence"]["validationErrors"],
        )
        self.assertIn("signed-release-provenance", result["externalGateIds"])
        self.assertFalse(result["locallyControllable"])

    def test_ubo_11_local_fail_closed_receipt_is_the_only_release_chain_exception(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            root = pathlib.Path(directory)
            registry_path = root / "registry.json"
            write_registry(registry_path, [registry_entry("UBO-11")])
            result_path = root / "UBO-11" / "result.json"
            result_path.parent.mkdir()
            receipt_path = result_path.parent / "ubo-11-local-receipt.json"
            receipt_path.write_text(
                json.dumps(
                    requirement_audit.evidence.UBO_11_LOCAL_FAIL_CLOSED_RECEIPT,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            digest = hashlib.sha256(receipt_path.read_bytes()).hexdigest()
            result_path.write_text(
                json.dumps(
                    {
                        "testId": "UBO-11",
                        "testClass": "CU_E2E",
                        "status": "PASS",
                        "executedBy": "fixture-runner",
                        "steps": [{"executed": True}],
                        "evidence": {
                            "fixtureReceipts": [receipt_path.name],
                            "fileHashes": {receipt_path.name: digest},
                            "testReports": ["validated-report.json"],
                        },
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=[],
            ):
                audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

            result = audit["requirements"][0]
            self.assertEqual("PASS", result["status"])
            self.assertEqual("VALID", result["evidence"]["state"])
            self.assertEqual([], result["externalGateIds"])
            self.assertTrue(result["locallyControllable"])

            rejected_receipt = dict(
                requirement_audit.evidence.UBO_11_LOCAL_FAIL_CLOSED_RECEIPT
            )
            rejected_receipt["positiveUboInstallAttempted"] = True
            receipt_path.write_text(
                json.dumps(rejected_receipt, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            payload = json.loads(result_path.read_text(encoding="utf-8"))
            payload["evidence"]["fileHashes"][receipt_path.name] = hashlib.sha256(
                receipt_path.read_bytes()
            ).hexdigest()
            result_path.write_text(json.dumps(payload), encoding="utf-8")
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=[],
            ):
                rejected_audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

        rejected = rejected_audit["requirements"][0]
        self.assertEqual("NOT_RUN", rejected["status"])
        self.assertEqual("INVALID", rejected["evidence"]["state"])
        self.assertIn(
            "CU_E2E and ASSISTED_E2E PASS require a validated release chain",
            rejected["evidence"]["validationErrors"],
        )

    def test_integration_pass_without_a_concrete_report_remains_not_run(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            root = pathlib.Path(directory)
            registry_path = root / "registry.json"
            write_registry(registry_path, [registry_entry("SEC-01")])
            result_path = root / "SEC-01" / "result.json"
            result_path.parent.mkdir()
            result_path.write_text(
                json.dumps(
                    {
                        "testId": "SEC-01",
                        "status": "PASS",
                        "executedBy": "claimant",
                        "steps": [{"claimed": True}],
                        "evidence": {"testReports": []},
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=[],
            ):
                audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

        result = audit["requirements"][0]
        self.assertEqual("NOT_RUN", result["status"])
        self.assertIn(
            "INTEGRATION PASS requires a concrete validated test report",
            result["evidence"]["validationErrors"],
        )

    def test_validated_integration_pass_with_bound_report_is_accepted(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            root = pathlib.Path(directory)
            registry_path = root / "registry.json"
            write_registry(registry_path, [registry_entry("SEC-04")])
            result_path = root / "SEC-04" / "result.json"
            result_path.parent.mkdir()
            report = result_path.parent / "report.json"
            report.write_text('{"passed": true}\n', encoding="utf-8")
            digest = hashlib.sha256(report.read_bytes()).hexdigest()
            result_path.write_text(
                json.dumps(
                    {
                        "testId": "SEC-04",
                        "status": "PASS",
                        "executedBy": "fixture-runner",
                        "steps": [{"executed": True}],
                        "evidence": {
                            "testReports": ["report.json"],
                            "fileHashes": {"report.json": digest},
                        },
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                requirement_audit.evidence,
                "validate_release_evidence_chain",
                return_value=CHAIN_NOT_READY,
            ), mock.patch.object(
                requirement_audit.evidence,
                "validate_result",
                return_value=[],
            ):
                audit = requirement_audit.build_audit(
                    registry_path=registry_path,
                    evidence_root=root,
                )

        result = audit["requirements"][0]
        self.assertEqual("PASS", result["status"])
        self.assertEqual("VALID", result["evidence"]["state"])
        self.assertEqual([], result["evidence"]["validationErrors"])
        self.assertTrue(result["attempted"])

    def test_every_open_requirement_has_exact_typed_disposition_fields(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-requirement-audit-") as directory:
            audit = self.build(pathlib.Path(directory))

        gates = {item["id"] for item in audit["externalGates"]["items"]}
        required_fields = {
            "condition": str,
            "owner": str,
            "attempted": bool,
            "locallyControllable": bool,
            "nextAction": str,
        }
        for item in audit["requirements"]:
            self.assertNotEqual("PASS", item["status"])
            for field, field_type in required_fields.items():
                self.assertIn(field, item)
                self.assertIsInstance(item[field], field_type)
                if field_type is str:
                    self.assertTrue(item[field])
            self.assertLessEqual(set(item["externalGateIds"]), gates)

        by_id = {item["id"]: item for item in audit["requirements"]}
        self.assertTrue(by_id["SEC-04"]["locallyControllable"])
        self.assertEqual([], by_id["SEC-04"]["externalGateIds"])
        self.assertFalse(by_id["AUTH-27"]["locallyControllable"])
        self.assertIn(
            "signed-release-provenance", by_id["AUTH-27"]["externalGateIds"]
        )
        self.assertIn("widevine-mla", by_id["DRM-01"]["externalGateIds"])
        self.assertIn(
            "cloudkit-device-validation", by_id["IOS-01"]["externalGateIds"]
        )
        for number in (*range(0, 9), 10, 11):
            test_id = f"UBO-{number:02d}"
            self.assertEqual([], by_id[test_id]["externalGateIds"])
            self.assertTrue(by_id[test_id]["locallyControllable"])
        self.assertIn(
            "ubo-catalog-hosting-and-signing",
            by_id["UBO-09"]["externalGateIds"],
        )
        self.assertNotIn(
            "ubo-redistribution", by_id["UBO-09"]["externalGateIds"]
        )
        self.assertEqual("CU_E2E", by_id["UBO-11"]["primaryClass"])
        self.assertEqual(
            {"chrome-web-store", "signed-release-provenance"},
            set(by_id["UBO-12"]["externalGateIds"]),
        )
        self.assertEqual([], by_id["UBO-13"]["externalGateIds"])
        self.assertTrue(by_id["UBO-13"]["locallyControllable"])
        for test_id in ("EXT-11", "EXT-15", "RECOVERY-MAC-15"):
            self.assertIn("chrome-web-store", by_id[test_id]["externalGateIds"])
        for number in range(1, 16):
            perf = by_id[f"PERF-{number:02d}"]
            self.assertFalse(perf["locallyControllable"])
            self.assertEqual(["disk-headroom"], perf["externalGateIds"])


if __name__ == "__main__":
    unittest.main()
