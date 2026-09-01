#!/usr/bin/env python3
"""Fail-closed certificate-state tests; never mutate a real trust store."""

from __future__ import annotations

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Optional
from unittest import mock


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

import certificates  # noqa: E402
import certificate_state  # noqa: E402
import manage  # noqa: E402
import trust_receipts  # noqa: E402


class CertificateStateHardeningTests(unittest.TestCase):
    MATERIAL_PAYLOADS = {
        "caCertificate": (
            "CERTIFICATE",
            "Y2EtY2VydGlmaWNhdGU=",
        ),
        "caPrivateKey": (
            "EC PRIVATE KEY",
            "Y2EtcHJpdmF0ZS1rZXk=",
        ),
        "leafCertificate": (
            "CERTIFICATE",
            "bGVhZi1jZXJ0aWZpY2F0ZQ==",
        ),
        "leafPrivateKey": (
            "EC PRIVATE KEY",
            "bGVhZi1wcml2YXRlLWtleQ==",
        ),
    }

    def _write_manifest_material(
        self,
        state_directory: Path,
        *,
        material_directory: Optional[Path] = None,
        relative_paths: bool = False,
    ) -> tuple[dict[str, object], dict[str, Path]]:
        state_directory.mkdir(parents=True, exist_ok=True)
        material_root = material_directory or state_directory
        material_root.mkdir(parents=True, exist_ok=True)
        paths: dict[str, Path] = {}
        manifest: dict[str, object] = {
            "schemaVersion": 1,
            "hostNames": list(certificates.HOST_NAMES),
            "caSha1": "1" * 40,
            "caSha256": "2" * 64,
            "leafSha256": "3" * 64,
        }
        for key, filename in certificates.CERTIFICATE_MATERIAL_FILES.items():
            label, payload = self.MATERIAL_PAYLOADS[key]
            path = material_root / filename
            path.write_text(
                "-----BEGIN %s-----\n%s\n-----END %s-----\n"
                % (label, payload, label),
                encoding="ascii",
            )
            paths[key] = path
            manifest[key] = filename if relative_paths else str(path.resolve())
        (state_directory / certificates.CERTIFICATE_MANIFEST).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return manifest, paths

    def test_generated_manifest_material_is_canonical_and_state_local(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-state-local-certs-") as temporary:
            state_directory = Path(temporary)
            manifest = certificates.generate(state_directory)
            stored_manifest = certificates.read_manifest(state_directory)
            self.assertIsNotNone(stored_manifest)

            for key, filename in certificates.CERTIFICATE_MATERIAL_FILES.items():
                self.assertEqual(filename, stored_manifest[key])
                self.assertEqual(
                    (state_directory / filename).resolve(),
                    Path(str(manifest[key])).resolve(),
                )
                self.assertFalse(Path(str(manifest[key])).is_symlink())

    def test_relative_legacy_paths_normalize_only_inside_state_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-relative-certs-") as temporary:
            state_directory = Path(temporary)
            _manifest, paths = self._write_manifest_material(
                state_directory,
                relative_paths=True,
            )

            normalized = certificates.validated_manifest(state_directory)

            for key, path in paths.items():
                self.assertEqual(path.resolve(), Path(str(normalized[key])))

    def test_external_legacy_manifest_fails_closed_without_touching_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-external-certs-") as temporary:
            root = Path(temporary)
            state_directory = root / "state"
            _manifest, paths = self._write_manifest_material(
                state_directory,
                material_directory=root / "legacy-external-material",
            )

            operations = (
                lambda: certificates.validated_manifest(state_directory),
                lambda: certificates.generate(state_directory),
                lambda: certificates.remove_certificate_material(state_directory),
            )
            for operation in operations:
                with self.subTest(operation=operation):
                    with self.assertRaisesRegex(
                        certificates.CertificateError,
                        "legacy, external, or non-canonical",
                    ):
                        operation()
                    self.assertTrue(
                        state_directory.joinpath(
                            certificates.CERTIFICATE_MANIFEST
                        ).is_file()
                    )
                    self.assertTrue(all(path.is_file() for path in paths.values()))

    def test_receipt_match_requires_literal_true_explicit_consent(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-consent-receipt-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            pending = trust_receipts.new_pending_receipt(
                manifest,
                trust_type=certificates.TRUST_TYPE_MACOS,
                target={"keychain": str((state_directory / "login.keychain-db").resolve())},
            )
            receipt = trust_receipts.installed_successor(pending, manifest)
            for consent in (None, False, 1, "true"):
                candidate = dict(receipt)
                if consent is None:
                    candidate.pop("explicitConsent")
                else:
                    candidate["explicitConsent"] = consent
                with self.subTest(consent=consent):
                    self.assertFalse(
                        certificates._receipt_matches_manifest(candidate, manifest)
                    )

            with mock.patch.object(
                certificates,
                "_fingerprint",
                return_value=manifest["caSha256"],
            ), mock.patch.object(
                certificates,
                "_certificate_sha256",
                return_value=manifest["leafSha256"],
            ):
                self.assertTrue(
                    certificates._receipt_matches_manifest(receipt, manifest)
                )

    def test_duplicate_pem_blocks_cleanup_before_any_removal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-duplicate-certs-") as temporary:
            state_directory = Path(temporary)
            _manifest, paths = self._write_manifest_material(state_directory)
            duplicate = state_directory / "copies" / "renamed-key.pem"
            duplicate.parent.mkdir()
            label, payload = self.MATERIAL_PAYLOADS["leafPrivateKey"]
            duplicate.write_text(
                "-----BEGIN %s-----\n  %s  \n-----END %s-----\n"
                % (label, payload, label),
                encoding="ascii",
            )

            with self.assertRaisesRegex(
                certificates.CertificateError,
                "duplicate certificate/key material",
            ):
                certificates.remove_certificate_material(state_directory)

            self.assertTrue(duplicate.is_file())
            self.assertTrue(all(path.is_file() for path in paths.values()))
            self.assertTrue(
                (state_directory / certificates.CERTIFICATE_MANIFEST).is_file()
            )

            duplicate.unlink()
            result = certificates.remove_certificate_material(state_directory)
            self.assertEqual(4, result["removedMaterialCount"])
            self.assertEqual(
                "byte-or-same-pem-der-and-hard-links",
                result["duplicateScanScope"],
            )
            self.assertTrue(result["manifestRemoved"])
            self.assertTrue(all(not path.exists() for path in paths.values()))
            self.assertFalse(
                (state_directory / certificates.CERTIFICATE_MANIFEST).exists()
            )

    def test_external_hard_link_blocks_cleanup_before_any_removal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-hardlink-certs-") as temporary:
            root = Path(temporary)
            state_directory = root / "state"
            _manifest, paths = self._write_manifest_material(state_directory)
            external_reference = root / "external-key-reference.pem"
            os.link(paths["caPrivateKey"], external_reference)

            with self.assertRaisesRegex(
                certificates.CertificateError,
                "hard-linked certificate/key material",
            ):
                certificates.remove_certificate_material(state_directory)

            self.assertTrue(external_reference.is_file())
            self.assertTrue(all(path.is_file() for path in paths.values()))

    def test_installed_pending_and_incomplete_receipts_all_block_cleanup(self) -> None:
        receipt_cases = (
            ("installed", certificates.TRUST_RECEIPT),
            ("pending", certificates.TRUST_RECEIPT),
            ("pending-write", certificates.TRUST_RECEIPT + ".pending"),
        )
        for installation_state, receipt_name in receipt_cases:
            with self.subTest(installation_state=installation_state):
                with tempfile.TemporaryDirectory(
                    prefix="ahoi-receipt-cleanup-"
                ) as temporary:
                    state_directory = Path(temporary)
                    _manifest, paths = self._write_manifest_material(state_directory)
                    (state_directory / receipt_name).write_text(
                        json.dumps(
                            {
                                "schemaVersion": 2,
                                "trustType": certificates.TRUST_TYPE_SIMULATOR,
                                "installationState": installation_state,
                                "explicitConsent": True,
                            }
                        )
                        + "\n",
                        encoding="utf-8",
                    )
                    args = manage.build_parser().parse_args(
                        ["cleanup", "--state-dir", str(state_directory)]
                    )
                    stdout = io.StringIO()
                    stderr = io.StringIO()
                    with mock.patch.object(
                        manage.custom_protocol,
                        "is_installed",
                        return_value=False,
                    ), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(
                        stderr
                    ):
                        self.assertEqual(1, args.function(args))

                    self.assertEqual("", stdout.getvalue())
                    self.assertIn("installed/pending trust receipt", stderr.getvalue())
                    self.assertTrue(all(path.is_file() for path in paths.values()))
                    self.assertTrue(
                        (state_directory / certificates.CERTIFICATE_MANIFEST).is_file()
                    )

    def test_generation_intermediates_stay_in_private_random_staging(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-generation-stage-") as temporary:
            state_directory = Path(temporary)
            sentinel = state_directory / "outside-sentinel"
            sentinel.write_text("unchanged", encoding="utf-8")
            hostile_names = (
                "ahoi-e2e-leaf.ext",
                "ahoi-e2e-leaf.csr",
                "ahoi-e2e-ca.srl",
            )
            for name in hostile_names:
                (state_directory / name).symlink_to(sentinel)
            commands: list[list[str]] = []

            def recording_runner(command, **kwargs):
                commands.append(list(command))
                return subprocess.run(command, **kwargs)

            certificates.generate(state_directory, runner=recording_runner)

            staged_arguments = [
                value
                for command in commands
                for value in command
                if value.endswith((".csr", ".ext", ".srl"))
            ]
            self.assertTrue(staged_arguments)
            self.assertTrue(
                all("/.certificate-generation-" in value for value in staged_arguments)
            )
            self.assertEqual("unchanged", sentinel.read_text(encoding="utf-8"))
            self.assertTrue(
                all((state_directory / name).is_symlink() for name in hostile_names)
            )

    def test_macos_receipt_is_pending_before_mutation_and_removal_is_verified(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-macos-journal-") as temporary:
            state_directory = Path(temporary)
            manifest = certificates.generate(state_directory)
            keychain = (state_directory / "isolated.keychain-db").resolve()
            trusted = False

            def runner(command, **_kwargs):
                nonlocal trusted
                command = list(command)
                if command[0] == "openssl":
                    algorithm = command[-1]
                    value = manifest["caSha256"] if algorithm == "-sha256" else manifest["caSha1"]
                    return subprocess.CompletedProcess(command, 0, "SHA=%s\n" % value, "")
                if command[:2] == ["security", "add-trusted-cert"]:
                    pending = certificates.read_trust_receipt(state_directory)
                    self.assertEqual("pending", pending["installationState"])
                    trusted = True
                elif command[:2] == ["security", "find-certificate"]:
                    output = (
                        "SHA-256 hash: %s\n" % str(manifest["caSha256"]).upper()
                        if trusted
                        else ""
                    )
                    return subprocess.CompletedProcess(command, 0, output, "")
                elif command[:2] == ["security", "delete-certificate"]:
                    trusted = False
                return subprocess.CompletedProcess(command, 0, "", "")

            receipt = certificates.install_trust(
                state_directory,
                confirmation=certificates.TRUST_CONFIRMATION,
                keychain=keychain,
                runner=runner,
            )
            self.assertEqual("installed", receipt["installationState"])
            self.assertEqual(3, receipt["schemaVersion"])
            self.assertTrue(
                certificates.remove_trust(
                    state_directory,
                    confirmation=certificates.REMOVE_CONFIRMATION,
                    runner=runner,
                )
            )
            self.assertFalse(trusted)
            self.assertIsNone(certificates.read_trust_receipt(state_directory))

    def test_failed_macos_mutation_retains_removable_pending_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-macos-pending-") as temporary:
            state_directory = Path(temporary)
            manifest = certificates.generate(state_directory)

            def failing_runner(command, **_kwargs):
                command = list(command)
                if command[0] == "openssl":
                    algorithm = command[-1]
                    value = manifest["caSha256"] if algorithm == "-sha256" else manifest["caSha1"]
                    return subprocess.CompletedProcess(command, 0, "SHA=%s\n" % value, "")
                if command[:2] == ["security", "add-trusted-cert"]:
                    self.assertEqual(
                        "pending",
                        certificates.read_trust_receipt(state_directory)["installationState"],
                    )
                    raise subprocess.CalledProcessError(1, command, stderr="injected")
                return subprocess.CompletedProcess(command, 0, "", "")

            with self.assertRaisesRegex(certificates.CertificateError, "command failed"):
                certificates.install_trust(
                    state_directory,
                    confirmation=certificates.TRUST_CONFIRMATION,
                    keychain=state_directory / "isolated.keychain-db",
                    runner=failing_runner,
                )
            self.assertEqual(
                "pending",
                certificates.read_trust_receipt(state_directory)["installationState"],
            )
            self.assertTrue(
                certificates.remove_trust(
                    state_directory,
                    confirmation=certificates.REMOVE_CONFIRMATION,
                    runner=lambda command, **_kwargs: subprocess.CompletedProcess(
                        command, 0, "", ""
                    ),
                )
            )
            self.assertIsNone(certificates.read_trust_receipt(state_directory))

    def test_receipt_journal_recovers_pending_only_successor_and_truncation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-receipt-recovery-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            pending = trust_receipts.new_pending_receipt(
                manifest,
                trust_type=certificates.TRUST_TYPE_MACOS,
                target={"keychain": str((state_directory / "login.keychain-db").resolve())},
            )
            receipt_path = state_directory / certificates.TRUST_RECEIPT
            pending_path = state_directory / (certificates.TRUST_RECEIPT + ".pending")
            pending_path.write_text("{", encoding="utf-8")
            self.assertIsNone(certificates.read_trust_receipt(state_directory))
            self.assertFalse(pending_path.exists())
            pending_path.write_text(json.dumps(pending), encoding="utf-8")
            self.assertEqual(pending, certificates.read_trust_receipt(state_directory))
            self.assertTrue(receipt_path.is_file())

            installed = trust_receipts.installed_successor(pending, manifest)
            pending_path.write_text(json.dumps(installed), encoding="utf-8")
            self.assertEqual(installed, certificates.read_trust_receipt(state_directory))
            self.assertFalse(pending_path.exists())

            receipt_path.write_text(json.dumps(pending), encoding="utf-8")
            pending_path.write_text("{", encoding="utf-8")
            self.assertEqual(pending, certificates.read_trust_receipt(state_directory))
            self.assertFalse(pending_path.exists())

    def test_receipt_symlink_and_hardlink_are_rejected(self) -> None:
        for link_type in ("symlink", "hardlink"):
            with self.subTest(link_type=link_type), tempfile.TemporaryDirectory(
                prefix="ahoi-receipt-link-"
            ) as temporary:
                state_directory = Path(temporary)
                self._write_manifest_material(state_directory, relative_paths=True)
                manifest = certificates.validated_manifest(state_directory)
                receipt = trust_receipts.new_pending_receipt(
                    manifest,
                    trust_type=certificates.TRUST_TYPE_MACOS,
                    target={"keychain": str((state_directory / "login.keychain-db").resolve())},
                )
                source = state_directory / "receipt-source.json"
                source.write_text(json.dumps(receipt), encoding="utf-8")
                destination = state_directory / certificates.TRUST_RECEIPT
                if link_type == "symlink":
                    destination.symlink_to(source)
                else:
                    os.link(source, destination)
                with self.assertRaises(certificates.CertificateError):
                    certificates.read_trust_receipt(state_directory)

    def test_v3_receipt_strictly_binds_schema_target_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-receipt-binding-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            receipt = trust_receipts.new_pending_receipt(
                manifest,
                trust_type=certificates.TRUST_TYPE_MACOS,
                target={"keychain": str((state_directory / "login.keychain-db").resolve())},
            )
            mutations = (
                {"schemaVersion": 99},
                {"trustType": "unknown"},
                {"manifestSha256": "0" * 64},
                {"keychain": "relative.keychain-db"},
                {"unexpected": True},
            )
            for mutation in mutations:
                candidate = {**receipt, **mutation}
                with self.subTest(mutation=mutation), self.assertRaises(
                    certificates.CertificateError
                ):
                    trust_receipts.validate_receipt(
                        candidate,
                        manifest,
                        allow_legacy=False,
                    )

            simulator = trust_receipts.new_pending_receipt(
                manifest,
                trust_type=certificates.TRUST_TYPE_SIMULATOR,
                target={
                    "simulatorUDID": "CAE7F82B-52D2-4607-992C-EDF40C323DE3",
                    "simulatorName": "Ahoi Mobile E2E",
                    "simulatorDeviceTypeIdentifier": "test-device-type",
                    "simulatorRuntimeIdentifier": "test-runtime",
                    "simulatorDataPath": str((state_directory / "sim-data").resolve()),
                    "rootStoreVerification": trust_receipts.ROOT_STORE_VERIFICATION,
                },
            )
            invalid_simulator = dict(simulator)
            invalid_simulator["simulatorUDID"] = "booted"
            with self.assertRaisesRegex(certificates.CertificateError, "invalid exact UDID"):
                trust_receipts.validate_receipt(
                    invalid_simulator,
                    manifest,
                    allow_legacy=False,
                )

    def test_v1_receipt_requires_explicit_verified_migration(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-v1-migration-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            legacy = {
                "schemaVersion": 1,
                "explicitConsent": True,
                "keychain": str((state_directory / "login.keychain-db").resolve()),
                "caSha1": manifest["caSha1"],
                "caSha256": manifest["caSha256"],
            }
            (state_directory / certificates.TRUST_RECEIPT).write_text(
                json.dumps(legacy), encoding="utf-8"
            )
            self.assertFalse(certificates.trust_installation_is_valid(state_directory))
            with mock.patch.object(
                certificates,
                "_macos_trust_installation_is_valid",
                return_value=True,
            ):
                migrated = certificates.migrate_legacy_trust_receipt(
                    state_directory,
                    confirmation=certificates.TRUST_RECEIPT_MIGRATION_CONFIRMATION,
                )
            self.assertEqual(3, migrated["schemaVersion"])
            self.assertEqual(1, migrated["migrationSourceSchemaVersion"])
            self.assertEqual(migrated, certificates.read_trust_receipt(state_directory))

    def test_v1_receipt_with_nonliteral_consent_cannot_migrate(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-v1-consent-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            legacy = {
                "schemaVersion": 1,
                "explicitConsent": 1,
                "keychain": str((state_directory / "login.keychain-db").resolve()),
                "caSha1": manifest["caSha1"],
                "caSha256": manifest["caSha256"],
            }
            (state_directory / certificates.TRUST_RECEIPT).write_text(
                json.dumps(legacy), encoding="utf-8"
            )
            with self.assertRaisesRegex(certificates.CertificateError, "literal explicit"):
                certificates.migrate_legacy_trust_receipt(
                    state_directory,
                    confirmation=certificates.TRUST_RECEIPT_MIGRATION_CONFIRMATION,
                )

    def test_partial_material_unlink_resumes_from_durable_journal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-cleanup-journal-") as temporary:
            state_directory = Path(temporary)
            _manifest, paths = self._write_manifest_material(state_directory)
            original_unlink = certificate_state._unlink_regular
            material_calls = 0

            def fail_second_material(path, *, label):
                nonlocal material_calls
                if label == "journaled certificate/key material":
                    material_calls += 1
                    if material_calls == 2:
                        raise certificates.CertificateError("injected unlink interruption")
                return original_unlink(path, label=label)

            with mock.patch.object(
                certificate_state,
                "_unlink_regular",
                side_effect=fail_second_material,
            ):
                with self.assertRaisesRegex(
                    certificates.CertificateError, "injected unlink interruption"
                ):
                    certificates.remove_certificate_material(state_directory)
            self.assertEqual(3, sum(path.is_file() for path in paths.values()))
            self.assertTrue(
                (state_directory / certificate_state.CLEANUP_JOURNAL).is_file()
            )
            self.assertTrue(
                (state_directory / certificates.CERTIFICATE_MANIFEST).is_file()
            )

            result = certificates.remove_certificate_material(state_directory)
            self.assertTrue(result["journalResumed"])
            self.assertFalse(any(path.exists() for path in paths.values()))
            self.assertFalse(
                (state_directory / certificate_state.CLEANUP_JOURNAL).exists()
            )

    def test_pending_simulator_receipt_can_cancel_only_after_exact_absence(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-simulator-cancel-") as temporary:
            state_directory = Path(temporary)
            self._write_manifest_material(state_directory, relative_paths=True)
            manifest = certificates.validated_manifest(state_directory)
            identity = {
                "simulatorUDID": "CAE7F82B-52D2-4607-992C-EDF40C323DE3",
                "simulatorName": "Ahoi Mobile E2E",
                "simulatorDeviceTypeIdentifier": "test-device-type",
                "simulatorRuntimeIdentifier": "test-runtime",
                "simulatorDataPath": str((state_directory / "sim-data").resolve()),
            }
            pending = trust_receipts.new_pending_receipt(
                manifest,
                trust_type=certificates.TRUST_TYPE_SIMULATOR,
                target={
                    **identity,
                    "rootStoreVerification": trust_receipts.ROOT_STORE_VERIFICATION,
                },
            )
            trust_receipts.write_initial_pending(state_directory, manifest, pending)
            with mock.patch.object(
                certificates, "_recorded_simulator_identity", return_value=identity
            ), mock.patch.object(
                certificates, "_simulator_root_store_contains_ca", return_value=True
            ):
                with self.assertRaisesRegex(certificates.CertificateError, "still trusts"):
                    certificates.cancel_pending_simulator_trust(
                        state_directory,
                        confirmation=certificates.SIMULATOR_TRUST_CANCEL_CONFIRMATION,
                    )
            self.assertIsNotNone(certificates.read_trust_receipt(state_directory))
            with mock.patch.object(
                certificates, "_recorded_simulator_identity", return_value=identity
            ), mock.patch.object(
                certificates, "_simulator_root_store_contains_ca", return_value=False
            ):
                self.assertTrue(
                    certificates.cancel_pending_simulator_trust(
                        state_directory,
                        confirmation=certificates.SIMULATOR_TRUST_CANCEL_CONFIRMATION,
                    )
                )
            self.assertIsNone(certificates.read_trust_receipt(state_directory))


if __name__ == "__main__":
    unittest.main()
