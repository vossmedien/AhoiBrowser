#!/usr/bin/env python3
"""Deterministic simulator-trust tests; never mutate a real trust store."""

from __future__ import annotations

import json
import shutil
import sqlite3
import ssl
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

import certificates  # noqa: E402
import manage  # noqa: E402


SIMULATOR_UDID = "CAE7F82B-52D2-4607-992C-EDF40C323DE3"
RUNTIME_ID = "com.apple.CoreSimulator.SimRuntime.iOS-26-5"
DEVICE_TYPE_ID = "com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro"


class SimulatorTrustLifecycleTests(unittest.TestCase):
    def _make_trust_store(self, data_path: Path) -> Path:
        trust_store = data_path / certificates.SIMULATOR_TRUST_STORE
        trust_store.parent.mkdir(parents=True)
        connection = sqlite3.connect(trust_store)
        try:
            connection.execute(
                "CREATE TABLE tsettings(sha256 BLOB NOT NULL, data BLOB)"
            )
            connection.commit()
        finally:
            connection.close()
        return trust_store

    def _inventory(self, data_path: Path, *, state: str = "Booted") -> str:
        return json.dumps(
            {
                "devices": {
                    RUNTIME_ID: [
                        {
                            "udid": SIMULATOR_UDID,
                            "name": "Ahoi Mobile E2E",
                            "deviceTypeIdentifier": DEVICE_TYPE_ID,
                            "dataPath": str(data_path),
                            "isAvailable": True,
                            "state": state,
                        }
                    ]
                }
            }
        )

    def _runner(
        self,
        manifest,
        data_path: Path,
        commands: list[list[str]],
        *,
        install_row: bool = True,
        inventory_state: str = "Booted",
        device_present: bool = True,
    ):
        ca_path = str(manifest["caCertificate"])
        leaf_path = str(manifest["leafCertificate"])

        def runner(command, **_kwargs):
            command = list(command)
            commands.append(command)
            if command[0] == "openssl" and "-fingerprint" in command:
                certificate = command[command.index("-in") + 1]
                algorithm = command[-1]
                if certificate == ca_path:
                    value = (
                        manifest["caSha256"]
                        if algorithm == "-sha256"
                        else manifest["caSha1"]
                    )
                elif certificate == leaf_path and algorithm == "-sha256":
                    value = manifest["leafSha256"]
                else:
                    return subprocess.CompletedProcess(command, 1, "", "unexpected cert")
                return subprocess.CompletedProcess(command, 0, "SHA=%s\n" % value, "")
            if command == ["xcrun", "simctl", "list", "devices", "--json"]:
                inventory = (
                    self._inventory(data_path, state=inventory_state)
                    if device_present
                    else json.dumps({"devices": {RUNTIME_ID: []}})
                )
                return subprocess.CompletedProcess(command, 0, inventory, "")
            if command[:3] == ["xcrun", "simctl", "keychain"]:
                if install_row:
                    der = ssl.PEM_cert_to_DER_cert(
                        Path(ca_path).read_text(encoding="utf-8")
                    )
                    trust_store = data_path / certificates.SIMULATOR_TRUST_STORE
                    connection = sqlite3.connect(trust_store)
                    try:
                        connection.execute(
                            "INSERT INTO tsettings(sha256, data) VALUES (?, ?)",
                            (bytes.fromhex(str(manifest["caSha256"])), der),
                        )
                        connection.commit()
                    finally:
                        connection.close()
                return subprocess.CompletedProcess(command, 0, "", "")
            return subprocess.CompletedProcess(command, 1, "", "unexpected command")

        return runner

    def test_install_binds_exact_booted_device_and_root_fingerprints(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-simulator-trust-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            data_path = directory / "simulator-data"
            self._make_trust_store(data_path)
            commands: list[list[str]] = []
            runner = self._runner(manifest, data_path, commands)

            receipt = certificates.install_simulator_trust(
                directory,
                confirmation=certificates.SIMULATOR_TRUST_CONFIRMATION,
                udid=SIMULATOR_UDID,
                runner=runner,
            )

            self.assertEqual(certificates.TRUST_TYPE_SIMULATOR, receipt["trustType"])
            self.assertEqual("installed", receipt["installationState"])
            self.assertEqual(SIMULATOR_UDID, receipt["simulatorUDID"])
            self.assertEqual(RUNTIME_ID, receipt["simulatorRuntimeIdentifier"])
            self.assertEqual(DEVICE_TYPE_ID, receipt["simulatorDeviceTypeIdentifier"])
            self.assertEqual(manifest["caSha1"], receipt["caSha1"])
            self.assertEqual(manifest["caSha256"], receipt["caSha256"])
            self.assertEqual(manifest["leafSha256"], receipt["leafSha256"])
            add_commands = [
                command
                for command in commands
                if command[:3] == ["xcrun", "simctl", "keychain"]
            ]
            self.assertEqual(
                [[
                    "xcrun",
                    "simctl",
                    "keychain",
                    SIMULATOR_UDID,
                    "add-root-cert",
                    str(manifest["caCertificate"]),
                ]],
                add_commands,
            )
            self.assertNotIn(str(manifest["caPrivateKey"]), add_commands[0])
            self.assertTrue(
                certificates.trust_installation_is_valid(directory, runner=runner)
            )
            shutdown_runner = self._runner(
                manifest,
                data_path,
                commands,
                inventory_state="Shutdown",
            )
            self.assertFalse(
                certificates.trust_installation_is_valid(
                    directory,
                    runner=shutdown_runner,
                )
            )

    def test_consent_alias_and_shutdown_device_fail_before_mutation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-simulator-refuse-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            data_path = directory / "simulator-data"
            self._make_trust_store(data_path)
            commands: list[list[str]] = []
            runner = self._runner(manifest, data_path, commands)

            with self.assertRaisesRegex(certificates.CertificateError, "exact confirmation"):
                certificates.install_simulator_trust(
                    directory,
                    confirmation="yes",
                    udid=SIMULATOR_UDID,
                    runner=runner,
                )
            with self.assertRaisesRegex(certificates.CertificateError, "exact uppercase UUID"):
                certificates.install_simulator_trust(
                    directory,
                    confirmation=certificates.SIMULATOR_TRUST_CONFIRMATION,
                    udid="booted",
                    runner=runner,
                )
            shutdown_runner = self._runner(
                manifest,
                data_path,
                commands,
                inventory_state="Shutdown",
            )
            with self.assertRaisesRegex(certificates.CertificateError, "available and Booted"):
                certificates.install_simulator_trust(
                    directory,
                    confirmation=certificates.SIMULATOR_TRUST_CONFIRMATION,
                    udid=SIMULATOR_UDID,
                    runner=shutdown_runner,
                )
            self.assertFalse(
                any(command[:3] == ["xcrun", "simctl", "keychain"] for command in commands)
            )
            self.assertIsNone(certificates.read_trust_receipt(directory))

    def test_success_without_root_row_keeps_pending_cleanup_obligation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-simulator-pending-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            data_path = directory / "simulator-data"
            self._make_trust_store(data_path)
            commands: list[list[str]] = []
            runner = self._runner(
                manifest,
                data_path,
                commands,
                install_row=False,
            )

            with self.assertRaisesRegex(certificates.CertificateError, "exact CA is absent"):
                certificates.install_simulator_trust(
                    directory,
                    confirmation=certificates.SIMULATOR_TRUST_CONFIRMATION,
                    udid=SIMULATOR_UDID,
                    runner=runner,
                )
            receipt = certificates.read_trust_receipt(directory)
            self.assertIsNotNone(receipt)
            self.assertEqual("pending", receipt["installationState"])
            self.assertFalse(
                certificates.trust_installation_is_valid(directory, runner=runner)
            )

    def test_no_broad_cleanup_and_finalize_requires_exact_device_absence(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-simulator-finalize-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            data_path = directory / "simulator-data"
            self._make_trust_store(data_path)
            commands: list[list[str]] = []
            present_runner = self._runner(manifest, data_path, commands)
            certificates.install_simulator_trust(
                directory,
                confirmation=certificates.SIMULATOR_TRUST_CONFIRMATION,
                udid=SIMULATOR_UDID,
                runner=present_runner,
            )

            with self.assertRaisesRegex(certificates.CertificateError, "no single-root removal"):
                certificates.remove_trust(
                    directory,
                    confirmation=certificates.REMOVE_CONFIRMATION,
                    runner=present_runner,
                )
            with self.assertRaisesRegex(certificates.CertificateError, "still exists"):
                certificates.finalize_deleted_simulator_trust(
                    directory,
                    confirmation=certificates.SIMULATOR_TRUST_FINALIZE_CONFIRMATION,
                    runner=present_runner,
                )
            absent_runner = self._runner(
                manifest,
                data_path,
                commands,
                device_present=False,
            )
            shutil.rmtree(data_path)
            self.assertTrue(
                certificates.finalize_deleted_simulator_trust(
                    directory,
                    confirmation=certificates.SIMULATOR_TRUST_FINALIZE_CONFIRMATION,
                    runner=absent_runner,
                )
            )
            self.assertIsNone(certificates.read_trust_receipt(directory))
            flattened = " ".join(" ".join(command) for command in commands)
            self.assertNotIn(" keychain %s reset" % SIMULATOR_UDID, flattened)
            self.assertNotIn(" delete ", flattened)

    def test_cli_exposes_typed_simulator_lifecycle(self) -> None:
        parser = manage.build_parser()
        install = parser.parse_args(
            [
                "simulator-trust-install",
                "--udid",
                SIMULATOR_UDID,
                "--confirm",
                certificates.SIMULATOR_TRUST_CONFIRMATION,
            ]
        )
        finalize = parser.parse_args(
            [
                "simulator-trust-finalize",
                "--confirm",
                certificates.SIMULATOR_TRUST_FINALIZE_CONFIRMATION,
            ]
        )
        self.assertIs(install.function, manage.command_simulator_trust_install)
        self.assertIs(finalize.function, manage.command_simulator_trust_finalize)


if __name__ == "__main__":
    unittest.main()
