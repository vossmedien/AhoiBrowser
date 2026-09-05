import json
import pathlib
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import mobile_release_candidate_receipt as candidate_receipt  # noqa: E402


TOOLCHAIN = {
    "xcodeVersion": "Xcode 26.0\nBuild version 17A000",
    "swiftVersion": "Apple Swift version 6.2",
}


class MobileReleaseCandidateReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ahoi-mobile-candidate-")
        # macOS may provide TMPDIR through /var -> /private/var. The fixture
        # itself must be canonical before testing deliberate symlink inputs.
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.repository = self.root / "repository"
        self.repository.mkdir()
        self.run_git("init", "--quiet")
        self.run_git("config", "user.email", "candidate@example.invalid")
        self.run_git("config", "user.name", "Candidate Fixture")
        (self.repository / "tracked.txt").write_text("clean\n", encoding="utf-8")
        self.run_git("add", "tracked.txt")
        self.run_git("commit", "--quiet", "-m", "fixture")
        self.head = self.run_git("rev-parse", "HEAD").stdout.strip()
        self.app = self.root / "AhoiMobile.app"
        self.app.mkdir()
        self.binary = self.app / "AhoiMobile"
        self.binary.write_bytes(b"exact simulator executable")
        self.write_info()
        self.project = self.root / "AhoiMobile.xcodeproj"
        self.project.mkdir()
        (self.project / "project.pbxproj").write_text(
            "// exact generated project\n", encoding="utf-8"
        )
        self.xctestrun = self.root / "AhoiMobile.xctestrun"
        self.xctestrun.write_bytes(b"exact xctestrun")

    def tearDown(self):
        self.temporary.cleanup()

    def run_git(self, *arguments):
        return subprocess.run(
            ["git", *arguments],
            cwd=self.repository,
            check=True,
            capture_output=True,
            text=True,
        )

    def write_info(self, **overrides):
        info = {
            "CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser",
            "CFBundleExecutable": "AhoiMobile",
            "CFBundleShortVersionString": "0.1",
            "CFBundleVersion": "1",
            "AhoiSourceCommit": self.head,
            "AhoiBuildMode": "DebugLocal",
            "DTPlatformName": "iphonesimulator",
            **overrides,
        }
        with (self.app / "Info.plist").open("wb") as handle:
            plistlib.dump(info, handle)

    @staticmethod
    def team_signature():
        return {
            "kind": "team",
            "valid": True,
            "identifier": "app.ahoibrowser.AhoiBrowser",
            "teamId": "248AJ5BN47",
            "distributionEligible": False,
        }

    @staticmethod
    def ad_hoc_signature():
        return {
            "kind": "adHoc",
            "valid": True,
            "identifier": "app.ahoibrowser.AhoiBrowser",
            "teamId": None,
            "distributionEligible": False,
        }

    def build(self, signature=None, **kwargs):
        signature = signature or self.team_signature()
        with (
            mock.patch.object(
                candidate_receipt, "inspect_signature", return_value=signature
            ),
            mock.patch.object(
                candidate_receipt, "inspect_toolchain", return_value=TOOLCHAIN
            ),
        ):
            return candidate_receipt.build_receipt(
                self.repository,
                self.app,
                **kwargs,
            )

    def test_clean_simulator_candidate_binds_identity_toolchain_and_all_hashes(self):
        receipt = self.build(
            xcode_project=self.project,
            xctestrun=self.xctestrun,
        )

        self.assertEqual(1, receipt["schemaVersion"])
        self.assertEqual("simulator-candidate-binding", receipt["kind"])
        self.assertEqual(self.head, receipt["sourceCommit"])
        self.assertFalse(receipt["sourceDirty"])
        self.assertEqual("app.ahoibrowser.AhoiBrowser", receipt["bundleId"])
        self.assertEqual("248AJ5BN47", receipt["teamId"])
        self.assertEqual("0.1", receipt["marketingVersion"])
        self.assertEqual("1", receipt["buildNumber"])
        self.assertEqual("DebugLocal", receipt["buildMode"])
        self.assertEqual("iphonesimulator", receipt["platform"])
        self.assertEqual(TOOLCHAIN, receipt["toolchain"])
        self.assertNotIn(str(self.root), json.dumps(receipt, sort_keys=True))
        self.assertEqual(
            candidate_receipt.sha256_path(self.app),
            receipt["hashes"]["appTreeSha256"],
        )
        self.assertEqual(
            candidate_receipt.sha256_path(self.binary),
            receipt["hashes"]["binarySha256"],
        )
        self.assertEqual(
            candidate_receipt.sha256_path(self.app / "Info.plist"),
            receipt["hashes"]["infoPlistSha256"],
        )
        self.assertIn("xcodeProjectSha256", receipt["hashes"])
        self.assertIn("xctestrunSha256", receipt["hashes"])

    def test_dirty_source_and_nonmatching_embedded_commit_fail_closed(self):
        (self.repository / "tracked.txt").write_text("dirty\n", encoding="utf-8")
        with self.assertRaisesRegex(
            candidate_receipt.CandidateReceiptError,
            "source is dirty",
        ):
            self.build()

        self.run_git("checkout", "--", "tracked.txt")
        self.write_info(AhoiSourceCommit="f" * 40)
        with self.assertRaisesRegex(
            candidate_receipt.CandidateReceiptError,
            "must equal the exact clean Git HEAD",
        ):
            self.build()

    def test_app_and_optional_artifact_symlinks_are_rejected(self):
        repository_link = self.root / "repository-link"
        repository_link.symlink_to(self.repository)
        with (
            mock.patch.object(
                candidate_receipt,
                "inspect_signature",
                return_value=self.team_signature(),
            ),
            mock.patch.object(
                candidate_receipt, "inspect_toolchain", return_value=TOOLCHAIN
            ),
            self.assertRaisesRegex(
                candidate_receipt.CandidateReceiptError,
                "repository root must not contain symlink components",
            ),
        ):
            candidate_receipt.build_receipt(repository_link, self.app)

        target = self.root / "target.txt"
        target.write_text("target\n", encoding="utf-8")
        (self.app / "linked.txt").symlink_to(target)
        with self.assertRaisesRegex(
            candidate_receipt.CandidateReceiptError,
            "app bundle must not contain symlinks",
        ):
            self.build()

        (self.app / "linked.txt").unlink()
        (self.project / "linked.pbxproj").symlink_to(
            self.project / "project.pbxproj"
        )
        with self.assertRaisesRegex(
            candidate_receipt.CandidateReceiptError,
            "Xcode project must not contain symlinks",
        ):
            self.build(xcode_project=self.project)

    def test_written_receipt_verifies_and_binary_change_is_detected(self):
        receipt = self.build(signature=self.ad_hoc_signature())
        output = self.root / "candidate-receipt.json"
        candidate_receipt.write_receipt(output, receipt)
        self.assertEqual(receipt, json.loads(output.read_text(encoding="utf-8")))
        self.assertEqual("adHoc", receipt["signing"]["kind"])
        self.assertIsNone(receipt["teamId"])
        self.assertFalse(receipt["signing"]["distributionEligible"])

        with (
            mock.patch.object(
                candidate_receipt,
                "inspect_signature",
                return_value=self.ad_hoc_signature(),
            ),
            mock.patch.object(
                candidate_receipt, "inspect_toolchain", return_value=TOOLCHAIN
            ),
        ):
            verified = candidate_receipt.verify_receipt(
                output, self.repository, self.app
            )
            self.assertEqual(receipt, verified)
            self.binary.write_bytes(b"changed executable")
            with self.assertRaisesRegex(
                candidate_receipt.CandidateReceiptError,
                "receipt.hashes.appTreeSha256",
            ):
                candidate_receipt.verify_receipt(
                    output, self.repository, self.app
                )

    def test_receipt_rejects_unexpected_fields_and_output_symlink(self):
        receipt = self.build()
        receipt["unexpected"] = "not part of the candidate contract"
        output = self.root / "candidate-receipt.json"
        candidate_receipt.write_receipt(output, receipt)
        with (
            mock.patch.object(
                candidate_receipt,
                "inspect_signature",
                return_value=self.team_signature(),
            ),
            mock.patch.object(
                candidate_receipt, "inspect_toolchain", return_value=TOOLCHAIN
            ),
            self.assertRaisesRegex(
                candidate_receipt.CandidateReceiptError,
                "fields do not match",
            ),
        ):
            candidate_receipt.verify_receipt(output, self.repository, self.app)

        output.unlink()
        output.symlink_to(self.root / "elsewhere.json")
        with self.assertRaisesRegex(
            candidate_receipt.CandidateReceiptError,
            "symlink components",
        ):
            candidate_receipt.write_receipt(output, self.build(), overwrite=True)


if __name__ == "__main__":
    unittest.main()
