import json
import os
import pathlib
import plistlib
import shutil
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import development_installation  # noqa: E402
from release import cli, common, installation  # noqa: E402


EXPECTED_BUNDLE = {
    "name": "AhoiBrowser",
    "identifier": "app.ahoibrowser.AhoiBrowser",
    "architecture": "arm64",
    "buildProfile": "release",
}


def make_app(
    parent: pathlib.Path,
    *,
    version: str,
    build: str,
    source: str,
    payload: bytes,
    profile: str = "release",
) -> pathlib.Path:
    app = parent / "AhoiBrowser.app"
    executable = app / "Contents/MacOS/AhoiBrowser"
    executable.parent.mkdir(parents=True)
    executable.write_bytes(payload)
    executable.chmod(0o755)
    plist = {
        "CFBundleName": "AhoiBrowser",
        "CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser",
        "CFBundleExecutable": "AhoiBrowser",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": build,
        "AhoiProductVersion": version,
        "AhoiUpdateChannel": "stable",
        "AhoiSourceCommit": source,
        "AhoiChromiumVersion": "152.0.8033.0",
        "AhoiChromiumCommit": "2" * 40,
        "AhoiGNArgsSHA256": "3" * 64,
        "AhoiBuildProfile": profile,
    }
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
    return app


def write_receipts(root: pathlib.Path, app: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    identity = common.bundle_identity(app)
    signed_identity = dict(identity)
    signed_identity["bundleTreeSha256"] = "a" * 64
    signing = root / "signing.json"
    notary = root / "notary.json"
    signing.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "kind": "signed-package-provenance",
                "signedBundle": signed_identity,
                "signing": {
                    "teamIdentifier": "ABCDEFGHIJ",
                    "authority": "Developer ID Application: Fixture (ABCDEFGHIJ)",
                    "hardenedRuntime": True,
                    "trustedTimestamp": True,
                },
            }
        )
    )
    notary.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "kind": "notarization-receipt",
                "bundle": {
                    "preStapleTreeSha256": "a" * 64,
                    "postStapleTreeSha256": identity["bundleTreeSha256"],
                    "identity": identity,
                    "staplerValidated": True,
                    "gatekeeperAccepted": True,
                },
            }
        )
    )
    return signing, notary


def copy_bundle(source: pathlib.Path, destination: pathlib.Path) -> None:
    shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True)


def exchange_bundle(first: pathlib.Path, second: pathlib.Path) -> None:
    temporary = first.parent / ".test-exchange"
    os.rename(first, temporary)
    os.rename(second, first)
    os.rename(temporary, second)


def move_exclusive(source: pathlib.Path, destination: pathlib.Path) -> None:
    if os.path.lexists(destination):
        raise common.ReleaseError("test destination already exists")
    os.rename(source, destination)


def verify_bundle(app: pathlib.Path, **_kwargs: object) -> dict:
    return {"bundle": common.bundle_identity(app)}


def receipt_factory(app: pathlib.Path, **kwargs: object) -> dict:
    receipt = {
        "bundle": common.bundle_identity(app),
        "installation": kwargs["installation"],
    }
    common.atomic_write_json(kwargs["output"], receipt)
    return receipt


