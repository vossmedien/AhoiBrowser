import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/chromium_checkout_hydration.py"
sys.path.insert(0, str(ROOT / "tools"))

from chromium_checkout_hydration import (  # noqa: E402
    EXIT_INCOMPLETE,
    EXIT_MUTATION,
    FetchResult,
    _fetch_object_ids,
    checkout_snapshot,
    changed_guard_fields,
    fetch_adaptively,
    fetch_command,
    run_hydration,
)


def run(*arguments: str, cwd: pathlib.Path = ROOT):
    return subprocess.run(
        arguments,
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )


def git(checkout: pathlib.Path, *arguments: str) -> str:
    result = run("git", *arguments, cwd=checkout)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.strip()


def commit(checkout: pathlib.Path, message: str) -> str:
    git(checkout, "add", "-A")
    git(checkout, "commit", "-q", "-m", message)
    return git(checkout, "rev-parse", "HEAD")


class CheckoutFixture:
    def __init__(self, root: pathlib.Path, *, remove_blobs: bool = True):
        self.repository = root / "repository"
        self.checkout = root / "chromium"
        (self.repository / "config").mkdir(parents=True)
        (self.repository / "artifacts/build").mkdir(parents=True)
        self.checkout.mkdir()
        git(self.checkout, "init", "-q")
        git(self.checkout, "config", "user.name", "Ahoi Test")
        git(self.checkout, "config", "user.email", "test@example.invalid")

        self.contents = {
            "duplicate": b"same target blob\n",
            "unique": b"unique target blob\n",
        }
        (self.checkout / "a.txt").write_bytes(self.contents["duplicate"])
        (self.checkout / "b.txt").write_bytes(self.contents["duplicate"])
        (self.checkout / "unique.txt").write_bytes(self.contents["unique"])
        self.target = commit(self.checkout, "target")
        self.object_ids = {
            "duplicate": git(self.checkout, "rev-parse", f"{self.target}:a.txt"),
            "unique": git(self.checkout, "rev-parse", f"{self.target}:unique.txt"),
        }

        (self.checkout / "a.txt").write_text("current a\n", encoding="utf-8")
        (self.checkout / "b.txt").write_text("current b\n", encoding="utf-8")
        (self.checkout / "unique.txt").write_text("current unique\n", encoding="utf-8")
        commit(self.checkout, "current")
        git(
            self.checkout,
            "remote",
            "add",
            "origin",
            "https://chromium.googlesource.com/chromium/src.git",
        )
        git(self.checkout, "config", "remote.origin.promisor", "true")
        git(self.checkout, "config", "remote.origin.partialclonefilter", "blob:none")

        pin = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
        pin["commit"] = self.target
        (self.repository / "config/chromium.json").write_text(
            json.dumps(pin), encoding="utf-8"
        )
        self.output = self.repository / "artifacts/build/hydration.json"

        if remove_blobs:
            for oid in self.object_ids.values():
                loose = self.checkout / ".git/objects" / oid[:2] / oid[2:]
                if not loose.is_file():
                    raise AssertionError(f"expected loose fixture object {oid}")
                loose.unlink()

    def restore(self, name: str) -> None:
        result = subprocess.run(
            ("git", "hash-object", "-w", "--stdin"),
            cwd=self.checkout,
            input=self.contents[name],
            capture_output=True,
            check=False,
        )
        if result.returncode:
            raise AssertionError(result.stderr.decode("utf-8", "replace"))
        if result.stdout.decode("ascii").strip() != self.object_ids[name]:
            raise AssertionError("fixture object hash changed")

    def invoke(self, *extra: str):
        return run(
            "python3",
            str(TOOL),
            "--repository",
            str(self.repository),
            "--checkout",
            str(self.checkout),
            "--target",
            self.target,
            "--output",
            str(self.output),
            *extra,
        )

    def namespace(self, **overrides):
        values = {
            "repository": self.repository,
            "checkout": self.checkout,
            "target": self.target,
            "output": self.output,
            "dry_run": False,
            "batch_size": 1,
            "attempts": 1,
            "fetch_timeout": 5,
            "max_fetch_commands": 1,
            "checkpoint_batches": 1,
            "max_blobs": 100,
            "retry_backoff_seconds": 0.0,
        }
        values.update(overrides)
        return argparse.Namespace(**values)


