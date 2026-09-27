import base64
import copy
import hashlib
import json
import pathlib
import socket
import sys
import threading
import unittest
import urllib.request
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/perf"))

import cdp  # noqa: E402
import perf_stats as ps  # noqa: E402
import run_desktop_perf as runner  # noqa: E402

CONDITIONS = {"chromiumVersion": "153.0.8010.53", "hardwareModel": "Mac16,1",
              "osBuild": "26A1", "powerSource": "ac", "flags": ["--x"],
              "windowSize": "1440,900", "accessibilityClients": [], "scenarioVersion": 2}


def run_file(baseline=False, **metrics):
    # Synthetic, receipt-verified fixtures for the statistical tests only.
    proof = {"verified": True, "budgetEligible": True, "binarySha256": "a" * 64,
             "kind": "unmodified-upstream-control" if baseline else "ahoi-release",
             "comparison": {"optimization": {
                 "chromePgoPhase": 2, "useThinLto": True,
                 "pgoProfile": {"target": "mac-arm", "name": "test.profdata",
                                "matchesChromiumPin": True, "sha256": "b" * 64}}}}
    return {"app": {"path": "x", "binarySha256": "a" * 64},
            "conditions": dict(CONDITIONS), "metrics": metrics, "buildEvidence": proof,
            "runtimeGuard": {"completed": True, "cancelled": False}}


def verdict(evaluation, budget):
    return next(v for v in evaluation["verdicts"] if v["budget"] == budget)


class StatsTest(unittest.TestCase):
    def test_legacy_load_end_startup_cannot_pass_first_paint_budget(self):
        candidate = run_file(startup_warm_ms=[1000] * 5,
                             startup_first_launch_ms=[1300] * 5)
        baseline = run_file(baseline=True, startup_warm_ms=[1000] * 5,
                            startup_first_launch_ms=[1300] * 5)
        candidate["conditions"]["scenarioVersion"] = 1
        baseline["conditions"]["scenarioVersion"] = 1
        evaluation = ps.evaluate(candidate, baseline)
        self.assertFalse(evaluation["pass"])
        for budget in ("PERF-02", "PERF-02-first"):
            self.assertEqual(verdict(evaluation, budget)["verdict"], "INSUFFICIENT")

    def test_percentile_is_nearest_rank(self):
        self.assertEqual(ps.percentile(list(range(1, 21)), 0.95), 19)
        self.assertEqual(ps.percentile([5.0], 0.95), 5.0)

    def test_summary(self):
        summary = ps.summarize([10, 11, 12, 13, 100])
        self.assertEqual(summary["median"], 12)
        self.assertEqual(summary["mad"], 1)
        self.assertEqual(summary["p95"], 100)

    def test_relative_budget_pass_and_fail(self):
        base = run_file(baseline=True, startup_warm_ms=[1000, 1002, 998, 1001, 999, 1000])
        ok = run_file(startup_warm_ms=[1040, 1042, 1038, 1041, 1039, 1040])
        slow = run_file(startup_warm_ms=[1200, 1202, 1198, 1201, 1199, 1200])
        self.assertEqual(verdict(ps.evaluate(ok, base), "PERF-02")["verdict"], "PASS")
        self.assertEqual(verdict(ps.evaluate(slow, base), "PERF-02")["verdict"], "FAIL")

    def test_higher_is_better_budget(self):
        base = run_file(baseline=True, speedometer_score=[30.0, 30.1, 29.9, 30.0, 30.05])
        worse = run_file(speedometer_score=[28.0, 28.1, 27.9, 28.0, 28.05])
        result = verdict(ps.evaluate(worse, base), "PERF-01")
        self.assertEqual(result["verdict"], "FAIL")
        self.assertAlmostEqual(result["worsening"], 1 - 28 / 30, places=3)

    def test_too_few_or_noisy_samples_are_insufficient(self):
        base = run_file(baseline=True, startup_warm_ms=[1000] * 6)
        few = run_file(startup_warm_ms=[1000, 1001])
        noisy = run_file(startup_warm_ms=[500, 1000, 1500, 700, 1300, 900])
        self.assertEqual(verdict(ps.evaluate(few, base), "PERF-02")["verdict"], "INSUFFICIENT")
        self.assertEqual(verdict(ps.evaluate(noisy, base), "PERF-02")["verdict"], "INSUFFICIENT")

    def test_mismatched_conditions_are_insufficient(self):
        base = run_file(baseline=True, startup_warm_ms=[1000] * 6)
        cand = run_file(startup_warm_ms=[1000] * 6)
        cand["conditions"]["accessibilityClients"] = ["VoiceOver"]
        result = verdict(ps.evaluate(cand, base), "PERF-02")
        self.assertEqual(result["verdict"], "INSUFFICIENT")
        self.assertIn("accessibilityClients", result["reason"])

    def test_absolute_budget_uses_p95(self):
        fast = run_file(command_bar_ms=[20, 21, 22, 20, 21, 22, 20, 21, 22, 49])
        self.assertEqual(verdict(ps.evaluate(fast, None), "PERF-03")["verdict"], "PASS")
        slow = run_file(command_bar_ms=[40, 41, 42, 40, 41, 42, 40, 41, 42, 60])
        self.assertEqual(verdict(ps.evaluate(slow, None), "PERF-03")["verdict"], "FAIL")

    def test_idle_budget(self):
        base = run_file(baseline=True, idle_cpu_percent=[0.20, 0.21, 0.19, 0.20, 0.22])
        same = run_file(idle_cpu_percent=[0.22, 0.23, 0.21, 0.22, 0.23])
        busy = run_file(idle_cpu_percent=[1.5, 1.52, 1.48, 1.5, 1.51])
        self.assertEqual(verdict(ps.evaluate(same, base), "PERF-07")["verdict"], "PASS")
        self.assertEqual(verdict(ps.evaluate(busy, base), "PERF-07")["verdict"], "FAIL")

    def test_unbound_old_run_cannot_pass_any_budget(self):
        candidate = run_file(command_bar_ms=[1] * 5, startup_warm_ms=[1] * 5)
        candidate.pop("buildEvidence")
        result = ps.evaluate(candidate, run_file(baseline=True, startup_warm_ms=[1] * 5))
        self.assertFalse(result["pass"])
        for budget in ("PERF-02", "PERF-03"):
            self.assertEqual(verdict(result, budget)["verdict"], "INSUFFICIENT")

    def test_unmonitored_or_cancelled_samples_never_pass(self):
        for value in (None, "unverified", {}, {"completed": False, "cancelled": False},
                      {"completed": True, "cancelled": True}):
            for side in ("candidate", "baseline"):
                candidate = run_file(command_bar_ms=[1] * 5, startup_warm_ms=[1] * 5)
                baseline = run_file(baseline=True, startup_warm_ms=[1] * 5)
                (candidate if side == "candidate" else baseline)["runtimeGuard"] = value
                with self.subTest(side=side, value=value):
                    result = ps.evaluate(candidate, baseline)
                    self.assertFalse(result["pass"])
                    self.assertEqual(verdict(result, "PERF-02")["verdict"], "INSUFFICIENT")

    def test_different_build_configuration_cannot_pass(self):
        candidate = run_file(startup_warm_ms=[1] * 5)
        baseline = run_file(baseline=True, startup_warm_ms=[1] * 5)
        baseline["buildEvidence"]["comparison"]["optimization"]["useThinLto"] = False
        self.assertEqual(verdict(ps.evaluate(candidate, baseline), "PERF-02")["verdict"],
                         "INSUFFICIENT")

    def test_missing_optimization_or_wrong_role_cannot_pass(self):
        candidate = run_file(startup_warm_ms=[1] * 5)
        baseline = run_file(baseline=True, startup_warm_ms=[1] * 5)
        for changed in ("optimization", "kind", "binary"):
            altered = copy.deepcopy(baseline)
            if changed == "optimization":
                altered["buildEvidence"]["comparison"].pop("optimization")
            elif changed == "kind":
                altered["buildEvidence"]["kind"] = "ahoi-release"
            else:
                altered["app"]["binarySha256"] = "different"
            with self.subTest(changed=changed):
                self.assertEqual(verdict(ps.evaluate(candidate, altered), "PERF-02")["verdict"],
                                 "INSUFFICIENT")

    def test_nothing_measured_is_not_a_pass(self):
        evaluation = ps.evaluate(run_file(), None)
        self.assertFalse(evaluation["pass"])
        self.assertTrue(all(v["verdict"] == "NOT_MEASURED" for v in evaluation["verdicts"]))


