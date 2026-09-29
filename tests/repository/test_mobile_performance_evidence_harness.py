import json
import pathlib
import plistlib
import shlex
import shutil
import subprocess
import tempfile
import unittest
import uuid


ROOT = pathlib.Path(__file__).resolve().parents[2]
MOBILE = ROOT / "apps" / "AhoiMobile"
HARNESS = MOBILE / "scripts" / "capture-performance-evidence.sh"
HELPER = MOBILE / "scripts" / "mobile-performance-evidence-helper.py"
EVALUATOR = MOBILE / "scripts" / "evaluate-performance-evidence.py"
BUDGETS = MOBILE / "performance-budgets.json"
SOURCE_COMMIT = "a" * 40
SAMPLE_KEYS = {
    "sessionReadyMilliseconds",
    "sessionFlushMilliseconds",
    "sessionFlushFailureCounts",
    "livePagesForeground",
    "livePagesBackground",
    "livePagesMemoryWarning",
    "discardMilliseconds",
    "restoreMilliseconds",
    "backgroundPolicyMilliseconds",
    "foregroundRestoreMilliseconds",
}
STAGES = {
    "launch-cold": (5, "idle", "normal", 1, None),
    "launch-warm-cache": (5, "idle", "normal", 1, None),
    "memory-normal-1": (1, "idle", "normal", 1, None),
    "memory-normal-5": (1, "idle", "normal", 5, None),
    "memory-normal-20-discard-restore": (
        1, "discard-restore", "normal", 20, None
    ),
    "memory-private-1": (1, "idle", "private", 1, None),
    "memory-private-5": (1, "idle", "private", 5, None),
    "memory-private-20-discard-restore": (
        1, "discard-restore", "private", 20, None
    ),
    "idle-resources": (1, "idle", "normal", 1, None),
    "idle-network": (3, "idle", "normal", 1, None),
    "controller-pressure-policy": (1, "lifecycle-flush", "normal", 20, None),
    "scroll-motion-standard": (1, "scroll", "normal", 1, "false"),
    "scroll-motion-reduced": (1, "scroll", "normal", 1, "true"),
}