class ReleaseInstallationTests(unittest.TestCase):
    def test_cli_exposes_only_policy_fixed_install_target(self):
        arguments = cli.parser().parse_args(
            [
                "install",
                "--app",
                "/Volumes/AhoiBrowser/AhoiBrowser.app",
                "--signing-receipt",
                "/evidence/signing.json",
                "--notary-receipt",
                "/evidence/notary.json",
                "--output",
                "/evidence/installed.json",
            ]
        )
        self.assertIs(arguments.handler, cli._install)
        self.assertFalse(hasattr(arguments, "target"))

    def install(
        self,
        candidate: pathlib.Path,
        target: pathlib.Path,
        signing: pathlib.Path,
        notary: pathlib.Path,
        output: pathlib.Path,
        **overrides: object,
    ) -> dict:
        arguments = {
            "copy_bundle": copy_bundle,
            "exchange_bundle": exchange_bundle,
            "move_exclusive": move_exclusive,
            "process_inspector": lambda _paths: [],
            "bundle_verifier": verify_bundle,
            "receipt_factory": receipt_factory,
        }
        arguments.update(overrides)
        return installation.install_release_app(
            candidate,
            signing_receipt_path=signing,
            notarization_receipt_path=notary,
            policy_path=ROOT / "config/macos-entitlements.json",
            output=output,
            expected_bundle=EXPECTED_BUNDLE,
            required_install_path=target,
            **arguments,
        )

    def test_replacement_keeps_version_bound_backup_and_atomic_evidence(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-success-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old",
            )
            previous = common.bundle_identity(target)
            signing, notary = write_receipts(root, candidate)
            output = root / "installed.json"

            receipt = self.install(candidate, target, signing, notary, output)

            self.assertEqual(common.bundle_identity(candidate), common.bundle_identity(target))
            evidence = receipt["installation"]
            self.assertEqual("renameatx_np(RENAME_SWAP)", evidence["method"])
            self.assertTrue(evidence["sameVolumeStaging"])
            backup = pathlib.Path(evidence["previousBundle"]["backupPath"])
            self.assertRegex(backup.name, r"^\.AhoiBrowser\.rollback-v1\.0\.0-b10-")
            self.assertEqual(previous, common.bundle_identity(backup))

    def test_replacement_verification_failure_restores_previous_bundle(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-rollback-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old",
            )
            previous = common.bundle_identity(target)
            signing, notary = write_receipts(root, candidate)
            output = root / "installed.json"

            def fail_after_receipt(app: pathlib.Path, **kwargs: object) -> dict:
                receipt_factory(app, **kwargs)
                raise common.ReleaseError("downstream fixture failure")

            with self.assertRaisesRegex(common.ReleaseError, "prior installation state"):
                self.install(
                    candidate,
                    target,
                    signing,
                    notary,
                    output,
                    receipt_factory=fail_after_receipt,
                )

            self.assertEqual(previous, common.bundle_identity(target))
            self.assertFalse(output.exists())
            self.assertEqual([], list(target.parent.glob(".AhoiBrowser.rollback-*.app")))

    def test_failed_initial_installation_restores_target_absence(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-initial-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            applications = root / "Applications"
            applications.mkdir()
            target = applications / "AhoiBrowser.app"
            signing, notary = write_receipts(root, candidate)
            output = root / "installed.json"

            def fail_after_receipt(app: pathlib.Path, **kwargs: object) -> dict:
                receipt_factory(app, **kwargs)
                raise common.ReleaseError("downstream fixture failure")

            with self.assertRaisesRegex(common.ReleaseError, "prior installation state"):
                self.install(
                    candidate,
                    target,
                    signing,
                    notary,
                    output,
                    receipt_factory=fail_after_receipt,
                )

            self.assertFalse(target.exists())
            self.assertFalse(output.exists())
            self.assertEqual([], list(applications.glob(".AhoiBrowser.stage-*.app")))

    def test_running_bundle_process_blocks_before_staging(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-running-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            applications = root / "Applications"
            applications.mkdir()
            target = applications / "AhoiBrowser.app"
            signing, notary = write_receipts(root, candidate)

            with self.assertRaisesRegex(common.ReleaseError, "quit all AhoiBrowser"):
                self.install(
                    candidate,
                    target,
                    signing,
                    notary,
                    root / "installed.json",
                    process_inspector=lambda _paths: [
                        {"pid": 42, "bundle": str(candidate), "command": "fixture"}
                    ],
                )

            self.assertEqual([], list(applications.glob(".AhoiBrowser.*.app")))

    def test_foreign_receipt_appearing_during_install_is_preserved(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-output-race-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old",
            )
            previous = common.bundle_identity(target)
            signing, notary = write_receipts(root, candidate)
            output = root / "installed.json"

            def copy_and_create_foreign_output(
                source: pathlib.Path, destination: pathlib.Path
            ) -> None:
                copy_bundle(source, destination)
                output.write_text("foreign evidence\n")

            with self.assertRaisesRegex(common.ReleaseError, "output appeared"):
                self.install(
                    candidate,
                    target,
                    signing,
                    notary,
                    output,
                    copy_bundle=copy_and_create_foreign_output,
                )

            self.assertEqual("foreign evidence\n", output.read_text())
            self.assertEqual(previous, common.bundle_identity(target))
            self.assertEqual([], list(target.parent.glob(".AhoiBrowser.rollback-*.app")))
            self.assertEqual([], list(root.glob(".*.install-reservation")))

    def test_symlink_candidate_and_reserved_backup_collision_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-install-paths-") as directory:
            root = pathlib.Path(directory).resolve()
            real = make_app(
                root / "real",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new",
            )
            linked_parent = root / "linked"
            linked_parent.mkdir()
            linked = linked_parent / "AhoiBrowser.app"
            linked.symlink_to(real, target_is_directory=True)
            applications = root / "Applications"
            applications.mkdir()
            target = applications / "AhoiBrowser.app"
            signing, notary = write_receipts(root, real)
            with self.assertRaisesRegex(common.ReleaseError, "real app directory"):
                self.install(linked, target, signing, notary, root / "linked.json")

            target = make_app(
                applications,
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old",
            )
            previous = common.bundle_identity(target)
            collision = installation._transaction_path(
                applications,
                prefix=installation.BACKUP_PREFIX,
                identity=previous,
                bundle_hash=previous["bundleTreeSha256"],
            )
            collision.mkdir()
            with self.assertRaisesRegex(common.ReleaseError, "already exists"):
                self.install(real, target, signing, notary, root / "collision.json")


class DevelopmentInstallationTests(unittest.TestCase):
    def test_cli_has_a_policy_fixed_target(self):
        arguments = development_installation.parser().parse_args(
            [
                "--app",
                "/Volumes/Ahoi/AhoiBrowser.app",
                "--output",
                "/evidence/installed-dev.json",
            ]
        )
        self.assertEqual("/Volumes/Ahoi/AhoiBrowser.app", arguments.app)
        self.assertFalse(hasattr(arguments, "target"))

    def install(
        self,
        candidate: pathlib.Path,
        target: pathlib.Path,
        output: pathlib.Path,
        verifier,
        **overrides: object,
    ) -> dict:
        arguments = {
            "copy_bundle": copy_bundle,
            "exchange_bundle": exchange_bundle,
            "move_exclusive": move_exclusive,
            "process_inspector": lambda _paths: [],
        }
        arguments.update(overrides)
        return development_installation.install_development_app(
            candidate,
            output=output,
            required_install_path=target,
            verifier=verifier,
            **arguments,
        )

    def test_dev_replacement_verifies_candidate_stage_and_activated_target(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-dev-install-success-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new-dev",
                profile="dev",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old-release",
            )
            previous = common.bundle_identity(target)
            output = root / "installed-dev.json"
            verified = []

            def verifier(app: pathlib.Path) -> None:
                verified.append(app)

            receipt = self.install(candidate, target, output, verifier)

            self.assertEqual(candidate, verified[0])
            self.assertRegex(verified[1].name, r"^\.AhoiBrowser\.rollback-")
            self.assertEqual(target, verified[2])
            self.assertEqual(3, len(verified))
            self.assertEqual("development-installation-receipt", receipt["kind"])
            self.assertFalse(receipt["releaseEvidenceEligible"])
            self.assertEqual("dev", receipt["bundle"]["buildProfile"])
            backup = pathlib.Path(
                receipt["installation"]["previousBundle"]["backupPath"]
            )
            self.assertEqual(previous, common.bundle_identity(backup))
            self.assertEqual(receipt, json.loads(output.read_text()))

    def test_post_activation_dev_verification_failure_rolls_back(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-dev-install-rollback-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new-dev",
                profile="dev",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old-release",
            )
            previous = common.bundle_identity(target)
            output = root / "installed-dev.json"
            calls = 0

            def verifier(_app: pathlib.Path) -> None:
                nonlocal calls
                calls += 1
                if calls == 3:
                    raise common.ReleaseError("installed dev verification failed")

            with self.assertRaisesRegex(common.ReleaseError, "prior installation state"):
                self.install(candidate, target, output, verifier)

            self.assertEqual(3, calls)
            self.assertEqual(previous, common.bundle_identity(target))
            self.assertFalse(output.exists())
            self.assertEqual([], list(target.parent.glob(".AhoiBrowser.rollback-*.app")))

    def test_release_profile_is_rejected_before_verification_or_staging(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-dev-install-profile-") as directory:
            root = pathlib.Path(directory).resolve()
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"release",
            )
            applications = root / "Applications"
            applications.mkdir()
            target = applications / "AhoiBrowser.app"
            calls = []

            with self.assertRaisesRegex(common.ReleaseError, "build profile"):
                self.install(
                    candidate,
                    target,
                    root / "installed-dev.json",
                    lambda app: calls.append(app),
                )

            self.assertEqual([], calls)
            self.assertEqual([], list(applications.glob(".AhoiBrowser.*.app")))

    def test_verifier_change_is_detected_before_activation(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-dev-verifier-race-") as directory:
            root = pathlib.Path(directory).resolve()
            verifier_script = root / "verify-built-app.sh"
            verifier_script.write_text("#!/bin/bash\nexit 0\n")
            verifier_script.chmod(0o755)
            candidate = make_app(
                root / "candidate",
                version="2.0.0",
                build="20",
                source="4" * 40,
                payload=b"new-dev",
                profile="dev",
            )
            target = make_app(
                root / "Applications",
                version="1.0.0",
                build="10",
                source="5" * 40,
                payload=b"old-release",
            )
            previous = common.bundle_identity(target)
            output = root / "installed-dev.json"
            calls = 0

            def mutate_verifier(_app: pathlib.Path) -> None:
                nonlocal calls
                calls += 1
                if calls == 2:
                    verifier_script.write_text("#!/bin/bash\nexit 1\n")
                    verifier_script.chmod(0o755)

            with mock.patch.object(
                development_installation, "VERIFY_SCRIPT", verifier_script
            ):
                with self.assertRaisesRegex(common.ReleaseError, "changed during"):
                    self.install(
                        candidate,
                        target,
                        output,
                        mutate_verifier,
                    )

            self.assertEqual(2, calls)
            self.assertEqual(previous, common.bundle_identity(target))
            self.assertFalse(output.exists())
            self.assertEqual([], list(root.glob(".*.install-reservation")))


if __name__ == "__main__":
    unittest.main()
