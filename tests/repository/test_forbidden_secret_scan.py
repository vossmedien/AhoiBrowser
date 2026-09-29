import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import forbidden_secret_scan  # noqa: E402


class ForbiddenSecretScanTests(unittest.TestCase):
    def canary_file(self, directory: pathlib.Path, value: bytes) -> pathlib.Path:
        path = directory / "canaries.txt"
        path.write_bytes(value + b"\n")
        os.chmod(path, 0o600)
        return path

    def test_clean_redacted_capture_passes_without_retaining_canary(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            canary = b"AHOI_SYNTHETIC_SECRET_clean-contract-0001"
            canary_file = self.canary_file(directory, canary)
            evidence = directory / "evidence"
            evidence.mkdir()
            (evidence / "netlog.json").write_text(
                'Authorization: <redacted>\nCookie: <redacted>\r\n'
                '"password": "<redacted>"\npassword=<redacted>\n',
                encoding="utf-8",
            )
            result = forbidden_secret_scan.scan(
                (forbidden_secret_scan._root("evidence", evidence),),
                canary_file=canary_file,
            )
            self.assertTrue(result["clean"])
            self.assertNotIn(canary.decode(), str(result))

    def test_canary_and_complete_auth_cookie_values_fail_value_safely(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            canary = b"AHOI_SYNTHETIC_SECRET_detect-contract-0002"
            canary_file = self.canary_file(directory, canary)
            crash = directory / "crash"
            crash.mkdir()
            (crash / "report.bin").write_bytes(
                b"prefix "
                + canary
                + b'\nAuthorization: Bearer opaque-value\n"Cookie": "session=opaque"\n'
            )
            result = forbidden_secret_scan.scan(
                (forbidden_secret_scan._root("crash", crash),),
                canary_file=canary_file,
            )
            self.assertFalse(result["clean"])
            self.assertEqual(
                {"synthetic-canary-1", "authorization-header", "cookie-field"},
                {finding["rule"] for finding in result["findings"]},
            )
            self.assertNotIn(canary.decode(), str(result))
            self.assertNotIn("opaque-value", str(result))
            self.assertNotIn("report.bin", str(result))
            self.assertTrue(
                all(
                    str(finding["fileId"]).startswith("file-")
                    for finding in result["findings"]
                )
            )

    def test_unquoted_credentials_fail_without_exposing_the_filename(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            canary_file = self.canary_file(
                directory, b"AHOI_SYNTHETIC_SECRET_unquoted-contract-0003"
            )
            evidence = directory / "evidence"
            evidence.mkdir()
            secret_filename = "customer-private-token.txt"
            (evidence / secret_filename).write_text(
                "password=unquoted-secret\naccess_token: another-secret\n",
                encoding="utf-8",
            )
            result = forbidden_secret_scan.scan(
                (forbidden_secret_scan._root("evidence", evidence),),
                canary_file=canary_file,
            )
            self.assertFalse(result["clean"])
            self.assertEqual(
                {"credential-field"},
                {finding["rule"] for finding in result["findings"]},
            )
            self.assertNotIn(secret_filename, str(result))
            self.assertNotIn("unquoted-secret", str(result))

    def test_empty_or_canary_only_root_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            canary_file = self.canary_file(
                directory, b"AHOI_SYNTHETIC_SECRET_empty-contract-0004"
            )
            for root in (directory / "empty", directory):
                if root != directory:
                    root.mkdir()
                with self.subTest(root=root):
                    with self.assertRaisesRegex(
                        forbidden_secret_scan.ScanConfigurationError,
                        "eligible regular file",
                    ):
                        forbidden_secret_scan.scan(
                            (forbidden_secret_scan._root("evidence", root),),
                            canary_file=canary_file,
                        )

    def test_canary_file_must_be_owner_only(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            path = self.canary_file(
                directory, b"AHOI_SYNTHETIC_SECRET_permissions-contract-0005"
            )
            os.chmod(path, 0o644)
            with self.assertRaisesRegex(
                forbidden_secret_scan.ScanConfigurationError, "group/world"
            ):
                forbidden_secret_scan._read_canaries(path)

    def test_canary_file_must_not_be_a_symlink(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            target = self.canary_file(
                directory, b"AHOI_SYNTHETIC_SECRET_symlink-contract-0006"
            )
            link = directory / "canary-link.txt"
            link.symlink_to(target)
            evidence = directory / "evidence"
            evidence.mkdir()
            with self.assertRaisesRegex(
                forbidden_secret_scan.ScanConfigurationError, "symlink"
            ):
                forbidden_secret_scan.scan(
                    (forbidden_secret_scan._root("evidence", evidence),),
                    canary_file=link,
                )

    def test_evidence_tree_must_not_contain_a_symlink(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            canary_file = self.canary_file(
                directory, b"AHOI_SYNTHETIC_SECRET_tree-symlink-contract-0007"
            )
            outside = directory / "outside.txt"
            outside.write_text("Authorization: <redacted>\n", encoding="utf-8")
            evidence = directory / "evidence"
            evidence.mkdir()
            (evidence / "capture.json").write_text("{}\n", encoding="utf-8")
            (evidence / "linked-capture.json").symlink_to(outside)
            with self.assertRaisesRegex(
                forbidden_secret_scan.ScanConfigurationError, "symlink"
            ):
                forbidden_secret_scan.scan(
                    (forbidden_secret_scan._root("evidence", evidence),),
                    canary_file=canary_file,
                )

    def test_file_generation_change_during_read_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-secret-scan-") as temporary:
            directory = pathlib.Path(temporary)
            evidence_path = directory / "capture.json"
            evidence_path.write_bytes(b"{}\n")
            root = forbidden_secret_scan._root("evidence", evidence_path)
            descriptor = os.open(evidence_path, os.O_RDONLY)
            initial = forbidden_secret_scan._stamp(os.fstat(descriptor))
            original_read = os.read
            changed = False

            def mutating_read(file_descriptor, length):
                nonlocal changed
                payload = original_read(file_descriptor, length)
                if payload and not changed:
                    changed = True
                    with evidence_path.open("ab") as stream:
                        stream.write(b"x")
                        stream.flush()
                        os.fsync(stream.fileno())
                return payload

            try:
                with mock.patch.object(
                    forbidden_secret_scan.os, "read", side_effect=mutating_read
                ):
                    with self.assertRaisesRegex(
                        forbidden_secret_scan.ScanConfigurationError,
                        "changed while it was read",
                    ):
                        forbidden_secret_scan._scan_file(
                            descriptor,
                            initial,
                            root,
                            (b"AHOI_SYNTHETIC_SECRET_race-contract-0008",),
                            "file-000001",
                        )
            finally:
                os.close(descriptor)


if __name__ == "__main__":
    unittest.main()
