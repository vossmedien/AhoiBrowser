import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]

import sys

sys.path.insert(0, str(ROOT / "tools"))
import evidence  # noqa: E402


def result_for(test_id: str, test_class: str, status: str = "NOT_RUN"):
    chromium = evidence.load("config/chromium.json")
    entry = evidence.registry_entry(test_id)
    payload = {
        "schemaVersion": 2,
        "testId": test_id,
        "requirement": entry["description"],
        "testClass": test_class,
        "status": status,
        "executedBy": "test-agent",
        "source": {"gitCommit": "1" * 40, "gitDirty": False},
        "productVersion": (ROOT / "VERSION").read_text(encoding="utf-8").strip(),
        "chromium": {
            "version": chromium["version"],
            "commit": chromium["commit"],
        },
        "bundle": {
            "path": "/Applications/AhoiBrowser.app",
            "sha256": "NOT_AVAILABLE",
            "architecture": "NOT_AVAILABLE",
            "name": "NOT_AVAILABLE",
            "identifier": "NOT_AVAILABLE",
            "marketingVersion": "NOT_AVAILABLE",
            "buildNumber": "NOT_AVAILABLE",
            "productVersion": "NOT_AVAILABLE",
            "channel": "NOT_AVAILABLE",
            "sourceCommit": "NOT_AVAILABLE",
            "chromiumVersion": "NOT_AVAILABLE",
            "chromiumCommit": "NOT_AVAILABLE",
            "gnArgsSha256": "NOT_AVAILABLE",
            "buildProfile": "NOT_AVAILABLE",
            "teamIdentifier": "NOT_AVAILABLE",
            "signingAuthority": "NOT_AVAILABLE",
            "signatureVerified": False,
            "hardenedRuntimeVerified": False,
            "notarizationVerified": False,
        },
        "environment": {
            "osVersion": "26.6",
            "osBuild": "fixture",
            "device": "Mac-fixture",
            "hostArchitecture": "arm64",
            "locale": "de",
            "theme": "system",
            "glass": False,
            "profileType": "fresh",
            "startState": "fixture start state",
        },
        "startedAt": "2026-08-20T00:00:00+00:00",
        "completedAt": "2026-08-20T00:00:01+00:00",
        "expectedResult": entry["description"],
        "actualResult": "NOT_RUN: fixture",
        "steps": [],
        "assertions": [],
        "evidence": {
            "screenshots": [],
            "videos": [],
            "redactedLogs": [],
            "fixtureReceipts": [],
            "networkCaptures": [],
            "fileHashes": {},
            "testReports": [],
            "linkedIssue": None,
            "repeatRun": None,
        },
        "artifacts": [],
        "blocker": {
            "condition": "fixture not run",
            "owner": "test",
            "nextAction": "run fixture",
        },
    }
    if status == "PASS":
        payload.pop("blocker")
        payload["actualResult"] = "fixture executed"
        payload["steps"] = [
            {
                "index": 1,
                "action": "execute",
                "expected": "works",
                "observed": "works",
            }
        ]
        payload["evidence"]["repeatRun"] = "repeat-result.json"
    return payload


