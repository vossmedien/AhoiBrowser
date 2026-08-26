import base64
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ROLL = ROOT / "tools/chromium_roll.py"
PIN = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
sys.path.insert(0, str(ROOT / "tools"))

from chromium_roll_hydration import (  # noqa: E402
    HydrationError,
    MAX_TARGET_PATHS,
    fetch_gitiles_response,
    gitiles_blob_url,
    hydrate_target_blobs,
)


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
            json.dumps({**PIN, "commit": target}), encoding="utf-8"
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


class ChromiumRollHydrationTests(unittest.TestCase):
    def make_fixture(self, root: pathlib.Path):
        repository = root / "repository"
        checkout = root / "chromium"
        responses = root / "responses"
        (repository / "config").mkdir(parents=True)
        (repository / "patches/chromium").mkdir(parents=True)
        responses.mkdir()
        checkout.mkdir()
        run("git", "init", "-q", cwd=checkout)
        run("git", "config", "user.name", "Ahoi Test", cwd=checkout)
        run("git", "config", "user.email", "test@example.invalid", cwd=checkout)
        (checkout / "a.txt").write_text("target a\n", encoding="utf-8")
        (checkout / "existing.txt").write_text("existing\n", encoding="utf-8")
        (checkout / "DEPS").write_text("target deps\n", encoding="utf-8")
        (checkout / "dir").mkdir()
        (checkout / "dir/child.txt").write_text("target child\n", encoding="utf-8")
        (checkout / "dir/sibling.txt").write_text(
            "target sibling\n", encoding="utf-8"
        )
        target = commit(checkout, "target")
        object_ids = {
            path: run("git", "rev-parse", f"{target}:{path}", cwd=checkout).stdout.strip()
            for path in (
                "a.txt",
                "existing.txt",
                "DEPS",
                "dir/child.txt",
                "dir/sibling.txt",
            )
        }
        contents = {
            "a.txt": b"target a\n",
            "existing.txt": b"existing\n",
            "DEPS": b"target deps\n",
            "dir/child.txt": b"target child\n",
            "dir/sibling.txt": b"target sibling\n",
        }
        (checkout / "a.txt").write_text("current a\n", encoding="utf-8")
        (checkout / "DEPS").write_text("current deps\n", encoding="utf-8")
        (checkout / "current.txt").write_text("current only\n", encoding="utf-8")
        (checkout / "dir/child.txt").write_text("current child\n", encoding="utf-8")
        (checkout / "dir/sibling.txt").write_text(
            "current sibling\n", encoding="utf-8"
        )
        commit(checkout, "current")
        (repository / "config/chromium.json").write_text(
            json.dumps({**PIN, "commit": target}), encoding="utf-8"
        )
        patches = repository / "patches/chromium"
        (patches / "product.patch").write_text(
            """diff --git a/a.txt b/a.txt
--- a/a.txt
+++ b/a.txt
@@ -1 +1 @@
-target a
+patched a
diff --git a/existing.txt b/existing.txt
--- a/existing.txt
+++ b/existing.txt
@@ -1 +1 @@
-existing
+patched existing
""",
            encoding="utf-8",
        )
        (patches / "series").write_text("product.patch\n", encoding="utf-8")
        for path in ("a.txt", "DEPS", "dir/child.txt", "dir/sibling.txt"):
            oid = object_ids[path]
            if path != "dir/child.txt":
                (responses / f"{oid}.b64").write_bytes(
                    base64.b64encode(contents[path])
                )
            loose = checkout / ".git/objects" / oid[:2] / oid[2:]
            self.assertTrue(loose.is_file())
            loose.unlink()
        (checkout / "untracked.txt").write_text("preserve\n", encoding="utf-8")
        tracked = checkout / "current.txt"
        tracked_stat = tracked.stat()
        os.utime(
            tracked,
            ns=(tracked_stat.st_atime_ns, tracked_stat.st_mtime_ns + 2_000_000_000),
        )
        return repository, checkout, responses, target, object_ids

    def invoke(
        self,
        repository: pathlib.Path,
        checkout: pathlib.Path,
        responses: pathlib.Path,
        target: str,
        *extra: str,
    ):
        return run(
            "python3",
            str(ROLL),
            "hydrate",
            "--repository",
            str(repository),
            "--checkout",
            str(checkout),
            "--target",
            target,
            "--offline-response-directory",
            str(responses),
            *extra,
        )

    def missing(self, checkout: pathlib.Path, oid: str) -> bool:
        environment = {**os.environ, "GIT_NO_LAZY_FETCH": "1"}
        result = subprocess.run(
            ("git", "cat-file", "-e", oid),
            cwd=checkout,
            env=environment,
            check=False,
            capture_output=True,
        )
        return result.returncode != 0

    def test_only_missing_patch_blob_is_written_and_report_is_portable_deterministic(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-hydrate-") as raw:
            root = pathlib.Path(raw)
            repository, checkout, responses, target, object_ids = self.make_fixture(root)
            index_before = (checkout / ".git/index").read_bytes()
            head_before = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            first = self.invoke(repository, checkout, responses, target)
            index_after = (checkout / ".git/index").read_bytes()
            report = json.loads(first.stdout)
            hydrated = checkout / ".git/objects" / object_ids["a.txt"][:2] / object_ids["a.txt"][2:]
            hydrated.unlink()
            second = self.invoke(repository, checkout, responses, target)

            self.assertEqual(0, first.returncode, first.stderr)
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(1, report["summary"]["requestedBlobCount"])
            self.assertEqual(1, report["summary"]["hydratedBlobCount"])
            self.assertFalse(self.missing(checkout, object_ids["a.txt"]))
            self.assertTrue(self.missing(checkout, object_ids["DEPS"]))
            self.assertEqual(index_before, index_after)
            self.assertEqual(head_before, run("git", "rev-parse", "HEAD", cwd=checkout).stdout)
            self.assertEqual("preserve\n", (checkout / "untracked.txt").read_text())
            self.assertTrue(report["mutationGuard"]["unchanged"])
            self.assertNotIn(str(root), first.stdout)

    def test_include_path_hydrates_an_explicit_target_blob(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-hydrate-") as raw:
            repository, checkout, responses, target, object_ids = self.make_fixture(
                pathlib.Path(raw)
            )
            result = self.invoke(
                repository, checkout, responses, target, "--include-path", "DEPS"
            )
            unsafe = self.invoke(
                repository, checkout, responses, target, "--include-path", "../DEPS"
            )
        self.assertEqual(0, result.returncode, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(["DEPS"], report["inputs"]["includePaths"])
        self.assertEqual(2, report["summary"]["requestedBlobCount"])
        self.assertEqual(2, report["summary"]["hydratedBlobCount"])
        self.assertNotEqual(0, unsafe.returncode)
        self.assertNotIn("Traceback", unsafe.stderr)
        self.assertIn("unsafe hydration path", unsafe.stderr)

    def test_directory_include_is_bounded_and_reported_without_descendant_fetch(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-hydrate-") as raw:
            repository, checkout, responses, target, object_ids = self.make_fixture(
                pathlib.Path(raw)
            )
            patches = repository / "patches/chromium"
            (patches / "child.patch").write_text(
                """diff --git a/dir/child.txt b/dir/child.txt
--- a/dir/child.txt
+++ b/dir/child.txt
@@ -1 +1 @@
-target child
+patched child
""",
                encoding="utf-8",
            )
            (patches / "series").write_text(
                "product.patch\nchild.patch\n", encoding="utf-8"
            )
            child_oid = object_ids["dir/child.txt"]
            (responses / f"{child_oid}.b64").write_bytes(
                base64.b64encode(b"target child\n")
            )
            result = self.invoke(
                repository, checkout, responses, target, "--include-path", "dir"
            )
            child_missing = self.missing(checkout, child_oid)
            sibling_missing = self.missing(checkout, object_ids["dir/sibling.txt"])
        self.assertEqual(0, result.returncode, result.stderr)
        report = json.loads(result.stdout)
        directory = next(item for item in report["paths"] if item["path"] == "dir")
        self.assertEqual("tree", directory["targetType"])
        self.assertEqual("non_blob", directory["disposition"])
        self.assertFalse(child_missing)
        self.assertTrue(sibling_missing)

    def test_unbound_target_and_checkout_output_fail_before_blob_writes(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-hydrate-") as raw:
            repository, checkout, responses, target, object_ids = self.make_fixture(
                pathlib.Path(raw)
            )
            output = checkout / ".git/index"
            unsafe_output = self.invoke(
                repository,
                checkout,
                responses,
                target,
                "--output",
                str(output),
            )
            self.assertTrue(self.missing(checkout, object_ids["a.txt"]))
            (repository / "config/chromium.json").write_text(
                json.dumps({**PIN, "commit": "f" * 40}), encoding="utf-8"
            )
            unbound = self.invoke(repository, checkout, responses, target)
            still_missing = self.missing(checkout, object_ids["a.txt"])
        self.assertNotEqual(0, unsafe_output.returncode)
        self.assertIn("inside the Chromium checkout", unsafe_output.stderr)
        self.assertNotEqual(0, unbound.returncode)
        self.assertIn("target is not bound", unbound.stderr)
        self.assertTrue(still_missing)

    def test_wrong_hash_and_aggregate_overflow_write_no_blobs(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-hydrate-") as raw:
            root = pathlib.Path(raw)
            repository, checkout, responses, target, object_ids = self.make_fixture(root)
            (responses / f"{object_ids['a.txt']}.b64").write_bytes(
                base64.b64encode(b"wrong\n")
            )
            wrong = self.invoke(repository, checkout, responses, target)
            self.assertTrue(self.missing(checkout, object_ids["a.txt"]))
            self.assertNotIn("Traceback", wrong.stderr)
            self.assertIn("hash mismatch", wrong.stderr)

            (responses / f"{object_ids['a.txt']}.b64").write_bytes(
                base64.b64encode(b"target a\n")
            )
            aggregate = self.invoke(
                repository,
                checkout,
                responses,
                target,
                "--include-path",
                "DEPS",
                "--max-response-bytes",
                "16",
                "--max-total-response-bytes",
                "20",
            )
            self.assertTrue(self.missing(checkout, object_ids["a.txt"]))
            self.assertTrue(self.missing(checkout, object_ids["DEPS"]))
        self.assertNotEqual(0, wrong.returncode)
        self.assertNotEqual(0, aggregate.returncode)
        self.assertNotIn("Traceback", aggregate.stderr)
        self.assertIn("aggregate response limit", aggregate.stderr)

    def test_url_encoding_official_source_redirect_and_response_bound(self):
        target = "a" * 40
        url = gitiles_blob_url(target, "dir/a b?#%.cc")
        self.assertTrue(url.endswith("/dir/a%20b%3F%23%25.cc?format=TEXT"))
        with self.assertRaises(HydrationError):
            gitiles_blob_url(target, "../DEPS")
        with self.assertRaises(HydrationError):
            fetch_gitiles_response("http://example.invalid/blob", 5, 8)

        class Response:
            def __init__(self, final_url: str, payload: bytes):
                self.final_url = final_url
                self.payload = payload

            def __enter__(self):
                return self

            def __exit__(self, *unused):
                del unused

            def geturl(self):
                return self.final_url

            def getcode(self):
                return 200

            def read(self, maximum):
                return self.payload[:maximum]

        class Opener:
            def __init__(self, response):
                self.response = response

            def open(self, request, timeout):
                del request, timeout
                return self.response

        with self.assertRaisesRegex(HydrationError, "redirected"):
            fetch_gitiles_response(
                url, 5, 8, opener=Opener(Response(url + "&redirected=1", b"YQ=="))
            )
        with self.assertRaisesRegex(HydrationError, "per-response"):
            fetch_gitiles_response(
                url, 5, 4, opener=Opener(Response(url, b"YWFhYWFh"))
            )

    def test_path_bound_and_partial_promotion_have_explicit_resume_semantics(self):
        contents = {"a": b"one\n", "b": b"two\n"}

        def blob_oid(payload):
            framed = b"blob " + str(len(payload)).encode() + b"\0" + payload
            return hashlib.sha1(framed, usedforsecurity=False).hexdigest()

        object_ids = {name: blob_oid(payload) for name, payload in contents.items()}
        present = set()
        writes = 0

        def fake_git(command, input_bytes=None, check=True):
            nonlocal writes
            del check
            if command[0] == "ls-tree":
                return b"".join(
                    f"100644 blob {object_ids[name]}\t{name}\0".encode()
                    for name in ("a", "b")
                )
            if command[0] == "cat-file":
                return b"".join(
                    (
                        f"{oid} blob {len(contents[name])}\n"
                        if oid in present
                        else f"{oid} missing\n"
                    ).encode()
                    for name, oid in object_ids.items()
                )
            if command[0] == "hash-object":
                writes += 1
                oid = blob_oid(input_bytes)
                if writes == 2:
                    raise RuntimeError("simulated object-store failure")
                present.add(oid)
                return f"{oid}\n".encode()
            raise AssertionError(command)

        def response(target, path, oid, timeout, maximum):
            del target, oid, timeout, maximum
            return base64.b64encode(contents[path])

        with self.assertRaisesRegex(HydrationError, "rerun safely resumes"):
            hydrate_target_blobs(
                git=fake_git,
                target="a" * 40,
                touched_paths=["a", "b"],
                load_response=response,
                timeout=5,
                total_timeout=30,
                max_response_bytes=32,
                max_total_response_bytes=64,
            )
        self.assertEqual({object_ids["a"]}, present)

        with self.assertRaisesRegex(HydrationError, "path safety bound"):
            hydrate_target_blobs(
                git=fake_git,
                target="a" * 40,
                touched_paths=[f"p/{index}" for index in range(MAX_TARGET_PATHS + 1)],
                load_response=response,
                timeout=5,
                total_timeout=30,
                max_response_bytes=32,
                max_total_response_bytes=64,
            )


if __name__ == "__main__":
    unittest.main()
