import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/perf"))
import build_evidence as be
import run_desktop_perf as runner
from perf import optimization_receipt as optimization


class BuildEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.app = self.root / "Candidate.app"
        self.app.mkdir()
        (self.app / "framework").write_bytes(b"the actual engine")
        self.args = (b'target_os = "mac"\ntarget_cpu = "arm64"\nis_debug = false\n'
                     b'is_component_build = false\nis_official_build = true\n')
        self.commit = "c" * 40
        self.pin = {"commit": "d" * 40, "version": "153.0.8010.53"}
        self.toolchain = {"mode": "pinned-reference", "xcodeVersion": "27.0",
                          "xcodeBuild": "27A266a", "macOSSDKBuild": "26A425"}
        self.pins = {"xcode": {"requiredVersion": "27.0", "requiredBuild": "27A266a"},
                     "sdks": {"macOS": {"chromiumOfficialBuild": "26A425"}},
                     "buildTools": {"clangBinarySha256": "e" * 64,
                                    "lldBinarySha256": "f" * 64}}
        self.identity = {"path": str(self.app), "binarySha256": "a" * 64,
                         "bundleIdentifier": "app.ahoibrowser.AhoiBrowser",
                         "chromiumVersion": self.pin["version"],
                         "chromiumCommit": self.pin["commit"],
                         "sourceCommit": self.commit, "buildProfile": "release",
                         "gnArgsSha256": be.sha(self.args)}
        self.receipt = {
            "schemaVersion": 2, "kind": "ahoi-release",
            "app": {**self.identity, "bundleTreeSha256": be.tree_sha256(self.app)},
            "source": {"repositoryCommit": self.commit, "repositoryDirty": False,
                       "overlayApplied": True, "chromiumCommit": self.pin["commit"],
                       "chromiumVersion": self.pin["version"], "overlayFingerprint": "b" * 64,
                       "depotToolsCommit": "e" * 40, "dependencyBuildWorkarounds": []},
            "build": {"gnArgsSha256": be.sha(self.args),
                      "generatedGnArgsSha256": be.sha(self.args),
                      "clang": {"binarySha256": "e" * 64}, "lld": {"binarySha256": "f" * 64},
                      "effectiveOptimization": {"chromePgoPhase": 2, "useThinLto": True,
                          "pgoProfile": {"target": "mac-arm", "name": "test.profdata",
                                         "sha256": "9" * 64, "matchesChromiumPin": True}}},
            "toolchain": self.toolchain,
        }
        self.receipt["engineInputKey"] = be.key_of(be.receipt_components(self.receipt))
        self.path = self.root / "receipt.json"
        self.source = mock.patch.object(be, "source_file", side_effect=self.read_source)
        self.source.start()
        self.addCleanup(self.source.stop)

    def read_source(self, repository, commit, path):
        self.assertEqual(commit, self.commit)
        if path.startswith("config/build/"):
            return self.args
        return json.dumps({"config/chromium.json": self.pin,
                           "config/toolchain.json": self.pins}[path]).encode()

    def verify(self, receipt=None):
        self.path.write_text(json.dumps(receipt or self.receipt))
        return be.verify(self.identity, self.path, self.root)

    def test_full_bundle_and_receipt_binding(self):
        proof = self.verify()
        self.assertTrue(proof["verified"])
        self.assertTrue(proof["budgetEligible"])
        self.assertEqual(proof["receiptSha256"], be.sha(self.path.read_bytes()))
        self.assertEqual(proof["comparison"]["toolchain"], self.toolchain)

    def test_framework_change_is_not_hidden_by_unchanged_launcher(self):
        (self.app / "framework").write_bytes(b"a different engine")
        with self.assertRaisesRegex(be.EvidenceError, "bundle tree"):
            self.verify()

    def test_missing_optimization_is_insufficient_not_verified_as_release_budget(self):
        self.receipt["build"].pop("effectiveOptimization")
        proof = self.verify()
        self.assertTrue(proof["verified"])
        self.assertFalse(proof["budgetEligible"])
        self.assertIn("optimization", proof["reason"])

    def test_refuses_development_dirty_mismatched_or_unpinned_receipts(self):
        edits = [(("kind",), "ahoi-dev"), (("source", "repositoryDirty"), True),
                 (("source", "overlayApplied"), False),
                 (("source", "chromiumCommit"), "0" * 40),
                 (("app", "binarySha256"), "0" * 64),
                 (("build", "gnArgsSha256"), "0" * 64),
                 (("build", "generatedGnArgsSha256"), "0" * 64),
                 (("engineInputKey",), "0" * 64),
                 (("toolchain", "xcodeBuild"), "wrong")]
        for keys, value in edits:
            receipt = copy.deepcopy(self.receipt)
            target = receipt
            for key in keys[:-1]:
                target = target[key]
            target[keys[-1]] = value
            with self.subTest(keys=keys), self.assertRaises(be.EvidenceError):
                self.verify(receipt)

    def test_configuration_is_read_from_receipt_revision(self):
        with mock.patch.object(be, "source_file", side_effect=self.read_source) as read:
            self.verify()
            self.assertEqual({call.args[1] for call in read.call_args_list}, {self.commit})

    def test_rejects_component_build_even_when_receipt_calls_it_release(self):
        self.args = self.args.replace(b"is_component_build = false", b"is_component_build = true")
        self.receipt["build"]["gnArgsSha256"] = be.sha(self.args)
        self.receipt["build"]["generatedGnArgsSha256"] = be.sha(self.args)
        with self.assertRaisesRegex(be.EvidenceError, "component"):
            self.verify()

    def test_upstream_requires_unmodified_source_and_chromium_identity(self):
        self.receipt["kind"] = "unmodified-upstream-control"
        self.receipt["source"]["overlayApplied"] = False
        for obj in (self.identity, self.receipt["app"]):
            obj["bundleIdentifier"] = "org.chromium.Chromium"
        self.assertTrue(self.verify()["budgetEligible"])
        self.receipt["source"]["overlayApplied"] = True
        with self.assertRaises(be.EvidenceError):
            self.verify()

    def test_missing_receipt_refuses_before_any_browser_or_fixture_launch(self):
        with mock.patch.object(runner, "app_identity", return_value=self.identity), \
                mock.patch.object(runner, "FixtureServer") as fixtures, \
                mock.patch.object(runner, "Browser") as browser, \
                mock.patch.object(runner, "preflight") as host:
            rc = runner.main(["--app", str(self.app), "--scenario", "startup",
                              "--output", str(self.root / "output")])
        self.assertEqual(rc, 7)
        fixtures.assert_not_called()
        browser.assert_not_called()
        host.assert_not_called()

    def test_runner_preserves_verified_receipt_in_sample_artifact(self):
        self.verify()  # Write a real fixture receipt, bound to the tiny bundle.
        destination = self.root / "output"
        conditions = {"hardwareModel": "Fixture", "osBuild": "test", "powerSource": "ac",
                      "accessibilityClients": []}
        with mock.patch.object(runner, "app_identity", return_value=self.identity), \
                mock.patch.object(runner, "FixtureServer") as fixtures, \
                mock.patch.object(runner, "host_conditions", return_value=conditions), \
                mock.patch.object(runner, "preflight", return_value=conditions), \
                mock.patch.object(runner.runtime_guard, "LeaseGuard") as guard, \
                mock.patch.dict(runner.SCENARIOS, {"startup": mock.Mock(
                    return_value={"startup_warm_ms": 1.0})}):
            guard.return_value.summary.return_value = {"completed": True, "cancelled": False}
            self.assertEqual(runner.main([
                "--app", str(self.app), "--build-receipt", str(self.path),
                "--scenario", "startup", "--runs", "1", "--output", str(destination)]), 0)
        artifact = json.loads((destination / "candidate-run.json").read_text())
        self.assertEqual(artifact["schemaVersion"], 2)
        self.assertEqual(artifact["buildEvidence"]["receiptSha256"], be.sha(self.path.read_bytes()))
        self.assertEqual(artifact["metrics"]["startup_warm_ms"], [1.0])
        fixtures.return_value.close.assert_called_once()

    def test_optimization_requires_pinned_profile_and_effective_flags(self):
        valid = self.receipt["build"]["effectiveOptimization"]
        for value in (None, {}, {**valid, "chromePgoPhase": 0},
                      {**valid, "useThinLto": "true"}, {**valid, "pgoProfile": {}},
                      {**valid, "pgoProfile": {**valid["pgoProfile"], "matchesChromiumPin": False}}):
            with self.subTest(value=value):
                self.assertIsNotNone(be.optimization_problem(value))


