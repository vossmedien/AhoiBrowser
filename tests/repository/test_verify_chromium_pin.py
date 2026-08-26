import base64
import copy
import json
import os
import pathlib
import subprocess
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
VERIFIER = ROOT / "tools/verify_chromium_pin.py"
PIN_PATH = ROOT / "config/chromium.json"


class ChromiumPinVerifierTests(unittest.TestCase):
    def setUp(self):
        self.pin = json.loads(PIN_PATH.read_text(encoding="utf-8"))

    def write_inputs(self, directory: pathlib.Path, pin=None, annotated=True):
        pin = copy.deepcopy(pin or self.pin)
        paths = {
            "config": directory / "chromium.json",
            "remote_refs": directory / "remote-refs.txt",
            "tag_ref_json": directory / "tag-ref.json",
            "branch_ref_json": directory / "branch-ref.json",
            "commit_json": directory / "commit.json",
            "branch_point_json": directory / "branch-point.json",
            "version_text": directory / "version.txt",
            "release_json": directory / "release.json",
        }
        paths["config"].write_text(json.dumps(pin), encoding="utf-8")

        tag_lines = []
        if annotated:
            tag_lines.extend(
                (
                    f"{'a' * 40}\t{pin['tag']}",
                    f"{pin['commit']}\t{pin['tag']}^{{}}",
                )
            )
        else:
            tag_lines.append(f"{pin['commit']}\t{pin['tag']}")
        tag_lines.append(f"{'b' * 40}\trefs/branch-heads/{pin['branchHead']}")
        paths["remote_refs"].write_text("\n".join(tag_lines) + "\n", encoding="utf-8")
        paths["tag_ref_json"].write_text(
            ")]}'\n" + json.dumps({"commit": pin["commit"]}),
            encoding="utf-8",
        )
        paths["branch_ref_json"].write_text(
            ")]}'\n" + json.dumps({"commit": "b" * 40}),
            encoding="utf-8",
        )

        commit_message = "\n".join(
            (
                "Release change",
                "",
                "Cr-Commit-Position: "
                f"refs/branch-heads/{pin['branchHead']}@{{#{pin['branchHeadPosition']}}}",
                "Cr-Branched-From: "
                f"{pin['branchPoint']}-refs/heads/main@{{#{pin['branchPosition']}}}",
                "",
            )
        )
        paths["commit_json"].write_text(
            ")]}'\n" + json.dumps({"commit": pin["commit"], "message": commit_message}),
            encoding="utf-8",
        )
        paths["branch_point_json"].write_text(
            json.dumps(
                {
                    "commit": pin["branchPoint"],
                    "message": "Branch point\n\nCr-Commit-Position: "
                    f"refs/heads/main@{{#{pin['branchPosition']}}}\n",
                }
            ),
            encoding="utf-8",
        )
        version = pin["version"].split(".")
        version_payload = "\n".join(
            f"{key}={value}"
            for key, value in zip(("MAJOR", "MINOR", "BUILD", "PATCH"), version)
        )
        paths["version_text"].write_bytes(
            base64.b64encode(version_payload.encode("utf-8")) + b"\n"
        )
        paths["release_json"].write_text(
            json.dumps(
                {
                    "releases": [
                        {
                            "version": pin["version"],
                            "fraction": pin["rolloutFraction"],
                            "pinnable": pin["pinnable"],
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        return paths

    def run_verifier(self, paths):
        return subprocess.run(
            [
                "python3",
                str(VERIFIER),
                "--config",
                str(paths["config"]),
                "--remote-refs",
                str(paths["remote_refs"]),
                "--commit-json",
                str(paths["commit_json"]),
                "--branch-point-json",
                str(paths["branch_point_json"]),
                "--version-text",
                str(paths["version_text"]),
                "--release-json",
                str(paths["release_json"]),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_accepts_annotated_tag_peeled_to_exact_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.run_verifier(self.write_inputs(pathlib.Path(temporary)))
        self.assertEqual("", result.stderr)
        self.assertEqual(0, result.returncode)

    def test_accepts_lightweight_tag_pointing_to_exact_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.write_inputs(pathlib.Path(temporary), annotated=False)
            result = self.run_verifier(paths)
        self.assertEqual("", result.stderr)
        self.assertEqual(0, result.returncode)

    def test_accepts_one_eligible_among_same_version_rollout_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.write_inputs(pathlib.Path(temporary))
            eligible = {
                "version": self.pin["version"],
                "fraction": self.pin["rolloutFraction"],
                "pinnable": self.pin["pinnable"],
            }
            paths["release_json"].write_text(
                json.dumps(
                    {
                        "releases": [
                            {
                                "version": self.pin["version"],
                                "fraction": 0.5,
                                "pinnable": False,
                            },
                            eligible,
                            {
                                "version": self.pin["version"],
                                "fraction": 1.0,
                                "pinnable": False,
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )
            result = self.run_verifier(paths)
        self.assertEqual("", result.stderr)
        self.assertEqual(0, result.returncode)

    def test_rejects_zero_eligible_same_version_release_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.write_inputs(pathlib.Path(temporary))
            paths["release_json"].write_text(
                json.dumps(
                    {
                        "releases": [
                            {
                                "version": self.pin["version"],
                                "fraction": 0.5,
                                "pinnable": False,
                            },
                            {
                                "version": self.pin["version"],
                                "fraction": 1.0,
                                "pinnable": False,
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("found 0 among 2 same-version records", result.stderr)

    def test_rejects_exact_duplicate_eligible_release_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.write_inputs(pathlib.Path(temporary))
            eligible = {
                "version": self.pin["version"],
                "fraction": self.pin["rolloutFraction"],
                "pinnable": self.pin["pinnable"],
            }
            paths["release_json"].write_text(
                json.dumps({"releases": [eligible, copy.deepcopy(eligible)]}),
                encoding="utf-8",
            )
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("found 2 among 2 same-version records", result.stderr)

    def test_rejects_fully_rolled_pinnable_release_that_has_ended(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.write_inputs(pathlib.Path(temporary))
            paths["release_json"].write_text(
                json.dumps(
                    {
                        "releases": [
                            {
                                "version": self.pin["version"],
                                "fraction": self.pin["rolloutFraction"],
                                "pinnable": self.pin["pinnable"],
                                "serving": {
                                    "endTime": "2026-08-25T12:00:00Z"
                                },
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("found 0 among 1 same-version records", result.stderr)

    def test_rejects_same_version_with_wrong_commit_even_when_other_payloads_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            paths = self.write_inputs(directory)
            wrong_pin = copy.deepcopy(self.pin)
            wrong_pin["commit"] = "c" * 40
            paths["config"].write_text(json.dumps(wrong_pin), encoding="utf-8")
            paths["commit_json"].write_text(
                json.dumps(
                    {
                        "commit": wrong_pin["commit"],
                        "message": "Release change\n\nCr-Commit-Position: "
                        f"refs/branch-heads/{wrong_pin['branchHead']}"
                        f"@{{#{wrong_pin['branchHeadPosition']}}}\n"
                        "Cr-Branched-From: "
                        f"{wrong_pin['branchPoint']}-refs/heads/main"
                        f"@{{#{wrong_pin['branchPosition']}}}\n",
                    }
                ),
                encoding="utf-8",
            )
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("tag target mismatch", result.stderr)

    def test_rejects_branch_head_position_not_proven_by_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            paths = self.write_inputs(directory)
            payload = json.loads(paths["commit_json"].read_text(encoding="utf-8").split("\n", 1)[1])
            configured = f"#{self.pin['branchHeadPosition']}"
            payload["message"] = payload["message"].replace(
                configured, f"#{self.pin['branchHeadPosition'] + 1}"
            )
            paths["commit_json"].write_text(json.dumps(payload), encoding="utf-8")
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("Cr-Commit-Position mismatch", result.stderr)

    def test_rejects_branch_point_not_bound_from_release_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            paths = self.write_inputs(directory)
            payload = json.loads(paths["commit_json"].read_text(encoding="utf-8").split("\n", 1)[1])
            payload["message"] = payload["message"].replace(self.pin["branchPoint"], "d" * 40)
            paths["commit_json"].write_text(json.dumps(payload), encoding="utf-8")
            result = self.run_verifier(paths)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("Cr-Branched-From", result.stderr)

    def test_rejects_unofficial_source_before_response_files_are_needed(self):
        with tempfile.TemporaryDirectory() as temporary:
            config_path = pathlib.Path(temporary) / "chromium.json"
            wrong_pin = copy.deepcopy(self.pin)
            wrong_pin["source"] = "https://example.test/chromium.git"
            config_path.write_text(json.dumps(wrong_pin), encoding="utf-8")
            result = subprocess.run(
                [
                    "python3",
                    str(VERIFIER),
                    "--config",
                    str(config_path),
                    "--validate-config-only",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertNotEqual(0, result.returncode)
        self.assertIn("official Chromium repository", result.stderr)

    def test_remote_ref_lookup_is_hard_bounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            config_path = directory / "chromium.json"
            config_path.write_text(json.dumps(self.pin), encoding="utf-8")
            fake_git = directory / "git"
            fake_git.write_text("#!/bin/sh\nsleep 30\n", encoding="utf-8")
            fake_git.chmod(0o755)
            environment = dict(os.environ)
            environment["PATH"] = f"{directory}:{environment['PATH']}"
            started = time.monotonic()
            result = subprocess.run(
                [
                    "python3",
                    str(VERIFIER),
                    "--config",
                    str(config_path),
                    "--resolve-remote-refs",
                    str(directory / "refs.txt"),
                    "--network-timeout",
                    "1",
                ],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )
            elapsed = time.monotonic() - started
        self.assertNotEqual(0, result.returncode)
        self.assertLess(elapsed, 5)
        self.assertIn("timed out after 1 seconds", result.stderr)

    def test_gitiles_ref_resolution_binds_exact_tag_and_branch(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            paths = self.write_inputs(directory)
            output = directory / "gitiles-refs.txt"
            result = subprocess.run(
                [
                    "python3",
                    str(VERIFIER),
                    "--config",
                    str(paths["config"]),
                    "--tag-ref-json",
                    str(paths["tag_ref_json"]),
                    "--branch-ref-json",
                    str(paths["branch_ref_json"]),
                    "--resolve-gitiles-refs",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            rendered = output.read_text(encoding="utf-8") if output.exists() else ""
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn(self.pin["commit"], rendered)

    def test_gitiles_ref_resolution_rejects_same_version_wrong_tag_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            paths = self.write_inputs(directory)
            paths["tag_ref_json"].write_text(
                json.dumps({"commit": "c" * 40}), encoding="utf-8"
            )
            result = subprocess.run(
                [
                    "python3",
                    str(VERIFIER),
                    "--config",
                    str(paths["config"]),
                    "--tag-ref-json",
                    str(paths["tag_ref_json"]),
                    "--branch-ref-json",
                    str(paths["branch_ref_json"]),
                    "--resolve-gitiles-refs",
                    str(directory / "refs.txt"),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertNotEqual(0, result.returncode)
        self.assertIn("tag target mismatch", result.stderr)

    def test_online_script_validates_and_resolves_refs_before_curl(self):
        script = (ROOT / "scripts/verify-pin-online.sh").read_text(encoding="utf-8")
        self.assertLess(script.index("--validate-config-only"), script.index("curl_official"))
        self.assertIn("--resolve-gitiles-refs", script)
        self.assertIn("+show/refs/tags/${version}?format=JSON", script)
        self.assertIn("+show/refs/branch-heads/", script)
        self.assertIn("--connect-timeout 10 --max-time 45", script)


if __name__ == "__main__":
    unittest.main()
