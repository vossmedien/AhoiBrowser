import hashlib
import json
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MOBILE = ROOT / "apps" / "AhoiMobile"
EVALUATOR = MOBILE / "scripts" / "evaluate-performance-evidence.py"
HELPER = MOBILE / "scripts" / "mobile-performance-evidence-helper.py"
HARNESS = MOBILE / "scripts" / "capture-performance-evidence.sh"
BUDGETS = MOBILE / "performance-budgets.json"
SOURCE_COMMIT = subprocess.check_output(
    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
).strip()
XCTRACE = pathlib.Path(
    subprocess.check_output(["/usr/bin/xcrun", "--find", "xctrace"], text=True).strip()
)

STAGES = {
    "launch-cold": ("App Launch", "life-cycle-period", "idle", False, 5),
    "launch-warm-cache": ("App Launch", "life-cycle-period", "idle", False, 5),
    "memory-normal-1": ("Activity Monitor", "sysmon-process", "idle", False, 1),
    "memory-normal-5": ("Activity Monitor", "sysmon-process", "idle", False, 1),
    "memory-normal-20-discard-restore": (
        "Activity Monitor", "sysmon-process", "discard-restore", False, 1
    ),
    "memory-private-1": ("Activity Monitor", "sysmon-process", "idle", False, 1),
    "memory-private-5": ("Activity Monitor", "sysmon-process", "idle", False, 1),
    "memory-private-20-discard-restore": (
        "Activity Monitor", "sysmon-process", "discard-restore", False, 1
    ),
    "idle-resources": ("Activity Monitor", "sysmon-process", "idle", False, 1),
    "idle-network": ("Network", "har", "idle", False, 3),
    "controller-pressure-policy": (
        "Activity Monitor", "sysmon-process", "lifecycle-flush", False, 1
    ),
    "scroll-motion-standard": (
        "Animation Hitches", "hitches-summary", "scroll", False, 1
    ),
    "scroll-motion-reduced": (
        "Animation Hitches", "hitches-summary", "scroll", True, 1
    ),
}
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


def sha256(path: pathlib.Path) -> str:
    if path.is_file():
        return hashlib.sha256(path.read_bytes()).hexdigest()
    digest = hashlib.sha256(b"ahoi-mobile-tree-sha256-v1\0")
    for child in sorted(path.rglob("*"), key=lambda item: item.relative_to(path).as_posix()):
        relative = child.relative_to(path).as_posix().encode()
        if child.is_symlink():
            digest.update(b"L\0" + relative + b"\0" + os.readlink(child).encode() + b"\0")
        elif child.is_file():
            digest.update(b"F\0" + relative + b"\0" + bytes.fromhex(sha256(child)))
        elif child.is_dir():
            digest.update(b"D\0" + relative + b"\0")
    return digest.hexdigest()


def toc_xml(duration: float) -> str:
    return (
        '<trace-toc><run number="1"><info><target><process pid="4242"/>'
        f'</target><summary><duration>{duration}</duration></summary></info></run></trace-toc>\n'
    )


def schema(columns: list[str], rows: str) -> str:
    cols = "".join(f"<col><mnemonic>{column}</mnemonic></col>" for column in columns)
    return f"<trace-query-result><schema>{cols}</schema><node>{rows}</node></trace-query-result>\n"


def launch_xml(milliseconds: int) -> str:
    nanoseconds = milliseconds * 1_000_000
    rows = (
        '<row><start id="target-start">0</start>'
        f'<duration id="target-duration">{nanoseconds}</duration>'
        '<process id="target-process"><pid>4242</pid></process></row>'
        '<row><start>0</start><duration>999999999999</duration>'
        '<process><pid>9999</pid></process></row>'
        '<row><start ref="target-start"/><duration ref="target-duration"/>'
        '<process ref="target-process"/></row>'
    )
    return schema(["start", "duration", "process"], rows)