class ImmutableSourceTest(unittest.TestCase):
    def test_git_reader_ignores_modified_worktree_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            def git(*args):
                return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()
            git("init", "-q")
            path = root / "config.gn"
            path.write_text("is_component_build = false\n")
            git("add", "config.gn")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.test",
                "-c", "commit.gpgsign=false", "commit", "-qm", "Fixture")
            commit = git("rev-parse", "HEAD")
            path.write_text("is_component_build = true\n")
            self.assertEqual(be.source_file(root, commit, "config.gn"),
                             b"is_component_build = false\n")


class OptimizationReceiptTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.src = pathlib.Path(self.temp.name)
        self.out = self.src / "out/Release"
        self.out.mkdir(parents=True)
        self.name = "test.profdata"
        self.profile = self.src / "chrome/build/pgo_profiles" / self.name
        self.profile.parent.mkdir(parents=True)
        self.profile.write_bytes(b"profile from the pinned download")
        self.pointer = self.src / "chrome/build/mac-arm.pgo.txt"
        self.pointer.write_text(self.name + "\n")
        self.args = ('chrome_pgo_phase = 2\nuse_thin_lto = true\n'
                     'target_os = "mac"\ntarget_cpu = "arm64"\n')
        self.flags = '  -fprofile-use=../../chrome/build/pgo_profiles/test.profdata\n'

    def collect(self):
        with mock.patch.object(optimization.subprocess, "check_output",
                               side_effect=[self.args, self.flags]) as command, \
                mock.patch.object(optimization, "source_file",
                                  return_value=(self.name + "\n").encode()):
            result = optimization.collect(self.src, self.out, self.src / "gn", "c" * 40)
        self.assertEqual(command.call_args_list[0].args[0][-2:], ["--list", "--short"])
        self.assertEqual(command.call_args_list[1].args[0][-2:],
                         ["//build/config/compiler/pgo:pgo_optimization_flags", "cflags"])
        return result

    def test_captures_effective_defaults_and_hashes_the_compiler_profile(self):
        result = self.collect()
        self.assertEqual(result["chromePgoPhase"], 2)
        self.assertTrue(result["useThinLto"])
        self.assertEqual(result["pgoProfile"]["sha256"], be.sha(self.profile.read_bytes()))
        self.assertNotIn(str(self.src), json.dumps(result))

    def test_refuses_custom_or_missing_profile_and_changed_pointer(self):
        original = self.flags
        for flags in ("", "-fprofile-use=/elsewhere/test.profdata\n", original + original):
            self.flags = flags
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                self.collect()
        self.flags = original
        self.pointer.write_text("different.profdata\n")
        with self.assertRaisesRegex(ValueError, "pointer differs"):
            self.collect()

    def test_refuses_development_or_unknown_effective_values(self):
        for args in (self.args.replace("phase = 2", "phase = 0"),
                     self.args.replace("use_thin_lto = true\n", ""),
                     self.args.replace('target_cpu = "arm64"', 'target_cpu = "x64"')):
            self.args = args
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.collect()


if __name__ == "__main__":
    unittest.main()
