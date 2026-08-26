import base64
import json
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ROLL = ROOT / "tools/chromium_roll.py"
PIN = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))


def run(*args: str, cwd: pathlib.Path = ROOT):
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def commit(checkout: pathlib.Path, message: str) -> str:
    run("git", "add", "-A", cwd=checkout)
    result = run("git", "commit", "-q", "-m", message, cwd=checkout)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return run("git", "rev-parse", "HEAD", cwd=checkout).stdout.strip()


class ChromiumRollDiscoveryTests(unittest.TestCase):
    VERSION = "152.0.7977.65"
    COMMIT = "1" * 40
    BRANCH_COMMIT = "2" * 40
    BRANCH_POINT = "3" * 40
    BRANCH_POSITION = 1669021
    BRANCH_HEAD_POSITION = 411

    def fixtures(self, root: pathlib.Path):
        lower = "151.0.7922.175"
        releases = {
            "releases": [
                {
                    "version": self.VERSION,
                    "fraction": 0.005,
                    "pinnable": False,
                },
                {
                    "version": self.VERSION,
                    "fraction": 1,
                    "pinnable": True,
                },
                {
                    "version": self.VERSION,
                    "fraction": 1,
                    "pinnable": True,
                    "serving": {"endTime": "2026-08-25T12:00:00Z"},
                },
                {
                    "version": self.VERSION,
                    "fraction": 0.005,
                    "pinnable": False,
                },
                {"version": lower, "fraction": 1, "pinnable": True},
            ]
        }
        dash = [
            {
                "channel": "Stable",
                "platform": "Mac",
                "version": self.VERSION,
                "milestone": 152,
                "chromium_main_branch_position": self.BRANCH_POSITION,
                "hashes": {"chromium": self.COMMIT},
            },
            {
                "channel": "Stable",
                "platform": "Mac",
                "version": lower,
                "milestone": 151,
                "chromium_main_branch_position": 1,
                "hashes": {"chromium": "4" * 40},
            },
        ]
        message = (
            "Release\n\n"
            "Cr-Commit-Position: refs/branch-heads/7977@"
            f"{{#{self.BRANCH_HEAD_POSITION}}}\n"
            f"Cr-Branched-From: {self.BRANCH_POINT}-refs/heads/main@"
            f"{{#{self.BRANCH_POSITION}}}\n"
        )
        point_message = (
            "Branch point\n\nCr-Commit-Position: refs/heads/main@"
            f"{{#{self.BRANCH_POSITION}}}\n"
        )
        values = {
            "release-json": releases,
            "dash-json": dash,
            "tag-ref-json": {"commit": self.COMMIT},
            "branch-ref-json": {"commit": self.BRANCH_COMMIT},
            "commit-json": {"commit": self.COMMIT, "message": message},
            "branch-point-json": {
                "commit": self.BRANCH_POINT,
                "message": point_message,
            },
        }
        paths = {}
        for option, payload in values.items():
            path = root / f"{option}.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            paths[option] = path
        version = "MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=65\n"
        version_path = root / "version.txt"
        version_path.write_bytes(base64.b64encode(version.encode("utf-8")))
        paths["version-text"] = version_path
        return paths

    def invoke(self, paths, *extra: str):
        command = ["python3", str(ROLL), "discover"]
        for option, path in paths.items():
            command.extend((f"--{option}", str(path)))
        command.extend(("--retrieved-at", "2026-08-26T12:00:00Z", *extra))
        return run(*command)

    def test_selects_highest_unique_eligible_release_and_tolerates_rollout_duplicates(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-discovery-") as raw:
            result = self.invoke(self.fixtures(pathlib.Path(raw)))
        self.assertEqual("", result.stderr)
        self.assertEqual(0, result.returncode)
        candidate = json.loads(result.stdout)
        self.assertEqual(self.VERSION, candidate["version"])
        self.assertEqual(self.COMMIT, candidate["commit"])
        self.assertEqual(7977, candidate["branchHead"])
        self.assertEqual(self.BRANCH_HEAD_POSITION, candidate["branchHeadPosition"])
        self.assertEqual(self.BRANCH_POINT, candidate["branchPoint"])
        self.assertEqual(self.BRANCH_POSITION, candidate["branchPosition"])

    def test_rejects_dash_metadata_that_disagrees_with_gitiles(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-discovery-") as raw:
            paths = self.fixtures(pathlib.Path(raw))
            dash = json.loads(paths["dash-json"].read_text(encoding="utf-8"))
            dash[0]["hashes"]["chromium"] = "9" * 40
            paths["dash-json"].write_text(json.dumps(dash), encoding="utf-8")
            result = self.invoke(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("Dash commit does not match", result.stderr)

    def test_offline_is_default_and_missing_fixtures_fail_cleanly(self):
        result = run("python3", str(ROLL), "discover")
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("offline discovery requires", result.stderr)

    def test_refuses_to_emit_a_candidate_over_production_config(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-discovery-") as raw:
            result = self.invoke(
                self.fixtures(pathlib.Path(raw)), "--output", str(ROOT / "config/chromium.json")
            )
        self.assertNotEqual(0, result.returncode)
        self.assertIn("refusing to overwrite production", result.stderr)
        self.assertEqual(PIN["commit"], json.loads((ROOT / "config/chromium.json").read_text())["commit"])

    def test_refuses_symlink_output_without_changing_its_target(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-discovery-") as raw:
            root = pathlib.Path(raw)
            target = root / "target.json"
            target.write_text("preserve me\n", encoding="utf-8")
            output = root / "candidate.json"
            output.symlink_to(target)
            result = self.invoke(
                self.fixtures(root), "--output", str(output)
            )
            self.assertTrue(output.is_symlink())
            self.assertEqual("preserve me\n", target.read_text(encoding="utf-8"))
        self.assertNotEqual(0, result.returncode)
        self.assertIn("symlink output", result.stderr)


class ChromiumRollPreflightTests(unittest.TestCase):
    def make_fixture(self, root: pathlib.Path):
        repository = root / "repository"
        checkout = root / "chromium"
        (repository / "config").mkdir(parents=True)
        (repository / "overlay/chromium/src").mkdir(parents=True)
        (repository / "patches/chromium").mkdir(parents=True)
        checkout.mkdir()
        run("git", "init", "-q", cwd=checkout)
        run("git", "config", "user.name", "Ahoi Test", cwd=checkout)
        run("git", "config", "user.email", "test@example.invalid", cwd=checkout)
        (checkout / "base.txt").write_text("base\n", encoding="utf-8")
        (checkout / "overlay.txt").write_text("upstream\n", encoding="utf-8")
        (checkout / "upstream.txt").write_text("new\n", encoding="utf-8")
        target = commit(checkout, "target")
        (checkout / "base.txt").write_text("dirty but preserved\n", encoding="utf-8")
        (checkout / "untracked.txt").write_text("also preserved\n", encoding="utf-8")
        (repository / "config/chromium.json").write_text(
            json.dumps(PIN), encoding="utf-8"
        )
        (repository / "overlay/chromium/src/overlay.txt").write_text(
            "Ahoi overlay\n", encoding="utf-8"
        )
        patches = repository / "patches/chromium"
        (patches / "applies.patch").write_text(
            """diff --git a/base.txt b/base.txt
--- a/base.txt
+++ b/base.txt
@@ -1 +1 @@
-base
+patched
""",
            encoding="utf-8",
        )
        (patches / "upstream.patch").write_text(
            """diff --git a/upstream.txt b/upstream.txt
--- a/upstream.txt
+++ b/upstream.txt
@@ -1 +1 @@
-old
+new
""",
            encoding="utf-8",
        )
        (patches / "conflict.patch").write_text(
            """diff --git a/missing.txt b/missing.txt
--- a/missing.txt
+++ b/missing.txt
@@ -1 +1 @@
-absent
+changed
""",
            encoding="utf-8",
        )
        (patches / "series").write_text(
            "applies.patch\nupstream.patch\nconflict.patch\n", encoding="utf-8"
        )
        return repository, checkout, target

    def invoke(self, repository: pathlib.Path, checkout: pathlib.Path, target: str):
        return run(
            "python3",
            str(ROLL),
            "preflight",
            "--repository",
            str(repository),
            "--checkout",
            str(checkout),
            "--target",
            target,
        )

    def test_classifies_apply_already_upstream_conflict_and_preserves_checkout(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as raw:
            repository, checkout, target = self.make_fixture(pathlib.Path(raw))
            clean_path = checkout / "overlay.txt"
            clean_stat = clean_path.stat()
            os.utime(
                clean_path,
                ns=(clean_stat.st_atime_ns, clean_stat.st_mtime_ns + 2_000_000_000),
            )
            head_before = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            index_before = (checkout / ".git/index").read_bytes()
            status_before = run(
                "git", "--no-optional-locks", "status", "--porcelain", cwd=checkout
            ).stdout
            base_before = (checkout / "base.txt").read_text(encoding="utf-8")
            result = self.invoke(repository, checkout, target)
            head_after = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            status_after = run(
                "git", "--no-optional-locks", "status", "--porcelain", cwd=checkout
            ).stdout
            report = json.loads(result.stdout)
            rendered = json.dumps(report)

            self.assertEqual(2, result.returncode)
            self.assertEqual(
                ["applies", "already_upstream", "conflict"],
                [patch["classification"] for patch in report["patches"]],
            )
            self.assertEqual(["overlay.txt"], report["overlay"]["collisions"])
            self.assertTrue(report["mutationGuard"]["unchanged"])
            self.assertFalse(report["ready"])
            self.assertNotIn(str(repository), rendered)
            self.assertNotIn(str(checkout), rendered)
            self.assertEqual(head_before, head_after)
            self.assertEqual(status_before, status_after)
            self.assertEqual(index_before, (checkout / ".git/index").read_bytes())
            self.assertEqual(base_before, (checkout / "base.txt").read_text(encoding="utf-8"))
            self.assertEqual("also preserved\n", (checkout / "untracked.txt").read_text())

    def test_overlay_fingerprint_binds_executable_mode(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as raw:
            repository, checkout, target = self.make_fixture(pathlib.Path(raw))
            (repository / "patches/chromium/series").write_text(
                "applies.patch\n", encoding="utf-8"
            )
            overlay = repository / "overlay/chromium/src/overlay.txt"
            overlay.chmod(0o644)
            first = self.invoke(repository, checkout, target)
            overlay.chmod(0o755)
            second = self.invoke(repository, checkout, target)
        self.assertEqual(0, first.returncode, first.stderr)
        self.assertEqual(0, second.returncode, second.stderr)
        first_report = json.loads(first.stdout)
        second_report = json.loads(second.stdout)
        self.assertNotEqual(
            first_report["overlay"]["sha256"],
            second_report["overlay"]["sha256"],
        )
        self.assertNotEqual(first_report["resultTree"], second_report["resultTree"])

    def test_all_applying_series_is_ready(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as raw:
            repository, checkout, target = self.make_fixture(pathlib.Path(raw))
            (repository / "patches/chromium/series").write_text(
                "applies.patch\n", encoding="utf-8"
            )
            result = self.invoke(repository, checkout, target)
        self.assertEqual("", result.stderr)
        self.assertEqual(0, result.returncode)
        self.assertTrue(json.loads(result.stdout)["ready"])

    def test_missing_target_object_fails_cleanly_without_checkout_mutation(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as raw:
            repository, checkout, _ = self.make_fixture(pathlib.Path(raw))
            status_before = run("git", "status", "--porcelain", cwd=checkout).stdout
            result = self.invoke(repository, checkout, "f" * 40)
            status_after = run("git", "status", "--porcelain", cwd=checkout).stdout
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("not available locally", result.stderr)
        self.assertEqual(status_before, status_after)

    def test_revision_expressions_are_rejected_instead_of_evaluated(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as raw:
            repository, checkout, _ = self.make_fixture(pathlib.Path(raw))
            result = self.invoke(repository, checkout, "HEAD~1")
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("target revision is unsafe", result.stderr)


if __name__ == "__main__":
    unittest.main()