class MobilePerformanceEvidenceHarnessTests(unittest.TestCase):
    def run_harness(self, *arguments: str):
        return subprocess.run(
            [str(HARNESS), *arguments], cwd=ROOT, check=False,
            capture_output=True, text=True,
        )

    def test_script_contract_is_bash_valid_raw_only_and_bounded(self):
        syntax = subprocess.run(
            ["/bin/bash", "-n", str(HARNESS)], cwd=ROOT,
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(0, syntax.returncode, syntax.stderr)
        help_result = self.run_harness("--help")
        self.assertEqual(0, help_result.returncode, help_result.stderr)
        self.assertIn("checksum-indexed trace bundles", help_result.stdout)
        self.assertIn("optimized PerformanceDevelopment", help_result.stdout)
        self.assertIn("accepts no caller-supplied measurements", help_result.stdout)
        self.assertIn("Cold runs reinstall the exact SHA-bound candidate", help_result.stdout)
        self.assertIn("warm runs require a completed", help_result.stdout)
        source = HARNESS.read_text(encoding="utf-8")
        helper_source = HELPER.read_text(encoding="utf-8")
        evaluator_source = EVALUATOR.read_text(encoding="utf-8")
        compile(helper_source, str(HELPER), "exec")
        compile(evaluator_source, str(EVALUATOR), "exec")
        self.assertLessEqual(len(source.splitlines()), 800)
        self.assertLessEqual(len(helper_source.splitlines()), 800)
        self.assertLessEqual(len(evaluator_source.splitlines()), 800)
        self.assertNotIn("--measurements", source + evaluator_source)
        self.assertIn("xctrace export --input", source)
        self.assertIn("--toc", source)
        self.assertIn("--har", source)
        self.assertIn("AhoiOptimizationLevel", source)
        self.assertIn("ad hoc signing is forbidden for a physical candidate", source)
        self.assertIn("host Macs are forbidden", helper_source)

    def test_launch_preparation_is_distinct_installed_and_fail_closed(self):
        source = HARNESS.read_text(encoding="utf-8")
        self.assertIn('ahoi_install_candidate baseline', source)
        self.assertIn('ahoi_uninstall_candidate "$capture_id"', source)
        self.assertIn('ahoi_install_candidate "$capture_id"', source)
        self.assertIn("preparation='clean-install'", source)
        self.assertIn("preparation='completed-prelaunch'", source)
        self.assertIn('validate-workload-marker', source)
        self.assertIn('cmp -s "$AHOI_BINARY_PATH"', source)
        self.assertIn('candidateSourceSHA=%s', source)
        self.assertIn('--launch -- "$AHOI_CAPTURE_LAUNCH_TARGET"', source)
        self.assertNotIn(
            '--launch -- "$AHOI_APP_PATH" "${launch_args[@]}"',
            source,
        )

    def test_output_outside_repository_is_rejected_before_creation(self):
        outside = pathlib.Path(tempfile.gettempdir()) / f"ahoi-forbidden-{uuid.uuid4().hex}"
        completed = self.run_harness(
            "--app", "/missing/AhoiMobile.app",
            "--device-udid", "00000000-0000-0000-0000-000000000000",
            "--output-dir", str(outside),
            "--expected-source-sha", SOURCE_COMMIT,
            "--duration-seconds", "5", "--dry-run",
        )
        self.assertNotEqual(0, completed.returncode)
        self.assertIn("must resolve below", completed.stderr)
        self.assertFalse(outside.exists())

    def test_schema_two_marker_is_exact_stage_bound_and_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix=".mobile-marker-v2-", dir=ROOT) as raw:
            marker = pathlib.Path(raw) / "marker.json"
            payload = self.marker_payload("launch-cold", "nonce", "idle", False)
            payload["samples"]["sessionReadyMilliseconds"] = [100]
            marker.write_text(json.dumps(payload), encoding="utf-8")
            valid = self.validate_marker(marker, "launch-cold", "nonce", "idle")
            self.assertEqual(0, valid.returncode, valid.stderr)

            invalid_payloads = []
            schema_one = dict(payload)
            schema_one["schemaVersion"] = 1
            invalid_payloads.append(schema_one)
            extra = dict(payload)
            extra["rawURL"] = "https://private.example"
            invalid_payloads.append(extra)
            unauthorized = self.marker_payload("idle-resources", "nonce", "idle", False)
            unauthorized["samples"]["sessionFlushFailureCounts"] = [0]
            invalid_payloads.append(unauthorized)
            wrong_motion = self.marker_payload("scroll-motion-reduced", "nonce", "scroll", False)
            invalid_payloads.append(wrong_motion)
            for index, invalid in enumerate(invalid_payloads):
                with self.subTest(index=index):
                    marker.write_text(json.dumps(invalid), encoding="utf-8")
                    rejected = self.validate_marker(
                        marker,
                        invalid.get("scenario", "launch-cold"),
                        "nonce",
                        invalid.get("workload", "idle"),
                    )
                    self.assertNotEqual(0, rejected.returncode)

    def test_dry_run_plans_all_raw_stages_with_distinct_motion_modes(self):
        device_udid = self.available_ios_simulator_udid()
        if device_udid is None:
            self.skipTest("No available iOS Simulator is installed")
        with tempfile.TemporaryDirectory(prefix=".mobile-performance-v2-", dir=ROOT) as raw:
            temporary = pathlib.Path(raw)
            app = self.make_ios_simulator_app(temporary)
            output = temporary / "evidence"
            completed = self.run_harness(
                "--app", str(app), "--device-udid", device_udid,
                "--output-dir", str(output), "--expected-source-sha", SOURCE_COMMIT,
                "--duration-seconds", "5", "--dry-run",
            )
            self.assertEqual(0, completed.returncode, completed.stderr + completed.stdout)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(2, manifest["schemaVersion"])
            self.assertEqual("DRY_RUN_NOT_PROFILED", manifest["status"])
            self.assertEqual("NOT_EVALUATED", manifest["evaluation"]["status"])
            self.assertEqual(
                "RED_PENDING_RAW_BUDGET_EVALUATION", manifest["releaseGate"]["status"]
            )
            self.assertFalse(manifest["releaseGate"]["acceptedAsFeaturePass"])
            self.assertEqual("DebugLocal", manifest["candidate"]["buildMode"])
            self.assertEqual("-Onone", manifest["candidate"]["optimizationLevel"])
            self.assertEqual("simulator", manifest["device"]["kind"])
            self.assertEqual(str(HELPER.resolve()), manifest["toolchain"]["helperPath"])
            self.assertEqual(str(EVALUATOR.resolve()), manifest["toolchain"]["evaluatorPath"])
            self.assertEqual(str(BUDGETS.resolve()), manifest["toolchain"]["budgetsPath"])
            self.assertEqual(
                "metadata/preparations.tsv",
                manifest["preparationEvidence"]["path"],
            )
            self.assertEqual(sum(item[0] for item in STAGES.values()), len(manifest["captures"]))
            by_stage = {}
            nonces = set()
            for capture in manifest["captures"]:
                by_stage.setdefault(capture["stageSlug"], []).append(capture)
                self.assertEqual("PLANNED_NOT_CAPTURED", capture["status"])
                self.assertIsNone(capture["dataExport"])
                self.assertIsNone(capture["marker"])
                self.assertIsNone(capture["hostSample"])
                self.assertNotIn(capture["scenarioNonce"], nonces)
                nonces.add(capture["scenarioNonce"])
                self.assertEqual(SOURCE_COMMIT, capture["scenarioNonce"][:40])
                plan = output / capture["trace"]["path"]
                argv = self.plan_argv(plan)
                plan_lines = plan.read_text(encoding="utf-8").splitlines()
                expected_preparation = (
                    "clean-install" if capture["stageSlug"] == "launch-cold" else
                    "completed-prelaunch" if capture["stageSlug"] == "launch-warm-cache" else
                    "retained-install"
                )
                self.assertIn(f"preparation={expected_preparation}", plan_lines)
                self.assertIn(f"candidateSourceSHA={SOURCE_COMMIT}", plan_lines)
                separator = argv.index("--", argv.index("--launch") + 1)
                self.assertEqual(str(app), argv[separator + 1])
                launch_arguments = argv[separator + 2:]
                self.assert_launch_arguments(capture["stageSlug"], launch_arguments)
            self.assertEqual(set(STAGES), set(by_stage))
            for slug, contract in STAGES.items():
                self.assertEqual(contract[0], len(by_stage[slug]))
            self.assertFalse(any((output / "traces").iterdir()))
            checksum = subprocess.run(
                ["shasum", "-a", "256", "-c", "artifacts.sha256"], cwd=output,
                check=False, capture_output=True, text=True,
            )
            self.assertEqual(0, checksum.returncode, checksum.stderr + checksum.stdout)

    def test_source_mode_and_optimization_mismatches_fail_before_capture(self):
        cases = (
            ({"build_mode": "DebugLocal", "optimization": None}, SOURCE_COMMIT,
             "plist-AhoiOptimizationLevel"),
            ({"build_mode": "TestFlightBootstrap", "optimization": "-O"}, SOURCE_COMMIT,
             "candidate lacks DEBUG performance workload support"),
            ({"build_mode": "DebugLocal", "optimization": "-Onone"}, "b" * 40,
             "does not match --expected-source-sha"),
        )
        for index, (settings, expected_sha, expected_error) in enumerate(cases):
            with self.subTest(index=index), tempfile.TemporaryDirectory(
                prefix=".mobile-performance-invalid-", dir=ROOT
            ) as raw:
                temporary = pathlib.Path(raw)
                app = self.make_ios_simulator_app(temporary, **settings)
                output = temporary / "evidence"
                completed = self.run_harness(
                    "--app", str(app),
                    "--device-udid", "00000000-0000-0000-0000-000000000000",
                    "--output-dir", str(output), "--expected-source-sha", expected_sha,
                    "--duration-seconds", "5", "--validation-only",
                )
                self.assertNotEqual(0, completed.returncode)
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                self.assertEqual("FAILED", manifest["status"])
                self.assertIn(expected_error, "\n".join(manifest["errors"]))
                self.assertFalse(any((output / "traces").iterdir()))

    def test_host_macho_with_ios_plist_is_never_accepted(self):
        with tempfile.TemporaryDirectory(prefix=".mobile-performance-host-", dir=ROOT) as raw:
            temporary = pathlib.Path(raw)
            app = self.make_host_macho_app(temporary)
            output = temporary / "evidence"
            completed = self.run_harness(
                "--app", str(app),
                "--device-udid", "00000000-0000-0000-0000-000000000000",
                "--output-dir", str(output), "--expected-source-sha", SOURCE_COMMIT,
                "--duration-seconds", "5", "--validation-only",
            )
            self.assertNotEqual(0, completed.returncode)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual("FAILED", manifest["status"])
            self.assertIn("binary and plist platform declarations do not match iOS", "\n".join(manifest["errors"]))

    @staticmethod
    def marker_payload(scenario: str, nonce: str, workload: str, reduced: bool):
        return {
            "schemaVersion": 2,
            "scenario": scenario,
            "nonce": nonce,
            "workload": workload,
            "status": "completed",
            "reduceMotion": reduced,
            "normalTabCount": 1,
            "privateTabCount": 0,
            "livePageCount": 1,
            "samples": {key: [] for key in SAMPLE_KEYS},
        }

    @staticmethod
    def validate_marker(path: pathlib.Path, scenario: str, nonce: str, workload: str):
        return subprocess.run(
            ["python3", str(HELPER), "validate-workload-marker", str(path),
             scenario, nonce, workload], cwd=ROOT, capture_output=True,
            text=True, check=False,
        )

    @staticmethod
    def plan_argv(path: pathlib.Path) -> list[str]:
        line = next(
            item for item in path.read_text(encoding="utf-8").splitlines()
            if item.startswith("argv=")
        )
        return shlex.split(line.removeprefix("argv="))

    def assert_launch_arguments(self, slug: str, arguments: list[str]):
        _, workload, mode, count, motion = STAGES[slug]
        prefix = ["-AhoiUITestFixture"]
        if mode == "normal":
            prefix += ["-AhoiUITestNormalTabCount", str(count)]
        else:
            prefix += ["-AhoiUITestPrivateTabCount", str(count), "-AhoiUITestSelectPrivate"]
        self.assertEqual(prefix, arguments[:len(prefix)], slug)
        offset = len(prefix)
        self.assertEqual(["-AhoiPerformanceWorkload", workload], arguments[offset:offset + 2])
        self.assertEqual(["-AhoiPerformanceEvidenceScenario", slug], arguments[offset + 2:offset + 4])
        self.assertEqual("-AhoiPerformanceEvidenceNonce", arguments[offset + 4])
        self.assertTrue(arguments[offset + 5].startswith(SOURCE_COMMIT + "-"))
        self.assertEqual(
            ["-AhoiPerformanceEvidenceMarker", f"ahoi-performance-{slug}.json"],
            arguments[offset + 6:offset + 8],
        )
        tail = arguments[offset + 8:]
        if motion is None:
            self.assertEqual([], tail)
        else:
            self.assertEqual(["-AhoiPerformanceReduceMotionOverride", motion], tail)

    @staticmethod
    def available_ios_simulator_udid():
        completed = subprocess.run(
            ["xcrun", "simctl", "list", "devices", "--json"],
            check=False, capture_output=True, text=True,
        )
        if completed.returncode:
            return None
        payload = json.loads(completed.stdout)
        for runtime, devices in payload.get("devices", {}).items():
            if "iOS" not in runtime:
                continue
            for device in devices:
                if device.get("isAvailable"):
                    return device.get("udid")
        return None

    @classmethod
    def make_ios_simulator_app(
        cls,
        root: pathlib.Path,
        *,
        build_mode: str = "DebugLocal",
        optimization: str | None = "-Onone",
    ):
        sdk = cls.command_stdout("xcrun", "--sdk", "iphonesimulator", "--show-sdk-path")
        clang = cls.command_stdout("xcrun", "--sdk", "iphonesimulator", "--find", "clang")
        app = root / "AhoiMobileTest.app"
        app.mkdir()
        executable = app / "AhoiMobileTest"
        source = root / "main.c"
        source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
        compiled = subprocess.run(
            [clang, "-target", "arm64-apple-ios26.0-simulator", "-isysroot", sdk,
             str(source), "-o", str(executable)],
            check=False, capture_output=True, text=True,
        )
        if compiled.returncode:
            raise unittest.SkipTest("Could not compile simulator fixture: " + compiled.stderr)
        cls.write_test_info_plist(
            app, executable.name, build_mode=build_mode, optimization=optimization
        )
        signed = subprocess.run(
            ["codesign", "--force", "--sign", "-", "--timestamp=none", str(app)],
            check=False, capture_output=True, text=True,
        )
        if signed.returncode:
            raise unittest.SkipTest("Could not sign simulator fixture: " + signed.stderr)
        return app

    @classmethod
    def make_host_macho_app(cls, root: pathlib.Path):
        source = pathlib.Path("/usr/bin/true")
        if not source.exists():
            raise unittest.SkipTest("Host Mach-O fixture is unavailable")
        app = root / "AhoiMobileTest.app"
        app.mkdir()
        executable = app / "AhoiMobileTest"
        shutil.copyfile(source, executable)
        executable.chmod(0o755)
        cls.write_test_info_plist(app, executable.name)
        return app

    @staticmethod
    def write_test_info_plist(
        app: pathlib.Path,
        executable_name: str,
        *,
        build_mode: str = "DebugLocal",
        optimization: str | None = "-Onone",
    ):
        payload = {
            "CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser",
            "CFBundleExecutable": executable_name,
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": "1.0",
            "CFBundleVersion": "1",
            "CFBundleSupportedPlatforms": ["iPhoneSimulator"],
            "DTPlatformName": "iphonesimulator",
            "UIDeviceFamily": [1, 2],
            "MinimumOSVersion": "26.0",
            "AhoiSourceCommit": SOURCE_COMMIT,
            "AhoiBuildMode": build_mode,
        }
        if optimization is not None:
            payload["AhoiOptimizationLevel"] = optimization
        (app / "Info.plist").write_bytes(plistlib.dumps(payload, sort_keys=True))

    @staticmethod
    def command_stdout(*arguments: str):
        completed = subprocess.run(
            list(arguments), check=False, capture_output=True, text=True,
        )
        if completed.returncode or not completed.stdout.strip():
            raise unittest.SkipTest(
                f"Required fixture tool failed ({' '.join(arguments)}): {completed.stderr}"
            )
        return completed.stdout.strip()


if __name__ == "__main__":
    unittest.main()