def sysmon_xml(points: int) -> str:
    rows = [
        '<row><time>0</time><process><pid>9999</pid></process><cpu-percent>99</cpu-percent>'
        '<interrupt-wakeups>999</interrupt-wakeups>'
        '<memory-resident-size>999999999999</memory-resident-size></row>'
    ]
    for index in range(points):
        process = (
            '<process id="target-process"><pid>4242</pid></process>'
            if index == 0 else '<process ref="target-process"/>'
        )
        rows.append(
            f'<row><time>{index * 1_000_000_000}</time>{process}'
            '<cpu-percent>0.5</cpu-percent>'
            f'<interrupt-wakeups>{index}</interrupt-wakeups>'
            f'<memory-resident-size>{100 * 1024 * 1024}</memory-resident-size></row>'
        )
    return schema(
        ["time", "process", "cpu-percent", "interrupt-wakeups", "memory-resident-size"],
        "".join(rows),
    )


def hitches_xml() -> str:
    rows = (
        '<row><start id="hitch-start">100000000</start>'
        '<duration id="hitch-duration">1000000</duration></row>'
        '<row><start>1100000000</start><duration ref="hitch-duration"/></row>'
    )
    return schema(["start", "duration"], rows)


class RawCaptureFixture:
    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix=".mobile-performance-evaluator-v2-", dir=ROOT
        )
        self.root = pathlib.Path(self.temporary.name)
        self.capture_root = self.root / "capture"
        self.manifest_path = self.capture_root / "manifest.json"
        self.index_path = self.capture_root / "artifacts.sha256"
        self.capture_root.mkdir()
        self.captures = []
        for slug, contract in STAGES.items():
            for run_index in range(1, contract[4] + 1):
                self.captures.append(self.make_capture(slug, run_index, contract))
        self.manifest = self.make_manifest()
        self.write_manifest_and_index()

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception, traceback):
        self.temporary.cleanup()

    def write(self, relative: str, content: str | bytes) -> pathlib.Path:
        path = self.capture_root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8")
        return path

    @staticmethod
    def ref(root: pathlib.Path, path: pathlib.Path) -> dict[str, str]:
        return {"path": path.relative_to(root).as_posix(), "sha256": sha256(path)}

    @staticmethod
    def marker(slug: str, nonce: str, workload: str, reduced: bool) -> dict:
        samples = {key: [] for key in SAMPLE_KEYS}
        if slug in {"launch-cold", "launch-warm-cache"}:
            samples["sessionReadyMilliseconds"] = [100]
        elif slug in {
            "memory-normal-20-discard-restore",
            "memory-private-20-discard-restore",
        }:
            samples["discardMilliseconds"] = [100] * 10
            samples["restoreMilliseconds"] = [200] * 10
        elif slug == "controller-pressure-policy":
            samples.update({
                "sessionFlushMilliseconds": [100] * 10,
                "sessionFlushFailureCounts": [0] * 10,
                "livePagesForeground": [5] * 10,
                "livePagesBackground": [2] * 10,
                "livePagesMemoryWarning": [1] * 10,
                "backgroundPolicyMilliseconds": [100] * 10,
                "foregroundRestoreMilliseconds": [200] * 10,
            })
        normal_count, private_count = 1, 0
        scales = {
            "memory-normal-1": (1, 0), "memory-normal-5": (5, 0),
            "memory-normal-20-discard-restore": (20, 0),
            "memory-private-1": (0, 1), "memory-private-5": (0, 5),
            "memory-private-20-discard-restore": (0, 20),
            "controller-pressure-policy": (20, 0),
        }
        normal_count, private_count = scales.get(slug, (normal_count, private_count))
        return {
            "schemaVersion": 2,
            "scenario": slug,
            "nonce": nonce,
            "workload": workload,
            "status": "completed",
            "reduceMotion": reduced,
            "normalTabCount": normal_count,
            "privateTabCount": private_count,
            "livePageCount": min(5, normal_count + private_count),
            "samples": samples,
        }

    def make_capture(self, slug: str, run_index: int, contract: tuple) -> dict:
        template, kind, workload, reduced, _ = contract
        capture_id = f"{slug}-run-{run_index:02d}"
        nonce = f"{SOURCE_COMMIT}-fixture-{capture_id}"
        trace = self.capture_root / "traces" / f"{capture_id}.trace"
        trace.mkdir(parents=True)
        (trace / "payload.bin").write_bytes(f"trace:{capture_id}".encode())
        duration = 60.0 if kind == "har" else 30.0 if kind == "hitches-summary" else 31.0
        toc = self.write(f"exports/{capture_id}.toc.xml", toc_xml(duration))
        if kind == "life-cycle-period":
            milliseconds = 1_000 if slug == "launch-cold" else 500
            raw = launch_xml(milliseconds)
        elif kind == "sysmon-process":
            raw = sysmon_xml(31 if slug == "idle-resources" else 5)
        elif kind == "hitches-summary":
            raw = hitches_xml()
        else:
            raw = json.dumps({"log": {"entries": []}}, sort_keys=True) + "\n"
        export = self.write(f"exports/{capture_id}.{kind}.raw", raw)
        marker = self.write(
            f"markers/{capture_id}.json",
            json.dumps(self.marker(slug, nonce, workload, reduced), sort_keys=True) + "\n",
        )
        host = self.write(
            f"host-samples/{capture_id}.json",
            json.dumps({
                "schemaVersion": 1,
                "logicalCPUCount": 10,
                "load1": 1.0,
                "maxForeignCPUPercent": 5.0,
                "totalForeignCPUPercent": 10.0,
                "sampledProcessCount": 5,
            }, sort_keys=True) + "\n",
        )
        return {
            "stageSlug": slug,
            "runIndex": run_index,
            "scenarioNonce": nonce,
            "template": template,
            "status": "CAPTURED_RAW",
            "trace": self.ref(self.capture_root, trace),
            "tocExport": self.ref(self.capture_root, toc),
            "dataExport": {
                "kind": kind,
                **self.ref(self.capture_root, export),
                "query": (
                    "HAR-v1" if kind == "har" else
                    f'/trace-toc/run[@number="1"]/data/table[@schema="{kind}"]'
                ),
            },
            "marker": self.ref(self.capture_root, marker),
            "hostSample": self.ref(self.capture_root, host),
            "commandLabel": "fixture-capture",
        }

    def make_command(self) -> dict:
        base = "commands/fixture-capture"
        command = self.write(base + ".command.txt", "cwd=fixture\nargv=xctrace record\n")
        stdout = self.write(base + ".stdout.log", "completed\n")
        stderr = self.write(base + ".stderr.log", "")
        exit_receipt = self.write(base + ".exit-code.txt", "0\n")
        return {
            "label": "fixture-capture",
            "command": self.ref(self.capture_root, command),
            "stdout": self.ref(self.capture_root, stdout),
            "stderr": self.ref(self.capture_root, stderr),
            "exitCode": 0,
            "exitCodeReceipt": self.ref(self.capture_root, exit_receipt),
        }

    def make_manifest(self) -> dict:
        toolchain = {}
        for name, path in (
            ("harness", HARNESS), ("helper", HELPER),
            ("evaluator", EVALUATOR), ("budgets", BUDGETS),
        ):
            toolchain[name + "Path"] = str(path.resolve())
            toolchain[name + "Sha256"] = sha256(path)
        toolchain.update({
            "xctracePath": str(XCTRACE),
            "xctraceSha256": sha256(XCTRACE),
            "xctraceVersion": "fixture-xctrace",
            "xcodeVersion": "fixture-xcode",
            "developerDirectory": "/Applications/Xcode.app/Contents/Developer",
            "hostSystem": "fixture-macOS",
            "hostKernel": "fixture-kernel",
        })
        return {
            "schemaVersion": 2,
            "kind": "mobile-performance-evidence-capture",
            "status": "CAPTURED_RAW_EVIDENCE",
            "exitCode": 0,
            "mode": "record",
            "request": {"expectedSourceCommit": SOURCE_COMMIT},
            "candidate": {
                "bundleId": "app.ahoibrowser.AhoiBrowser",
                "sourceCommit": SOURCE_COMMIT,
                "buildMode": "PerformanceDevelopment",
                "optimizationLevel": "-O",
                "binaryPlatform": "IOS",
                "supportedPlatform": "iPhoneOS",
                "executableSha256": "a" * 64,
                "infoPlistSha256": "b" * 64,
                "appBundleTreeSha256": "c" * 64,
                "signing": {
                    "verifiedStrictly": True,
                    "kind": "certificate",
                    "teamIdentifier": "248AJ5BN47",
                    "boundDeviceKind": "physical",
                },
            },
            "device": {
                "kind": "physical", "platform": "iOS", "deviceFamily": "iPhone",
                "udid": "00008101-FIXTURE", "name": "Fixture iPhone", "osVersion": "26.0",
            },
            "toolchain": toolchain,
            "captures": self.captures,
            "commands": [self.make_command()],
            "errors": [],
            "artifactChecksumIndex": "artifacts.sha256",
            "evaluation": {"status": "NOT_EVALUATED"},
            "releaseGate": {"acceptedAsFeaturePass": False},
        }

    def write_manifest_and_index(self) -> None:
        self.manifest_path.write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        lines = []
        for path in sorted(self.capture_root.rglob("*")):
            if path.is_file() and not path.is_symlink() and path != self.index_path:
                lines.append(f"{sha256(path)}  ./{path.relative_to(self.capture_root).as_posix()}")
        self.index_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def rewrite_capture_artifact(self, capture: dict, key: str, content: str) -> None:
        reference = capture[key]
        path = self.capture_root / reference["path"]
        path.write_text(content, encoding="utf-8")
        reference["sha256"] = sha256(path)
        self.write_manifest_and_index()


class MobilePerformanceBudgetEvaluatorTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.budgets = json.loads(BUDGETS.read_text(encoding="utf-8"))

    def run_evaluator(self, fixture: RawCaptureFixture, name: str = "evaluation.json", *extra):
        output = fixture.root / name
        completed = subprocess.run(
            [str(EVALUATOR), "--capture-manifest", str(fixture.manifest_path),
             "--budgets", str(BUDGETS), "--output", str(output), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        payload = json.loads(output.read_text(encoding="utf-8")) if output.exists() else None
        return completed, output, payload

    @staticmethod
    def result(payload: dict, metric_id: str) -> dict:
        return next(result for result in payload["metricResults"] if result["id"] == metric_id)

    def test_budget_v2_is_prefrozen_and_names_policy_metrics_honestly(self):
        self.assertEqual(2, self.budgets["schemaVersion"])
        policy = self.budgets["candidatePolicy"]
        self.assertEqual("PerformanceDevelopment", policy["requiredBuildMode"])
        self.assertEqual("-O", policy["requiredOptimizationLevel"])
        self.assertEqual("248AJ5BN47", policy["signingTeamIdentifier"])
        self.assertFalse(policy["simulatorOnlyAccepted"])
        source_map = {metric["id"]: metric["sourceStageSlugs"] for metric in self.budgets["metrics"]}
        self.assertIn("controller.background_policy.duration_ms", source_map)
        self.assertIn("controller.foreground_restore.duration_ms", source_map)
        self.assertNotIn("lifecycle.background.duration_ms", source_map)
        self.assertEqual(["scroll-motion-standard"], source_map["animation.standard.hitch_percent"])
        self.assertEqual(["scroll-motion-reduced"], source_map["animation.reduce_motion.hitch_percent"])
        ancestor = subprocess.run(
            ["git", "merge-base", "--is-ancestor", self.budgets["frozenAtBaseCommit"], SOURCE_COMMIT],
            cwd=ROOT, check=False,
        )
        self.assertEqual(0, ancestor.returncode)

    def test_clean_raw_capture_passes_with_derived_provenance_and_immutable_receipt(self):
        with RawCaptureFixture() as fixture:
            completed, output, payload = self.run_evaluator(fixture)
            self.assertEqual(0, completed.returncode, completed.stderr + completed.stdout)
            self.assertEqual("PASS", payload["status"])
            self.assertEqual("direct-raw-artifact-derivation", payload["normalization"]["mode"])
            self.assertFalse(payload["normalization"]["callerSuppliedMeasurementsAccepted"])
            self.assertTrue(payload["releaseGate"]["acceptedAsPerformancePass"])
            launch = self.result(payload, "launch.cold.duration_ms")
            self.assertEqual([1000.0] * 5, [sample["value"] for sample in launch["samples"]])
            self.assertTrue(all("target-pid=4242" in sample["provenance"]["detail"] for sample in launch["samples"]))
            self.assertTrue(all(sample["provenance"]["artifact"].endswith("life-cycle-period.raw") for sample in launch["samples"]))
            self.assertTrue(all(sample["provenance"]["scenarioNonce"].startswith(SOURCE_COMMIT + "-") for sample in launch["samples"]))
            self.assertTrue(all(len(sample["provenance"]["artifactSha256"]) == 64 for sample in launch["samples"]))
            self.assertEqual(31, self.result(payload, "idle.cpu.percent")["sampleCount"])
            self.assertEqual(30, self.result(payload, "idle.wakeups.per_second")["sampleCount"])
            self.assertEqual([0.5] * 31, [item["value"] for item in self.result(payload, "idle.cpu.percent")["samples"]])
            self.assertEqual([1.0] * 30, [item["value"] for item in self.result(payload, "idle.wakeups.per_second")["samples"]])
            standard = self.result(payload, "animation.standard.hitch_percent")
            reduced = self.result(payload, "animation.reduce_motion.hitch_percent")
            self.assertEqual(30, standard["sampleCount"])
            self.assertEqual(30, reduced["sampleCount"])
            self.assertAlmostEqual(0.1, standard["samples"][0]["value"])
            self.assertAlmostEqual(0.1, standard["samples"][1]["value"])
            receipt = output.with_name(output.name + ".sha256")
            digest, filename = receipt.read_text(encoding="utf-8").split()
            self.assertEqual(output.name, filename)
            self.assertEqual(sha256(output), digest)
            self.assertFalse(output.stat().st_mode & stat.S_IWUSR)
            second, _, _ = self.run_evaluator(fixture)
            self.assertEqual(2, second.returncode)
            self.assertIn("exclusive-create", second.stderr)

    def test_caller_supplied_measurements_argument_is_rejected(self):
        with RawCaptureFixture() as fixture:
            fake = fixture.root / "forged-measurements.json"
            fake.write_text('{"claimedStatus":"PASS"}\n', encoding="utf-8")
            completed, output, payload = self.run_evaluator(
                fixture, "manual.json", "--measurements", str(fake)
            )
            self.assertEqual(2, completed.returncode)
            self.assertIsNone(payload)
            self.assertFalse(output.exists())
            self.assertIn("unrecognized arguments", completed.stderr)

    def test_har_is_normalized_per_minute_and_nonzero_idle_request_fails_budget(self):
        with RawCaptureFixture() as fixture:
            capture = next(item for item in fixture.captures if item["stageSlug"] == "idle-network")
            har = {"log": {"entries": [{
                "request": {"headersSize": 100, "bodySize": 20},
                "response": {"headersSize": 50, "bodySize": 30, "content": {"size": 500}},
            }]}}
            fixture.rewrite_capture_artifact(capture, "dataExport", json.dumps(har) + "\n")
            completed, _, payload = self.run_evaluator(fixture)
            self.assertEqual(1, completed.returncode)
            self.assertEqual("RED_BUDGET_EXCEEDED", payload["status"])
            byte_values = sorted(item["value"] for item in self.result(payload, "idle.network.bytes_per_minute")["samples"])
            request_values = sorted(item["value"] for item in self.result(payload, "idle.network.requests_per_minute")["samples"])
            self.assertEqual([0.0, 0.0, 200.0], byte_values)
            self.assertEqual([0.0, 0.0, 1.0], request_values)
            self.assertEqual("BUDGET_EXCEEDED", self.result(payload, "idle.network.requests_per_minute")["status"])

    def test_raw_threshold_violation_is_computed_instead_of_trusting_claims(self):
        with RawCaptureFixture() as fixture:
            capture = next(item for item in fixture.captures if item["stageSlug"] == "launch-cold")
            fixture.rewrite_capture_artifact(capture, "dataExport", launch_xml(4000))
            completed, _, payload = self.run_evaluator(fixture)
            self.assertEqual(1, completed.returncode)
            self.assertEqual("RED_BUDGET_EXCEEDED", payload["status"])
            result = self.result(payload, "launch.cold.duration_ms")
            self.assertEqual("BUDGET_EXCEEDED", result["status"])
            self.assertIn("maximumSample", result["violations"])

    def test_unindexed_symlink_tamper_and_missing_artifact_each_fail_closed(self):
        cases = []
        with RawCaptureFixture() as fixture:
            (fixture.capture_root / "forged.json").write_text("{}\n", encoding="utf-8")
            cases.append(self.run_evaluator(fixture, "unindexed.json")[2])
        with RawCaptureFixture() as fixture:
            (fixture.capture_root / "forged-link").symlink_to(fixture.manifest_path)
            cases.append(self.run_evaluator(fixture, "symlink.json")[2])
        with RawCaptureFixture() as fixture:
            capture = fixture.captures[0]
            (fixture.capture_root / capture["dataExport"]["path"]).write_text("tampered\n")
            cases.append(self.run_evaluator(fixture, "tamper.json")[2])
        with RawCaptureFixture() as fixture:
            capture = fixture.captures[0]
            (fixture.capture_root / capture["marker"]["path"]).unlink()
            cases.append(self.run_evaluator(fixture, "missing.json")[2])
        for payload in cases:
            self.assertEqual("RED_INVALID_EVIDENCE", payload["status"])
            self.assertTrue(payload["errors"])

    def test_candidate_toolchain_host_and_prefreeze_contracts_fail_closed(self):
        mutations = (
            lambda fixture: fixture.manifest["candidate"].update(buildMode="DebugLocal"),
            lambda fixture: fixture.manifest["candidate"].update(optimizationLevel="-Onone"),
            lambda fixture: fixture.manifest["candidate"]["signing"].update(teamIdentifier="WRONG"),
            lambda fixture: fixture.manifest["device"].update(kind="simulator", platform="iOSSimulator"),
            lambda fixture: fixture.manifest["toolchain"].update(helperSha256="0" * 64),
            lambda fixture: fixture.manifest["candidate"].update(sourceCommit="f" * 40),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index), RawCaptureFixture() as fixture:
                mutate(fixture)
                fixture.write_manifest_and_index()
                completed, _, payload = self.run_evaluator(fixture, f"invalid-{index}.json")
                self.assertEqual(1, completed.returncode)
                self.assertEqual("RED_INVALID_EVIDENCE", payload["status"])
        with RawCaptureFixture() as fixture:
            capture = fixture.captures[0]
            busy = json.loads((fixture.capture_root / capture["hostSample"]["path"]).read_text())
            busy["load1"] = 100.0
            fixture.rewrite_capture_artifact(capture, "hostSample", json.dumps(busy) + "\n")
            completed, _, payload = self.run_evaluator(fixture, "busy.json")
            self.assertEqual(1, completed.returncode)
            self.assertIn("busy-host", "\n".join(payload["errors"]))

    def test_marker_cannot_inject_samples_outside_scenario_authority(self):
        with RawCaptureFixture() as fixture:
            capture = next(item for item in fixture.captures if item["stageSlug"] == "idle-resources")
            path = fixture.capture_root / capture["marker"]["path"]
            marker = json.loads(path.read_text(encoding="utf-8"))
            marker["samples"]["sessionFlushFailureCounts"] = [0] * 10
            fixture.rewrite_capture_artifact(capture, "marker", json.dumps(marker) + "\n")
            completed, _, payload = self.run_evaluator(fixture)
            self.assertEqual(1, completed.returncode)
            self.assertEqual("RED_INVALID_EVIDENCE", payload["status"])
            self.assertIn("outside scenario authority", "\n".join(payload["errors"]))

    def test_absent_stage_is_insufficient_and_output_inside_capture_is_rejected(self):
        with RawCaptureFixture() as fixture:
            fixture.manifest["captures"] = [
                capture for capture in fixture.captures
                if capture["stageSlug"] != "scroll-motion-reduced"
            ]
            fixture.write_manifest_and_index()
            completed, _, payload = self.run_evaluator(fixture, "absent.json")
            self.assertEqual(1, completed.returncode)
            self.assertIn("required raw capture stage is absent", "\n".join(payload["errors"]))
            inside = fixture.capture_root / "evaluation.json"
            blocked = subprocess.run(
                [str(EVALUATOR), "--capture-manifest", str(fixture.manifest_path),
                 "--output", str(inside)], cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(2, blocked.returncode)
            self.assertIn("outside the immutable capture root", blocked.stderr)


if __name__ == "__main__":
    unittest.main()