def fake_devtools_server():
    """One-connection WebSocket server answering every command with its id."""
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)

    def serve():
        conn, _ = server.accept()
        request = b""
        while b"\r\n\r\n" not in request:
            request += conn.recv(4096)
        key = [line.split(b": ")[1] for line in request.split(b"\r\n")
               if line.lower().startswith(b"sec-websocket-key")][0]
        accept = base64.b64encode(hashlib.sha1(
            key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
        conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        buffer = bytearray()

        def read(count):
            while len(buffer) < count:
                buffer.extend(conn.recv(65536))
            data = bytes(buffer[:count])
            del buffer[:count]
            return data

        while True:
            try:
                opcode, payload, _ = cdp.decode_frame(read)
            except (OSError, ValueError):
                break
            if opcode == 0x8:
                break
            command = json.loads(payload)
            event = {"method": "Test.event", "params": {"n": command["id"]}}
            reply = {"id": command["id"], "result": {"echo": command["method"],
                                                     "big": "x" * 70000}}
            for message in (event, reply):
                data = json.dumps(message).encode()
                header = bytearray([0x81])
                header += bytes([127]) + len(data).to_bytes(8, "big")
                conn.sendall(bytes(header) + data)
        conn.close()
        server.close()

    threading.Thread(target=serve, daemon=True).start()
    return server.getsockname()[1]


class CDPTest(unittest.TestCase):
    def test_frame_roundtrip_all_lengths(self):
        for size in (0, 125, 126, 65535, 65536, 70000):
            payload = bytes(range(256)) * (size // 256) + bytes(size % 256)
            frame = cdp.encode_frame(payload)
            stream = bytearray(frame)

            def read(count):
                data = bytes(stream[:count])
                del stream[:count]
                return data

            opcode, decoded, fin = cdp.decode_frame(read)
            self.assertEqual((opcode, decoded, fin), (1, payload, True))

    def test_session_against_fake_server(self):
        port = fake_devtools_server()
        session = cdp.CDPSession(f"ws://127.0.0.1:{port}/devtools/browser/x", timeout=5)
        self.assertEqual(session.send("Browser.getVersion")["echo"], "Browser.getVersion")
        self.assertEqual(session.wait_event("Test.event", 1)["params"]["n"], 1)
        session.close()


class RunnerTest(unittest.TestCase):
    def test_startup_requires_a_real_bounded_first_paint(self):
        mark = {"paintEntryName": "first-paint", "firstPaintEpochMs": 1100.0,
                "loadEventEndEpochMs": 1200.0, "receivedEpochMs": 1250.0}
        self.assertEqual(runner.first_paint_ms(mark, 1000.0), 100.0)
        for changed in ({"firstPaintEpochMs": None}, {"paintEntryName": None},
                        {"firstPaintEpochMs": True}, {"firstPaintEpochMs": float("nan")},
                        {"firstPaintEpochMs": 999.0}, {"firstPaintEpochMs": 1251.0},
                        {"receivedEpochMs": None}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                runner.first_paint_ms({**mark, **changed}, 1000.0)
        with self.assertRaises(ValueError):
            runner.first_paint_ms({"loadEventEndEpochMs": 1200.0,
                                   "receivedEpochMs": 1250.0}, 1000.0)

    def test_parse_cputime(self):
        self.assertAlmostEqual(runner.parse_cputime("0:01.50"), 1.5)
        self.assertAlmostEqual(runner.parse_cputime("1:02:03.00"), 3723.0)
        self.assertAlmostEqual(runner.parse_cputime("2-00:00:01.00"), 172801.0)

    def test_tree(self):
        table = {1: {"ppid": 0}, 2: {"ppid": 1}, 3: {"ppid": 2}, 4: {"ppid": 9}}
        self.assertEqual(sorted(runner.tree(1, table)), [1, 2, 3])

    def test_trace_durations(self):
        events = [{"name": "Ahoi.CommandBar.Query", "ph": "X", "dur": 12000},
                  {"name": "Ahoi.Workspace.Switch", "ph": "B", "ts": 1000, "tid": 1},
                  {"name": "Ahoi.Workspace.Switch", "ph": "E", "ts": 41000, "tid": 1},
                  {"name": "Other", "ph": "X", "dur": 5}]
        result = runner.trace_durations_ms(events, {
            "Ahoi.CommandBar.Query": "command_bar_ms",
            "Ahoi.Workspace.Switch": "workspace_switch_ms"})
        self.assertEqual(result, {"command_bar_ms": [12.0], "workspace_switch_ms": [40.0]})

    def quiet_host(self, **overrides):
        return {"hardwareModel": "Mac", "cpuCount": 10, "osBuild": "b", "powerSource": "ac",
                "loadAverage": [0.5, 0.5, 0.5], "thermal": "", "accessibilityClients": [],
                "busyProcesses": [], "hidIdleSeconds": 1000, **overrides}

    def test_preflight_refusals(self):
        app = pathlib.Path("/tmp/Candidate.app")
        with mock.patch.object(runner, "host_conditions", return_value=self.quiet_host()), \
                mock.patch.object(runner, "port_free", return_value=True):
            runner.preflight([app], 9355, 300, lease=False)
            with self.assertRaises(runner.Refused):
                runner.preflight([runner.INSTALLED_APP], 9355, 300, lease=False)
            runner.preflight([runner.INSTALLED_APP], 9355, 300, lease=True)
        for override in ({"busyProcesses": ["ninja"]}, {"powerSource": "battery"},
                         {"hidIdleSeconds": 5}, {"loadAverage": [9.0, 1, 1]}):
            with mock.patch.object(runner, "host_conditions",
                                   return_value=self.quiet_host(**override)), \
                    mock.patch.object(runner, "port_free", return_value=True):
                with self.assertRaises(runner.Refused, msg=str(override)):
                    runner.preflight([app], 9355, 300, lease=False)

    def test_fixture_server_reports_marks(self):
        fixtures = runner.FixtureServer()
        try:
            body = urllib.request.urlopen(fixtures.url("/start?run=a")).read().decode()
            self.assertIn("first-paint", body)
            self.assertIn("page 3", urllib.request.urlopen(fixtures.url("/page/3")).read().decode())
            request = urllib.request.Request(
                fixtures.url("/mark"), method="POST",
                data=json.dumps({"label": "a", "firstPaintEpochMs": 5,
                                 "paintEntryName": "first-paint"}).encode())
            urllib.request.urlopen(request).read()
            self.assertEqual(fixtures.wait_mark("a", 2)["firstPaintEpochMs"], 5)
        finally:
            fixtures.close()


if __name__ == "__main__":
    unittest.main()
