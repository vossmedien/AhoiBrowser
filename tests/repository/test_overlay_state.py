import datetime as dt
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from compose_overlay import compose_overlay  # noqa: E402
import overlay_state  # noqa: E402
from overlay_state import (  # noqa: E402
    OverlayStateError,
    apply_overlay_state,
    current_checkout_tree,
    delta_fingerprint,
    derive_expected_overlay,
    refresh_overlay_state,
    restore_overlay_state,
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
    def create_clean_fixture(self, root: pathlib.Path):
        repository = root / "repository"
        overlay = repository / "overlay/chromium/src"
        patch_root = repository / "patches/chromium"
        overlay.mkdir(parents=True)
        patch_root.mkdir(parents=True)
        (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
        (overlay / "remove.txt").write_text("remove me\n", encoding="utf-8")
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
        state_path = root / "overlay-state.json"
        return repository, checkout, commit, state_path

    def create_fixture(self, root: pathlib.Path):
        repository, checkout, commit, state_path = self.create_clean_fixture(root)
        overlay = repository / "overlay/chromium/src"
        patch_root = repository / "patches/chromium"

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

    def test_initial_apply_publishes_state_only_for_the_exact_composed_tree(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path = self.create_clean_fixture(
                pathlib.Path(raw_root)
            )
            base_tree = current_checkout_tree(checkout, commit)

            applied = apply_overlay_state(
                repository, checkout, state_path, commit
            )

            self.assertEqual(base_tree, applied.previous_tree)
            self.assertNotEqual(base_tree, applied.actual_tree)
            self.assertTrue(state_path.is_file())
            self.assertEqual("overlay\n", (checkout / "new.txt").read_text())
            self.assertEqual("remove me\n", (checkout / "remove.txt").read_text())
            verified = verify_overlay_state(
                repository, checkout, state_path, commit
            )
            self.assertEqual(applied.actual_tree, verified.actual_tree)

    def test_initial_state_publish_failure_rolls_back_only_its_tree_delta(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path = self.create_clean_fixture(
                pathlib.Path(raw_root)
            )
            base_tree = current_checkout_tree(checkout, commit)

            with mock.patch(
                "overlay_state._create_overlay_state_atomic",
                side_effect=OSError("synthetic initial state failure"),
            ):
                with self.assertRaisesRegex(
                    OverlayStateError, "exact tree delta was rolled back"
                ):
                    apply_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(base_tree, current_checkout_tree(checkout, commit))
            self.assertFalse(state_path.exists())
            self.assertFalse((checkout / "new.txt").exists())
            self.assertFalse((checkout / "remove.txt").exists())

    def test_initial_interrupt_after_apply_rolls_back_before_returning(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path = self.create_clean_fixture(
                pathlib.Path(raw_root)
            )
            base_tree = current_checkout_tree(checkout, commit)
            real_apply = overlay_state._run_git_apply
            interrupted = False

            def interrupt_after_apply(
                delta, *, checkout, check_only=False, reverse=False
            ):
                nonlocal interrupted
                real_apply(
                    delta,
                    checkout=checkout,
                    check_only=check_only,
                    reverse=reverse,
                )
                if not check_only and not reverse and not interrupted:
                    interrupted = True
                    raise KeyboardInterrupt("synthetic post-apply interrupt")

            with mock.patch(
                "overlay_state._run_git_apply", side_effect=interrupt_after_apply
            ):
                with self.assertRaisesRegex(OverlayStateError, "rolled back"):
                    apply_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(base_tree, current_checkout_tree(checkout, commit))
            self.assertFalse(state_path.exists())

    def test_restore_returns_only_the_verified_overlay_to_its_pinned_base(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            overlay_tree = current_checkout_tree(checkout, commit)
            base_tree = run(
                "git", "rev-parse", f"{commit}^{{tree}}", cwd=checkout
            ).stdout.strip()

            restored = restore_overlay_state(
                repository, checkout, state_path, commit
            )

            self.assertEqual(overlay_tree, restored.previous_tree)
            self.assertEqual(base_tree, restored.actual_tree)
            self.assertEqual(base_tree, current_checkout_tree(checkout, commit))
            self.assertFalse(state_path.exists())
            self.assertFalse((checkout / "new.txt").exists())
            self.assertFalse((checkout / "remove.txt").exists())
            self.assertEqual("base\n", (checkout / "base.txt").read_text())

    def test_restore_state_removal_failure_reapplies_the_exact_overlay(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            overlay_tree = current_checkout_tree(checkout, commit)
            state_before = state_path.read_bytes()

            with mock.patch(
                "overlay_state._remove_exact_overlay_state",
                side_effect=OverlayStateError("synthetic removal failure"),
            ):
                with self.assertRaisesRegex(
                    OverlayStateError, "previously recorded tree was reapplied"
                ):
                    restore_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(overlay_tree, current_checkout_tree(checkout, commit))
            self.assertEqual(state_before, state_path.read_bytes())
            verify_overlay_state(repository, checkout, state_path, commit)

    def test_restore_interrupt_after_reverse_apply_recovers_overlay_and_state(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            overlay_tree = current_checkout_tree(checkout, commit)
            state_before = state_path.read_bytes()
            real_apply = overlay_state._run_git_apply
            interrupted = False

            def interrupt_after_restore(
                delta, *, checkout, check_only=False, reverse=False
            ):
                nonlocal interrupted
                real_apply(
                    delta,
                    checkout=checkout,
                    check_only=check_only,
                    reverse=reverse,
                )
                if not check_only and reverse and not interrupted:
                    interrupted = True
                    raise KeyboardInterrupt("synthetic post-restore interrupt")

            with mock.patch(
                "overlay_state._run_git_apply", side_effect=interrupt_after_restore
            ):
                with self.assertRaisesRegex(OverlayStateError, "was reapplied"):
                    restore_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(overlay_tree, current_checkout_tree(checkout, commit))
            self.assertEqual(state_before, state_path.read_bytes())
            verify_overlay_state(repository, checkout, state_path, commit)

    def test_restore_rejects_a_symlink_state_without_touching_checkout_or_target(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            overlay_tree = current_checkout_tree(checkout, commit)
            target = pathlib.Path(raw_root) / "external-state.json"
            state_path.replace(target)
            state_path.symlink_to(target)
            target_before = target.read_bytes()

            with self.assertRaisesRegex(OverlayStateError, "non-symlink"):
                restore_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(overlay_tree, current_checkout_tree(checkout, commit))
            self.assertTrue(state_path.is_symlink())
            self.assertEqual(target_before, target.read_bytes())

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

    def test_refresh_applies_only_the_verified_old_tree_to_new_tree_delta(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, state = self.create_fixture(
                pathlib.Path(raw_root)
            )
            previous_tree = current_checkout_tree(checkout, commit)
            head_before = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            index_before = run("git", "write-tree", cwd=checkout).stdout
            overlay = repository / "overlay/chromium/src"
            (overlay / "new.txt").write_text("refreshed\n", encoding="utf-8")
            (overlay / "added.txt").write_text("added\n", encoding="utf-8")
            (overlay / "remove.txt").unlink()
            patch_root = repository / "patches/chromium"
            (patch_root / "refresh.patch").write_text(
                """diff --git a/base.txt b/base.txt
--- a/base.txt
+++ b/base.txt
@@ -1 +1 @@
-base
+patched
""",
                encoding="utf-8",
            )
            (patch_root / "series").write_text(
                "refresh.patch\n", encoding="utf-8"
            )

            refreshed = refresh_overlay_state(
                repository, checkout, state_path, commit
            )

            self.assertTrue(refreshed.checkout_changed)
            self.assertTrue(refreshed.state_changed)
            self.assertEqual(previous_tree, refreshed.previous_tree)
            self.assertNotEqual(previous_tree, refreshed.actual_tree)
            self.assertEqual("refreshed\n", (checkout / "new.txt").read_text())
            self.assertEqual("added\n", (checkout / "added.txt").read_text())
            self.assertFalse((checkout / "remove.txt").exists())
            self.assertEqual("patched\n", (checkout / "base.txt").read_text())
            self.assertEqual(
                head_before, run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            )
            self.assertEqual(
                index_before, run("git", "write-tree", cwd=checkout).stdout
            )
            verified = verify_overlay_state(
                repository, checkout, state_path, commit
            )
            self.assertEqual(refreshed.actual_tree, verified.actual_tree)
            self.assertNotEqual(state["fingerprint"], verified.input_fingerprint)

    def test_refresh_refuses_foreign_checkout_edits_before_composition_or_mutation(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            state_before = state_path.read_bytes()
            (repository / "overlay/chromium/src/new.txt").write_text(
                "new overlay\n", encoding="utf-8"
            )
            (checkout / "base.txt").write_text("foreign edit\n", encoding="utf-8")

            with self.assertRaisesRegex(
                OverlayStateError, "previously recorded applied overlay tree"
            ):
                refresh_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual("foreign edit\n", (checkout / "base.txt").read_text())
            self.assertEqual("overlay\n", (checkout / "new.txt").read_text())
            self.assertEqual(state_before, state_path.read_bytes())

    def test_failed_refresh_composition_leaves_checkout_and_state_untouched(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            previous_tree = current_checkout_tree(checkout, commit)
            state_before = state_path.read_bytes()
            patch_root = repository / "patches/chromium"
            (patch_root / "bad.patch").write_text(
                """diff --git a/missing.txt b/missing.txt
--- a/missing.txt
+++ b/missing.txt
@@ -1 +1 @@
-missing
+changed
""",
                encoding="utf-8",
            )
            (patch_root / "series").write_text("bad.patch\n", encoding="utf-8")

            with self.assertRaisesRegex(
                OverlayStateError, "could not compose deterministic overlay"
            ):
                refresh_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(previous_tree, current_checkout_tree(checkout, commit))
            self.assertEqual(state_before, state_path.read_bytes())

    def test_state_publish_failure_rolls_back_only_the_refresh_delta(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            previous_tree = current_checkout_tree(checkout, commit)
            state_before = state_path.read_bytes()
            (repository / "overlay/chromium/src/new.txt").write_text(
                "refreshed\n", encoding="utf-8"
            )

            with mock.patch(
                "overlay_state._write_overlay_state_atomic",
                side_effect=OSError("synthetic state write failure"),
            ):
                with self.assertRaisesRegex(OverlayStateError, "was rolled back"):
                    refresh_overlay_state(repository, checkout, state_path, commit)

            self.assertEqual(previous_tree, current_checkout_tree(checkout, commit))
            self.assertEqual("overlay\n", (checkout / "new.txt").read_text())
            self.assertEqual(state_before, state_path.read_bytes())

    def test_noop_refresh_preserves_state_and_semantic_input_change_updates_only_state(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            state_before = state_path.read_bytes()
            previous_tree = current_checkout_tree(checkout, commit)

            unchanged = refresh_overlay_state(
                repository, checkout, state_path, commit
            )
            self.assertFalse(unchanged.checkout_changed)
            self.assertFalse(unchanged.state_changed)
            self.assertEqual(state_before, state_path.read_bytes())

            (repository / "patches/chromium/series").write_text(
                "# changed comment, same tree\n", encoding="utf-8"
            )
            state_updated = refresh_overlay_state(
                repository, checkout, state_path, commit
            )
            self.assertFalse(state_updated.checkout_changed)
            self.assertTrue(state_updated.state_changed)
            self.assertEqual(previous_tree, current_checkout_tree(checkout, commit))
            self.assertNotEqual(state_before, state_path.read_bytes())
            verify_overlay_state(repository, checkout, state_path, commit)

    def test_refresh_cli_is_the_apply_script_update_path(self):
        with tempfile.TemporaryDirectory() as raw_root:
            repository, checkout, commit, state_path, _ = self.create_fixture(
                pathlib.Path(raw_root)
            )
            (repository / "overlay/chromium/src/new.txt").write_text(
                "cli refresh\n", encoding="utf-8"
            )

            result = run(
                "python3",
                str(ROOT / "tools/overlay_state.py"),
                "refresh",
                "--repository",
                str(repository),
                "--checkout",
                str(checkout),
                "--state",
                str(state_path),
                "--expected-commit",
                commit,
                cwd=ROOT,
            )

            self.assertTrue(result.stdout.startswith("checkout-refreshed "))
            self.assertEqual("cli refresh\n", (checkout / "new.txt").read_text())
            verify_overlay_state(repository, checkout, state_path, commit)

        apply_script = (ROOT / "scripts/apply-overlay.sh").read_text(
            encoding="utf-8"
        )
        restore_script = (ROOT / "scripts/restore-overlay.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn('tools/overlay_state.py" refresh', apply_script)
        self.assertIn('tools/overlay_state.py" apply', apply_script)
        self.assertIn("checkout-refreshed)", apply_script)
        self.assertIn("ahoi_invalidate_hook_state", apply_script)
        self.assertNotIn("git -C \"${AHOI_CHROMIUM_SRC}\" apply", apply_script)
        self.assertIn('tools/overlay_state.py" restore', restore_script)
        self.assertIn("checkout-restored\\ *)", restore_script)
        self.assertIn("ahoi_invalidate_hook_state", restore_script)
        self.assertLess(
            apply_script.index('tools/overlay_state.py" refresh'),
            apply_script.index('ahoi_require_hook_state "clean"'),
        )

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
