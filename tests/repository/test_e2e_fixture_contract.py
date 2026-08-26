import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class GeneralE2EFixtureRepositoryTests(unittest.TestCase):
    def test_fixture_is_wired_into_repository_gate(self):
        gate = (ROOT / "scripts/test-repository.sh").read_text(encoding="utf-8")
        self.assertIn("python3 fixtures/e2e/manage.py run-tests", gate)

    def test_wrapper_exposes_explicit_trust_lifecycle(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/ahoi_e2e_fixture.py"), "--help"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        for command in (
            "generate-certificates",
            "trust-install",
            "trust-remove",
            "start",
            "stop",
            "cleanup",
            "run-tests",
        ):
            with self.subTest(command=command):
                self.assertIn(command, result.stdout)

    def test_documentation_contains_exact_consent_and_cleanup_phrases(self):
        readme = (ROOT / "fixtures/e2e/README.md").read_text(encoding="utf-8")
        self.assertIn("I-understand-this-adds-a-local-test-CA", readme)
        self.assertIn("remove-the-local-test-CA", readme)
        self.assertIn("never invokes either trust-changing command", readme)
        self.assertIn("TLS validation enabled", readme)

    def test_required_fixture_assets_exist(self):
        required = (
            "fixtures/e2e/certificates.py",
            "fixtures/e2e/manage.py",
            "fixtures/e2e/pages.py",
            "fixtures/e2e/receipts.py",
            "fixtures/e2e/server.py",
            "fixtures/e2e/assets/h264-aac.mp4.b64",
            "fixtures/e2e/tests/test_e2e_fixture.py",
        )
        self.assertEqual([], [path for path in required if not (ROOT / path).is_file()])


if __name__ == "__main__":
    unittest.main()
