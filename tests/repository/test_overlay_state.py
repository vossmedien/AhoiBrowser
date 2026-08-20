import datetime as dt
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from compose_overlay import compose_overlay  # noqa: E402
from overlay_state import (  # noqa: E402
    OverlayStateError,
    current_checkout_tree,
    delta_fingerprint,
    derive_expected_overlay,
    verify_overlay_state,
)


def run(*args: str, cwd: pathlib.Path):
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    )


class OverlayStateTests(unittest.TestCase):
    def create_fixture(self, root: pathlib.Path):
        repository = root / "repository"
        overlay = repository / "overlay/chromium/src"
        patch_root = repository / "patches/chromium"
        overlay.mkdir(parents=True)
        patch_root.mkdir(parents=True)
        (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
        (patch_root / "series").write_text("# no patches\n", encoding="utf-8")

        checkout = root / "checkout"
        checkout.mkdir()
        run("git", "init", "-q", cwd=checkout)
        run("git", "config", "user.name", "Ahoi Test", cwd=checkout)
        run("git", "config", "user.email", "test@example.invalid", cwd=checkout)
        (checkout / "base.txt").write_text("base\n", encoding="utf-8")
        run("git", "add", "base.txt", cwd=checkout)
        run("git", "commit", "-q", "-m", "base", cwd=checkout)
        commit = run("git", "rev-parse", "HEAD", cwd=checkout).stdout.strip()

        combined, _ = compose_overlay(
            checkout,
            overlay,
            patch_root / "series",
            patch_root,
            base_revision=commit,
        )
        combined_path = root / "combined.patch"
        combined_path.write_bytes(combined)
        run("git", "apply", "--whitespace=error-all", str(combined_path), cwd=checkout)
        expected = derive_expected_overlay(repository, checkout, commit)
        state_path = root / "overlay-state.json"
        state = {
            "schemaVersion": 2,
            "fingerprint": expected.input_fingerprint,
            "checkoutDeltaFingerprint": expected.delta_fingerprint,
            "chromiumCommit": commit,
            "appliedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        }
        state_path.write_text(
            json.dumps(state, sort_keys=True) + "\n", encoding="utf-8"
        )
        return repository, checkout, commit, state_path, state

    def test_valid_state_is_bound_to_freshly_recomposed_checkout_tree(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, state = self.create_fixture(
                pathlib.Path(raw_root)
            )

            verified = verify_overlay_state(
                repository, checkout, state_path, commit
            )

            self.assertEqual(state["fingerprint"], verified.input_fingerprint)
            self.assertEqual(
                state["checkoutDeltaFingerprint"],
                verified.checkout_delta_fingerprint,
            )
            self.assertEqual(verified.expected_tree, verified.actual_tree)

    def test_forged_state_cannot_authorize_an_extra_checkout_edit(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, state = self.create_fixture(
                pathlib.Path(raw_root)
            )
            (checkout / "base.txt").write_text("forged extra edit\n", encoding="utf-8")
            forged_tree = current_checkout_tree(checkout, commit)
            state["checkoutDeltaFingerprint"] = delta_fingerprint(commit, forged_tree)
            state_path.write_text(
                json.dumps(state, sort_keys=True) + "\n", encoding="utf-8"
            )

            with self.assertRaisesRegex(
                OverlayStateError, "does not match the deterministic Ahoi overlay"
            ):
                verify_overlay_state(repository, checkout, state_path, commit)
            result = subprocess.run(
                (
                    "python3",
                    str(ROOT / "tools/overlay_state.py"),
                    "verify",
                    "--repository",
                    str(repository),
                    "--checkout",
                    str(checkout),
                    "--state",
                    str(state_path),
                    "--expected-commit",
                    commit,
                ),
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("does not match the deterministic Ahoi overlay", result.stderr)

    def test_state_schema_and_commit_are_strict(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, state = self.create_fixture(
                pathlib.Path(raw_root)
            )
            cases = (
                ({**state, "unexpected": True}, "schema mismatch"),
                ({**state, "schemaVersion": True}, "schemaVersion must be 2"),
                ({**state, "chromiumCommit": "0" * 40}, "does not match the pin"),
            )
            for payload, message in cases:
                with self.subTest(message=message):
                    state_path.write_text(
                        json.dumps(payload, sort_keys=True) + "\n", encoding="utf-8"
                    )
                    with self.assertRaisesRegex(OverlayStateError, message):
                        verify_overlay_state(
                            repository, checkout, state_path, commit
                        )

    def test_build_provenance_uses_recomputed_values_not_raw_state(self):
        source = (ROOT / "tools/build_provenance.py").read_text(encoding="utf-8")
        common = (ROOT / "scripts/lib/common.sh").read_text(encoding="utf-8")
        self.assertIn("verify_overlay_state(", source)
        self.assertIn("overlay_verification.input_fingerprint", source)
        self.assertIn("overlay_verification.checkout_delta_fingerprint", source)
        self.assertNotIn('state["checkoutDeltaFingerprint"]', source)
        self.assertIn('tools/overlay_state.py" verify', common)


if __name__ == "__main__":
    unittest.main()
