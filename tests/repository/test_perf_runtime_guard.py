import contextlib
import copy
import datetime as dt
import json
import pathlib
import socket
import sys
import tempfile
import threading
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/perf"))
import cdp
import owned_process
import runtime_guard as rg
import run_desktop_perf as runner


QUIET = {"hidIdleSeconds": 600, "powerSource": "ac", "busyProcesses": [],
         "accessibilityClients": []}
BUNDLE = {"binarySha256": "a" * 64, "bundleTreeSha256": "b" * 64}


class LeaseGuardTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = pathlib.Path(self.temp.name)
        self.checkpoint = self.directory / "owner.md"
        self.grant = {"id": "fixture-lease", "status": "open", "mode": "budget",
                      "expiresAt": "2999-01-01T00:00:00+00:00", "resources": ["host-quiet"],
                      "bundles": [BUNDLE], "lockDirectory": str(self.directory)}
        self.write_grant()
        self.probe = mock.Mock(return_value=copy.deepcopy(QUIET))

    def write_grant(self, **changes):
        self.grant.update(changes)
        self.checkpoint.write_text("# Owner checkpoint\n" + rg.MARKER + json.dumps(self.grant) + "\n")

    def guard(self, **kwargs):
        return rg.LeaseGuard(self.checkpoint, [BUNDLE], QUIET, self.probe, **kwargs)

    @contextlib.contextmanager
    def without_monitor(self, guard):
        with mock.patch.object(guard, "_start_monitor"):
            with guard:
                yield guard

    def test_claims_and_releases_only_its_own_lock(self):
        guard = self.guard()
        with self.without_monitor(guard):
            self.assertIs(rg.current(), guard)
            value = json.loads((self.directory / "h3.lock").read_text())
            self.assertEqual(value["leaseId"], "fixture-lease")
            self.assertFalse(guard.summary()["completed"])
        self.assertFalse((self.directory / "h3.lock").exists())
        self.assertIsNone(rg.current())
        self.assertTrue(guard.summary()["completed"])

    def test_closed_expired_missing_ambiguous_grants_do_not_claim(self):
        for text in ("historical window **open**", rg.MARKER + "{}",
                     rg.MARKER + json.dumps({**self.grant, "status": "closed"}),
                     rg.MARKER + json.dumps({**self.grant, "expiresAt": "2000-01-01T00:00:00Z"}),
                     (rg.MARKER + json.dumps(self.grant) + "\n") * 2):
            self.checkpoint.write_text(text)
            with self.subTest(text=text[:45]), self.assertRaises(rg.LeaseError):
                with self.without_monitor(self.guard()):
                    self.fail("must not acquire")
            self.assertFalse((self.directory / "h3.lock").exists())

    def test_candidate_resources_and_mode_must_match(self):
        original = copy.deepcopy(self.grant)
        for changes in ({"bundles": [{**BUNDLE, "bundleTreeSha256": "c" * 64}]},
                        {"resources": []}, {"mode": "validation"}):
            self.grant = copy.deepcopy(original)
            self.write_grant(**changes)
            with self.subTest(changes=changes), self.assertRaises(rg.LeaseError):
                with self.without_monitor(self.guard()):
                    self.fail("must not acquire")
        self.grant = original
        self.write_grant()
        with self.assertRaises(rg.LeaseError), self.without_monitor(self.guard(installed=True)):
            self.fail("installed-app resource absent")

    def test_command_line_cannot_shorten_owner_idle_floor(self):
        self.probe.return_value = {**QUIET, "hidIdleSeconds": 200}
        with self.assertRaises(rg.RunCancelled), self.without_monitor(self.guard(min_idle=0)):
            self.fail("must respect the owner idle floor")

    def test_existing_owner_or_h3_locks_are_never_removed(self):
        for name in ("build.lock", "e2e.lock", "h3.lock"):
            path = self.directory / name
            path.write_text("foreign lock")
            with self.subTest(name=name), self.assertRaises(rg.LeaseError):
                with self.without_monitor(self.guard()):
                    self.fail("must not acquire")
            self.assertEqual(path.read_text(), "foreign lock")
            path.unlink()

    def test_lock_race_after_claim_still_refuses_before_launch(self):
        guard = self.guard()
        with mock.patch.object(guard, "_conflicting_lock", side_effect=[False, True]), \
                self.assertRaises(rg.RunCancelled), self.without_monitor(guard):
            self.fail("must not acquire")
        self.assertFalse((self.directory / "h3.lock").exists())

    def test_revocation_expiry_replacement_and_new_owner_lock_abort(self):
        for change in ("closed", "expired", "replaced", "build"):
            self.write_grant(id="fixture-lease", status="open", expiresAt="2999-01-01T00:00:00Z")
            guard = self.guard()
            with self.subTest(change=change), self.assertRaises(rg.RunCancelled):
                with self.without_monitor(guard):
                    if change == "closed":
                        self.write_grant(status="closed")
                    elif change == "expired":
                        self.write_grant(expiresAt="2000-01-01T00:00:00Z")
                    elif change == "replaced":
                        self.write_grant(id="another-lease")
                    else:
                        (self.directory / "build.lock").touch()
                    guard.poll_once()
                    guard.check()
            self.assertFalse((self.directory / "h3.lock").exists())
            (self.directory / "build.lock").unlink(missing_ok=True)

    def test_replaced_h3_lock_is_preserved(self):
        guard = self.guard()
        with self.assertRaises(rg.RunCancelled), self.without_monitor(guard):
            lock = self.directory / "h3.lock"
            lock.unlink()
            lock.write_text("new owner's lock")
            guard.poll_once()
            guard.check()
        self.assertEqual((self.directory / "h3.lock").read_text(), "new owner's lock")

    def test_input_build_power_ax_or_failed_probe_cancel(self):
        for state in ({**QUIET, "hidIdleSeconds": 0}, {**QUIET, "busyProcesses": ["ninja"]},
                      {**QUIET, "powerSource": "battery"},
                      {**QUIET, "accessibilityClients": ["VoiceOver"]},
                      {**QUIET, "hidIdleSeconds": float("nan")}, {}):
            guard = self.guard()
            self.probe.return_value = copy.deepcopy(QUIET)
            with self.subTest(state=state), self.assertRaises(rg.RunCancelled):
                with self.without_monitor(guard):
                    self.probe.return_value = state
                    guard.poll_once()
                    guard.check()

    def test_probe_failure_reason_names_the_exception_class_only(self):
        guard = self.guard()
        with self.assertRaises(rg.RunCancelled), self.without_monitor(guard):
            self.probe.side_effect = rg.subprocess.TimeoutExpired("ps secret-arg", 2)
            guard.poll_once()
            guard.check()
        reason = guard.summary()["reason"]
        self.assertEqual(reason, "runtime lease/host probe failed (TimeoutExpired)")
        self.assertNotIn("secret", reason)

    def test_owned_driver_ax_client_is_recorded_not_cancelling(self):
        # The trace driver's own axtool is the documented, non-HID input path.
        guard = self.guard()
        with self.without_monitor(guard):
            self.probe.return_value = {**QUIET, "ownedAccessibilityClients": ["ahoi-axtool"]}
            guard.poll_once()
            guard.check()
        self.assertEqual(guard.summary()["driverAccessibilityClients"], ["ahoi-axtool"])
        self.assertFalse(guard.summary()["cancelled"])

    def test_split_ax_clients_by_owned_process_group(self):
        listing = ("  41 ahoi-axtool\n  77 VoiceOver\n  90 ahoi-axtool\n"
                   "  12 Safari\nnot-a-row\n")
        self.assertEqual(runner.split_ax_clients(listing, {41}), (["VoiceOver", "ahoi-axtool"],
                                                                   ["ahoi-axtool"]))
        self.assertEqual(runner.split_ax_clients(listing, set()),
                         (["VoiceOver", "ahoi-axtool"], []))

    def test_owned_process_groups_are_live_spawned_sessions(self):
        guard = self.guard()
        with self.without_monitor(guard):
            live, done = mock.Mock(pid=41), mock.Mock(pid=42)
            live.poll.return_value, done.poll.return_value = None, 0
            guard._processes = {live: threading.RLock(), done: threading.RLock()}
            self.assertEqual(guard.owned_process_groups(), {41})
            guard._processes = {}

    def test_cancellation_prevents_new_spawns_and_interrupts_wait(self):
        guard = self.guard()
        with self.assertRaises(rg.RunCancelled), self.without_monitor(guard):
            guard.abort("fixture cancellation")
            with mock.patch.object(rg.subprocess, "Popen") as spawn:
                with self.assertRaises(rg.RunCancelled):
                    guard.spawn("not executed", start_new_session=True)
                spawn.assert_not_called()
            with mock.patch.object(guard._cancelled, "wait") as wait:
                with self.assertRaises(rg.RunCancelled):
                    guard.wait(600)
                wait.assert_not_called()

    def test_real_monitor_stops_only_registered_mock_process_without_main_poll(self):
        active = threading.Event()
        stopped = threading.Event()
        self.probe.side_effect = lambda: {**QUIET, "hidIdleSeconds": 0 if active.is_set() else 600}
        guard = self.guard(interval=0.01)
        process = mock.Mock()
        with mock.patch.object(rg.subprocess, "Popen", return_value=process), \
                mock.patch.object(owned_process, "stop", side_effect=lambda *a, **k: stopped.set()) as stop, \
                self.assertRaises(rg.RunCancelled):
            with guard:
                self.assertIs(guard.spawn("fixture", start_new_session=True), process)
                active.set()
                self.assertTrue(stopped.wait(1), "monitor never stopped owned handle")
                self.assertTrue(all(call.args[0] is process for call in stop.call_args_list))
        self.assertFalse(guard._thread.is_alive())
        self.assertFalse((self.directory / "h3.lock").exists())

    def test_failed_process_cleanup_retains_coordination_lock(self):
        guard = self.guard()
        with mock.patch.object(rg.subprocess, "Popen", return_value=mock.Mock()), \
                mock.patch.object(owned_process, "stop", side_effect=owned_process.CleanupError()), \
                self.assertRaises(owned_process.CleanupError), self.without_monitor(guard):
            guard.spawn("fixture", start_new_session=True)
        self.assertTrue((self.directory / "h3.lock").exists())
        self.assertTrue(guard.summary()["lockRetained"])
        self.assertIsNone(rg.current())
        guard._release()  # Only dispose this test's own fake lease.

    def test_devtools_wait_cancellation_runs_before_network_attempt(self):
        check = mock.Mock(side_effect=rg.RunCancelled("fixture"))
        with mock.patch.object(cdp, "http_json") as request, self.assertRaises(rg.RunCancelled):
            cdp.wait_for_endpoint(9355, 60, check_cancel=check)
        request.assert_not_called()

    def test_silent_cdp_socket_can_be_cancelled_without_a_message(self):
        session = object.__new__(cdp.CDPSession)
        session.buffer = b""
        session.sock = mock.Mock()
        session.sock.gettimeout.return_value = 30
        session.sock.recv.side_effect = socket.timeout
        session.check_cancel = mock.Mock(side_effect=[None, None, rg.RunCancelled("fixture")])
        with self.assertRaises(rg.RunCancelled):
            session._read(2)
        session.sock.recv.assert_called_once()

    def test_cancelled_handshake_closes_its_socket(self):
        sock = mock.Mock()
        sock.gettimeout.return_value = 30
        check = mock.Mock(side_effect=[None, rg.RunCancelled("fixture")])
        with mock.patch.object(cdp.socket, "create_connection", return_value=sock), \
                self.assertRaises(rg.RunCancelled):
            cdp.CDPSession("ws://127.0.0.1:9355/devtools", check_cancel=check)
        sock.close.assert_called_once()

    def test_main_records_real_guard_revocation_without_completed_samples(self):
        bundle = self.directory / "Fixture.app"
        bundle.mkdir()
        (bundle / "fixture").write_text("not an executable")
        identity = {"path": str(bundle), "binarySha256": BUNDLE["binarySha256"]}
        self.write_grant(mode="validation", resources=[], bundles=[{
            "binarySha256": identity["binarySha256"],
            "bundleTreeSha256": rg.build_evidence.tree_sha256(bundle)}])
        calls = []
        def scenario(*args, **kwargs):
            calls.append(1)
            if len(calls) == 2:
                self.write_grant(status="closed")
                rg.current().poll_once()
            return {"startup_warm_ms": 1}
        with mock.patch.object(runner, "app_identity", return_value=identity), \
                mock.patch.object(runner, "preflight", return_value=QUIET), \
                mock.patch.object(runner, "runtime_host_state", return_value=QUIET), \
                mock.patch.object(runner, "FixtureServer"), \
                mock.patch.dict(runner.SCENARIOS, {"startup": scenario}):
            output = self.directory / "evidence"
            result = runner.main(["--app", str(bundle), "--scenario", "startup",
                                  "--validation-run", "--runs", "2", "--output", str(output),
                                  "--lease-checkpoint", str(self.checkpoint)])
        self.assertEqual(result, 1)
        self.assertEqual({p.name for p in output.iterdir()}, {"aborted-run.json"})
        evidence = json.loads((output / "aborted-run.json").read_text())
        self.assertTrue(evidence["runtimeGuard"]["cancelled"])
        self.assertFalse(evidence["runtimeGuard"]["completed"])
        self.assertEqual(evidence["partialRuns"][0]["metrics"]["startup_warm_ms"], [1])
        self.assertFalse((self.directory / "h3.lock").exists())
        self.assertIsNone(rg.current())

    def test_completed_validation_records_guard_and_releases_its_lock(self):
        bundle = self.directory / "Fixture.app"
        bundle.mkdir()
        identity = {"path": str(bundle), "binarySha256": BUNDLE["binarySha256"],
                    "chromiumVersion": "153.0.8010.53"}
        conditions = {**QUIET, "hardwareModel": "Fixture", "osBuild": "test"}
        self.write_grant(mode="validation", resources=[], bundles=[{
            "binarySha256": identity["binarySha256"],
            "bundleTreeSha256": rg.build_evidence.tree_sha256(bundle)}])
        with mock.patch.object(runner, "app_identity", return_value=identity), \
                mock.patch.object(runner, "preflight", return_value=conditions), \
                mock.patch.object(runner, "host_conditions", return_value=conditions), \
                mock.patch.object(runner, "runtime_host_state", return_value=QUIET), \
                mock.patch.object(runner, "FixtureServer"), \
                mock.patch.dict(runner.SCENARIOS, {"startup": mock.Mock(
                    return_value={"startup_warm_ms": 1})}):
            output = self.directory / "completed-evidence"
            result = runner.main(["--app", str(bundle), "--scenario", "startup",
                                  "--validation-run", "--runs", "1", "--output", str(output),
                                  "--lease-checkpoint", str(self.checkpoint)])
        self.assertEqual(result, 0)
        evidence = json.loads((output / "candidate-run.json").read_text())
        self.assertTrue(evidence["runtimeGuard"]["completed"])
        self.assertFalse(evidence["runtimeGuard"]["cancelled"])
        self.assertEqual(evidence["runtimeGuard"]["leaseId"], "fixture-lease")
        self.assertEqual(evidence["runtimeGuard"]["checks"], 2)
        self.assertFalse((self.directory / "h3.lock").exists())


if __name__ == "__main__":
    unittest.main()