class EvidenceValidationTests(unittest.TestCase):
    def validate(self, payload, **kwargs):
        with tempfile.TemporaryDirectory(prefix="ahoi-evidence-test-") as directory:
            path = pathlib.Path(directory) / "result.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            return evidence.validate_result(path, **kwargs)

    def enabled_release_config(self):
        original_load = evidence.load

        def configured_load(relative):
            if relative == "config/release-evidence.json":
                return {
                    "schemaVersion": 1,
                    "releasePassEnabled": True,
                    "requiredChain": [
                        "build-provenance",
                        "signed-package-provenance",
                        "notarization-receipt",
                        "installed-bundle-binding",
                    ],
                }
            if relative == "config/release-policy.json":
                policy = original_load(relative)
                policy["manifestSigning"]["trustedKeyIds"] = ["b" * 64]
                return policy
            return original_load(relative)

        return configured_load

    def test_registry_class_cannot_be_downgraded_to_bypass_computer_use(self):
        errors = self.validate(result_for("AUTH-27", "UNIT"))
        self.assertTrue(any("testClass does not match registry" in item for item in errors))

    def test_computer_use_pass_requires_real_bundle_proof_fields(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["assertions"] = [
            {"name": "visible journey", "passed": True, "evidence": "fixture"}
        ]
        errors = self.validate(payload)
        self.assertIn(
            "Computer Use PASS requires signature, Hardened Runtime, and notarization",
            errors,
        )
        self.assertIn("Computer Use PASS requires an installed bundle hash", errors)
        self.assertIn("Computer Use PASS requires an ARM64-only bundle", errors)
        self.assertIn(evidence.RELEASE_EVIDENCE_NOT_READY, errors)

    def test_computer_use_pass_fails_closed_without_release_attestation_chain(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["bundle"].update(
            {
                "sha256": "a" * 64,
                "architecture": "arm64",
                "signatureVerified": True,
                "hardenedRuntimeVerified": True,
                "notarizationVerified": True,
            }
        )
        payload["assertions"] = [
            {"name": "visible", "passed": True, "evidence": "claimed"}
        ]
        payload["evidence"]["screenshots"] = ["screenshots/visible.png"]
        self.assertIn(evidence.RELEASE_EVIDENCE_NOT_READY, self.validate(payload))

    def test_enabled_boolean_and_claimed_receipt_names_cannot_bypass_chain(self):
        with mock.patch.object(
            evidence,
            "load",
            side_effect=self.enabled_release_config(),
        ), mock.patch.object(
            evidence.release_chain,
            "validate_manifest",
        ) as validator:
            errors = evidence.validate_release_evidence_chain(None, None)

        self.assertIn(evidence.RELEASE_EVIDENCE_NOT_READY, errors)
        self.assertIn(evidence.RELEASE_MANIFEST_REQUIRED, errors)
        self.assertIn(evidence.RELEASE_PUBLIC_KEY_REQUIRED, errors)
        validator.assert_not_called()

    def test_release_chain_uses_explicit_paths_and_live_installed_app(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-release-gate-test-") as directory:
            root = pathlib.Path(directory)
            manifest = root / "release-manifest.json"
            public_key = root / "release-public.pem"
            manifest.write_text("{}\n", encoding="utf-8")
            public_key.write_text("fixture\n", encoding="utf-8")
            with mock.patch.object(
                evidence,
                "load",
                side_effect=self.enabled_release_config(),
            ), mock.patch.object(
                evidence.release_chain,
                "validate_manifest",
                return_value={"kind": "ahoi-release-manifest"},
            ) as validator:
                errors = evidence.validate_release_evidence_chain(
                    manifest,
                    public_key,
                )

        self.assertEqual([], errors)
        validator.assert_called_once()
        args, kwargs = validator.call_args
        self.assertEqual(manifest.resolve(), args[0])
        self.assertEqual(public_key.resolve(), kwargs["public_key"])
        self.assertEqual({"b" * 64}, kwargs["trusted_key_ids"])
        self.assertEqual(evidence.INSTALLED_BUNDLE, kwargs["installed_app"])
        self.assertEqual(
            ROOT / "config/macos-entitlements.json",
            kwargs["policy_path"],
        )

    def test_release_chain_validator_failure_is_a_hard_pass_failure(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-release-gate-test-") as directory:
            root = pathlib.Path(directory)
            manifest = root / "release-manifest.json"
            public_key = root / "release-public.pem"
            manifest.write_text("{}\n", encoding="utf-8")
            public_key.write_text("fixture\n", encoding="utf-8")
            with mock.patch.object(
                evidence,
                "load",
                side_effect=self.enabled_release_config(),
            ), mock.patch.object(
                evidence.release_chain,
                "validate_manifest",
                side_effect=evidence.ReleaseError("forged receipt"),
            ):
                errors = evidence.validate_release_evidence_chain(
                    manifest,
                    public_key,
                )

        self.assertIn(evidence.RELEASE_EVIDENCE_NOT_READY, errors)
        self.assertIn(
            "release evidence chain validation failed: forged receipt",
            errors,
        )

    def test_computer_use_pass_forwards_explicit_chain_paths(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["assertions"] = [
            {"name": "visible", "passed": True, "evidence": "fixture"}
        ]
        payload["evidence"]["screenshots"] = ["screenshots/visible.png"]
        manifest = pathlib.Path("/secure/release-manifest.json")
        public_key = pathlib.Path("/secure/release-public.pem")
        unavailable_bundle = evidence.installed_bundle_checks(
            pathlib.Path("/definitely/not/an/app")
        )
        with mock.patch.object(
            evidence,
            "validate_release_evidence_chain",
            return_value=["chain-validator-sentinel"],
        ) as chain_validator, mock.patch.object(
            evidence,
            "repository_state",
            return_value=payload["source"],
        ), mock.patch.object(
            evidence,
            "installed_bundle_checks",
            return_value=unavailable_bundle,
        ), mock.patch.object(
            evidence,
            "bundle_hash",
            return_value=evidence.NOT_AVAILABLE,
        ), mock.patch.object(
            evidence,
            "succeeds",
            return_value=False,
        ):
            errors = self.validate(
                payload,
                release_manifest=manifest,
                release_public_key=public_key,
            )

        self.assertIn("chain-validator-sentinel", errors)
        chain_validator.assert_called_once_with(manifest, public_key)

    def test_claimed_proof_cannot_replace_live_installed_bundle_validation(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["bundle"].update(
            {
                "sha256": "a" * 64,
                "architecture": "arm64",
                "signatureVerified": True,
                "hardenedRuntimeVerified": True,
                "notarizationVerified": True,
            }
        )
        payload["evidence"]["screenshots"] = ["screenshots/visible.png"]
        payload["assertions"] = [
            {"name": "claimed", "passed": True, "evidence": "not sufficient"}
        ]
        missing_bundle = evidence.installed_bundle_checks(
            pathlib.Path("/definitely/not/an/app")
        )
        with mock.patch.object(
            evidence, "installed_bundle_checks", return_value=missing_bundle
        ), mock.patch.object(evidence, "bundle_hash", return_value="NOT_AVAILABLE"):
            errors = self.validate(payload)
        self.assertIn("installed Computer Use bundle identity is not AhoiBrowser", errors)
        self.assertIn("installed Computer Use bundle signature is not valid", errors)
        self.assertIn(
            "installed Computer Use bundle has no valid notarization staple",
            errors,
        )
        self.assertIn(
            "installed Computer Use bundle is not available for validation",
            errors,
        )

    def test_not_run_requires_an_owned_next_action(self):
        payload = result_for("AUTH-27", "CU_E2E")
        payload.pop("blocker")
        self.assertIn(
            "NOT_RUN and blocked results require blocker details",
            self.validate(payload),
        )

    def test_pass_binds_requirement_source_and_repeat_run(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["requirement"] = "different claim"
        payload["source"]["gitDirty"] = True
        payload["evidence"]["repeatRun"] = None
        errors = self.validate(payload)
        self.assertIn("requirement does not match the test registry", errors)
        self.assertIn("PASS requires a clean Ahoi source checkout", errors)
        self.assertIn("PASS requires evidence of a successful repeat run", errors)

    def test_repository_pin_mismatch_is_rejected(self):
        payload = result_for("AUTH-27", "CU_E2E")
        payload["chromium"]["commit"] = "0" * 40
        errors = self.validate(payload)
        self.assertIn(
            "Chromium version/commit do not match the pinned repository baseline",
            errors,
        )

    def test_build_profile_enum_matches_the_strict_schema(self):
        payload = result_for("SEC-01", "INTEGRATION")
        payload["bundle"]["buildProfile"] = "garbage"
        self.assertIn("bundle.buildProfile is invalid", self.validate(payload))

    def test_malformed_nested_evidence_is_rejected_without_a_crash(self):
        payload = result_for("AUTH-27", "CU_E2E")
        payload["completedAt"] = "not-a-timestamp"
        payload["evidence"]["unexpected"] = True
        errors = self.validate(payload)
        self.assertIn("completedAt is not a valid date-time", errors)
        self.assertIn("unexpected field: evidence.unexpected", errors)

    def test_visual_evidence_cannot_escape_or_skip_hash_binding(self):
        payload = result_for("AUTH-27", "CU_E2E", "PASS")
        payload["bundle"].update(
            {
                "sha256": "a" * 64,
                "architecture": "arm64",
                "signatureVerified": True,
                "hardenedRuntimeVerified": True,
                "notarizationVerified": True,
            }
        )
        payload["assertions"] = [
            {"name": "visible", "passed": True, "evidence": "escape"}
        ]
        payload["evidence"]["screenshots"] = ["../outside.png"]
        errors = self.validate(payload)
        self.assertIn(
            "evidence path must stay inside the test package: ../outside.png",
            errors,
        )

    def test_integration_pass_cannot_fabricate_repeat_or_runner_reports(self):
        payload = result_for("SEC-01", "INTEGRATION", "PASS")
        payload["assertions"] = [
            {"name": "runner", "passed": True, "evidence": "claimed"}
        ]
        payload["evidence"]["repeatRun"] = "missing-repeat.json"
        errors = self.validate(payload)
        self.assertIn("PASS requires at least one hashed test report", errors)
        self.assertIn("evidence file is missing: missing-repeat.json", errors)


if __name__ == "__main__":
    unittest.main()