class ChromiumCheckoutHydrationCliTests(unittest.TestCase):
    def test_fetch_workflow_exposes_guarded_opt_in_before_normal_sync(self):
        fetch_script = (ROOT / "scripts/fetch-chromium.sh").read_text(
            encoding="utf-8"
        )
        building = (ROOT / "docs/BUILDING.md").read_text(encoding="utf-8")
        prehydrate = fetch_script.index('if [ "${prehydrate_target}" -eq 1 ]')
        invalidate = fetch_script.index("ahoi_invalidate_hook_state")
        sync = fetch_script.index("gclient sync")

        self.assertIn("--prehydrate-target", fetch_script)
        self.assertTrue(os.access(TOOL, os.X_OK))
        self.assertIn("chromium_checkout_hydration.py", fetch_script)
        self.assertIn("chromium-checkout-hydration.json", fetch_script)
        self.assertIn("GIT_NO_LAZY_FETCH=1", fetch_script)
        self.assertIn("http.version=HTTP/1.1", fetch_script)
        self.assertIn("http.maxRequests=1", fetch_script)
        self.assertIn("--no-write-fetch-head", fetch_script)
        self.assertIn("--filter=blob:none origin --stdin", fetch_script)
        self.assertEqual(2, fetch_script.count('$(checkout_guard)'))
        self.assertLess(fetch_script.index("ahoi_require_clean_git_checkout"), prehydrate)
        self.assertLess(prehydrate, invalidate)
        self.assertLess(invalidate, sync)
        self.assertNotIn("--force", fetch_script)
        self.assertIn("./scripts/fetch-chromium.sh --prehydrate-target", building)
        self.assertIn("resumable", building)

    def test_fetch_option_help_and_rejection_do_not_start_checkout_work(self):
        help_result = run("bash", str(ROOT / "scripts/fetch-chromium.sh"), "--help")
        rejected = run(
            "bash", str(ROOT / "scripts/fetch-chromium.sh"), "--not-a-fetch-option"
        )
        too_many = run(
            "bash",
            str(ROOT / "scripts/fetch-chromium.sh"),
            "--help",
            "unexpected",
        )

        self.assertEqual(0, help_result.returncode, help_result.stderr)
        self.assertIn("--prehydrate-target", help_result.stdout)
        self.assertEqual(1, rejected.returncode)
        self.assertIn("unsupported fetch option", rejected.stderr)
        self.assertEqual(1, too_many.returncode)
        self.assertIn("at most one option", too_many.stderr)
        for result in (help_result, rejected, too_many):
            self.assertNotIn("syncing Chromium", result.stdout + result.stderr)

    def test_dry_run_inventories_unique_missing_blobs_without_mutation(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-hydration-") as raw:
            fixture = CheckoutFixture(pathlib.Path(raw))
            index_before = (fixture.checkout / ".git/index").read_bytes()
            head_before = git(fixture.checkout, "rev-parse", "HEAD")
            result = fixture.invoke("--dry-run")
            report = json.loads(fixture.output.read_text(encoding="utf-8"))

            self.assertEqual(EXIT_INCOMPLETE, result.returncode, result.stderr)
            self.assertEqual("dry_run_incomplete", report["phase"])
            self.assertEqual(3, report["inventory"]["entryCount"])
            self.assertEqual(2, report["inventory"]["uniqueTargetBlobCount"])
            self.assertEqual(2, report["inventory"]["initiallyMissingBlobCount"])
            self.assertEqual(2, report["inventory"]["remainingMissingBlobCount"])
            self.assertTrue(report["target"]["pin"]["verified"])
            self.assertTrue(report["origin"]["verified"])
            self.assertTrue(report["mutationGuard"]["unchanged"])
            self.assertIn("FETCH_HEAD", report["mutationGuard"]["protected"])
            self.assertIn("shallow boundary", report["mutationGuard"]["protected"])
            self.assertEqual(0, report["transport"]["commandCount"])
            self.assertEqual(index_before, (fixture.checkout / ".git/index").read_bytes())
            self.assertEqual(head_before, git(fixture.checkout, "rev-parse", "HEAD"))
            leftovers = list(fixture.output.parent.glob(".ahoi-roll-*.tmp"))
            self.assertEqual([], leftovers)

    def test_rerun_recomputes_inventory_and_skips_already_present_objects(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-resume-") as raw:
            fixture = CheckoutFixture(pathlib.Path(raw))
            first = fixture.invoke("--dry-run")
            first_report = json.loads(fixture.output.read_text(encoding="utf-8"))
            fixture.restore("duplicate")
            second = fixture.invoke("--dry-run")
            second_report = json.loads(fixture.output.read_text(encoding="utf-8"))
            fixture.restore("unique")
            third = fixture.invoke("--dry-run")
            third_report = json.loads(fixture.output.read_text(encoding="utf-8"))

        self.assertEqual(EXIT_INCOMPLETE, first.returncode)
        self.assertEqual(EXIT_INCOMPLETE, second.returncode)
        self.assertEqual(0, third.returncode, third.stderr)
        self.assertEqual(2, first_report["inventory"]["initiallyMissingBlobCount"])
        self.assertEqual(1, second_report["inventory"]["initiallyMissingBlobCount"])
        self.assertEqual(0, third_report["inventory"]["initiallyMissingBlobCount"])
        self.assertEqual("complete", third_report["phase"])
        self.assertTrue(third_report["complete"])
        self.assertNotEqual(
            first_report["inventory"]["initialMissingObjectIdsSha256"],
            second_report["inventory"]["initialMissingObjectIdsSha256"],
        )

    def test_exact_target_pin_official_origin_and_promisor_are_mandatory(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-identity-") as raw:
            root = pathlib.Path(raw)
            fixture = CheckoutFixture(root)
            expression = run(
                "python3",
                str(TOOL),
                "--repository",
                str(fixture.repository),
                "--checkout",
                str(fixture.checkout),
                "--target",
                "HEAD",
                "--dry-run",
            )
            self.assertEqual(1, expression.returncode)
            self.assertIn("exact lowercase", expression.stderr)
            self.assertNotIn("Traceback", expression.stderr)

            pin = json.loads(
                (fixture.repository / "config/chromium.json").read_text(encoding="utf-8")
            )
            pin["commit"] = "f" * 40
            (fixture.repository / "config/chromium.json").write_text(
                json.dumps(pin), encoding="utf-8"
            )
            mismatch = fixture.invoke("--dry-run")
            self.assertEqual(1, mismatch.returncode)
            self.assertIn("does not match the pinned", mismatch.stderr)

            pin["commit"] = fixture.target
            (fixture.repository / "config/chromium.json").write_text(
                json.dumps(pin), encoding="utf-8"
            )
            git(fixture.checkout, "remote", "set-url", "origin", "https://example.invalid/src.git")
            wrong_origin = fixture.invoke("--dry-run")
            self.assertEqual(1, wrong_origin.returncode)
            self.assertIn("official Chromium repository", wrong_origin.stderr)

    def test_symlink_report_is_refused_before_any_fetch(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-output-") as raw:
            fixture = CheckoutFixture(pathlib.Path(raw))
            victim = fixture.repository / "victim.json"
            victim.write_text("preserve\n", encoding="utf-8")
            fixture.output.symlink_to(victim)
            result = fixture.invoke("--dry-run")
            self.assertEqual("preserve\n", victim.read_text(encoding="utf-8"))

        self.assertEqual(1, result.returncode)
        self.assertIn("symlink output", result.stderr)

    def test_fetch_head_mutation_has_dedicated_exit_and_atomic_report(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-guard-") as raw:
            fixture = CheckoutFixture(pathlib.Path(raw))

            def mutating_fetch(object_ids):
                self.assertEqual(1, len(object_ids))
                (fixture.checkout / ".git/FETCH_HEAD").write_text(
                    f"{fixture.target}\t\tfixture\n", encoding="ascii"
                )
                return FetchResult(False, "fixture failure")

            report, exit_code = run_hydration(
                fixture.namespace(), fetcher=mutating_fetch, sleeper=lambda _: None
            )
            saved = json.loads(fixture.output.read_text(encoding="utf-8"))

        self.assertEqual(EXIT_MUTATION, exit_code)
        self.assertEqual("mutation_detected", report["phase"])
        self.assertIn("fetchHeadSha256", report["mutationGuard"]["changedFields"])
        self.assertEqual(report, saved)


class ChromiumCheckoutHydrationAlgorithmTests(unittest.TestCase):
    OIDS = tuple(f"{value:040x}" for value in range(1, 6))

    def test_retry_then_success_avoids_split(self):
        present = set()
        calls = []

        def fetch(object_ids):
            calls.append(tuple(object_ids))
            if len(calls) == 1:
                return FetchResult(False, "transient HTTP/2 framing failure")
            present.update(object_ids)
            return FetchResult(True)

        statistics = fetch_adaptively(
            self.OIDS[:4],
            batch_size=4,
            attempts=3,
            max_fetch_commands=20,
            fetch=fetch,
            missing=lambda values: tuple(value for value in values if value not in present),
            retry_backoff_seconds=0,
        )

        self.assertEqual(2, statistics.command_count)
        self.assertEqual(1, statistics.retry_count)
        self.assertEqual(0, statistics.adaptive_splits)
        self.assertEqual(set(self.OIDS[:4]), statistics.hydrated)

    def test_persistent_batch_failure_is_adaptively_split_to_single_oids(self):
        present = set()
        calls = []

        def fetch(object_ids):
            calls.append(tuple(object_ids))
            if len(object_ids) > 1:
                return FetchResult(False, "batch rejected")
            present.update(object_ids)
            return FetchResult(True)

        statistics = fetch_adaptively(
            self.OIDS[:4],
            batch_size=4,
            attempts=1,
            max_fetch_commands=20,
            fetch=fetch,
            missing=lambda values: tuple(value for value in values if value not in present),
            retry_backoff_seconds=0,
        )

        self.assertEqual(set(self.OIDS[:4]), present)
        self.assertEqual(3, statistics.adaptive_splits)
        self.assertEqual(7, statistics.command_count)
        self.assertEqual({}, statistics.singleton_failures)
        self.assertTrue(any(len(call) == 4 for call in calls))
        self.assertTrue(all(len(call) <= 4 for call in calls))

    def test_command_budget_stops_pathological_recursive_fetches(self):
        statistics = fetch_adaptively(
            self.OIDS,
            batch_size=5,
            attempts=1,
            max_fetch_commands=2,
            fetch=lambda _: FetchResult(False, "offline"),
            missing=lambda values: tuple(values),
            retry_backoff_seconds=0,
        )
        self.assertEqual(2, statistics.command_count)
        self.assertTrue(statistics.command_budget_exhausted)
        self.assertLessEqual(statistics.command_count, 2)

    def test_transport_is_fixed_to_http1_single_request_and_oid_stdin(self):
        command = fetch_command()
        self.assertIn("http.version=HTTP/1.1", command)
        self.assertIn("http.maxRequests=1", command)
        self.assertIn("fetch.parallel=1", command)
        self.assertIn("fetch.negotiationAlgorithm=noop", command)
        self.assertIn("--filter=blob:none", command)
        self.assertEqual(("origin", "--stdin"), command[-2:])
        self.assertIn("--no-write-fetch-head", command)
        self.assertNotIn("--force", command)
        self.assertNotIn("checkout", command)
        self.assertNotIn("reset", command)

        recorded = {}

        class Process:
            pid = 12345
            returncode = 0

            def communicate(self, payload=None, timeout=None):
                recorded["payload"] = payload
                recorded["timeout"] = timeout
                return b"", b""

        def fake_popen(arguments, **kwargs):
            recorded["arguments"] = arguments
            recorded["kwargs"] = kwargs
            return Process()

        with mock.patch("chromium_checkout_hydration.subprocess.Popen", fake_popen):
            result = _fetch_object_ids(
                pathlib.Path("/fixture"), {}, (self.OIDS[0], self.OIDS[1]), 17
            )
        self.assertTrue(result.success)
        self.assertEqual(fetch_command(), recorded["arguments"])
        self.assertEqual(
            f"{self.OIDS[0]}\n{self.OIDS[1]}\n".encode("ascii"),
            recorded["payload"],
        )
        self.assertEqual(17, recorded["timeout"])
        self.assertTrue(recorded["kwargs"]["start_new_session"])

    def test_snapshot_detects_each_declared_git_metadata_guard(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-checkout-snapshot-") as raw:
            fixture = CheckoutFixture(pathlib.Path(raw), remove_blobs=False)
            environment = {
                **os.environ,
                "GIT_NO_LAZY_FETCH": "1",
                "GIT_OPTIONAL_LOCKS": "0",
                "GIT_TERMINAL_PROMPT": "0",
            }
            before = checkout_snapshot(fixture.checkout, environment)
            (fixture.checkout / "a.txt").write_text("guard change\n", encoding="utf-8")
            git(fixture.checkout, "update-ref", "refs/heads/guard-fixture", "HEAD")
            (fixture.checkout / ".git/FETCH_HEAD").write_text(
                f"{fixture.target}\t\tfixture\n", encoding="ascii"
            )
            (fixture.checkout / ".git/shallow").write_text(
                f"{fixture.target}\n", encoding="ascii"
            )
            after = checkout_snapshot(fixture.checkout, environment)
            changed = changed_guard_fields(before, after)

        self.assertIn("worktreeSha256", changed)
        self.assertIn("statusSha256", changed)
        self.assertIn("refsSha256", changed)
        self.assertIn("refCount", changed)
        self.assertIn("fetchHeadSha256", changed)
        self.assertIn("shallowSha256", changed)
        self.assertEqual(before["head"], after["head"])
        self.assertEqual(before["indexSha256"], after["indexSha256"])


if __name__ == "__main__":
    unittest.main()
