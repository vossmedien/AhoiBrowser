import json
import pathlib
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/perf"))
import cdp
import owned_process
import run_desktop_perf as runner


class OwnedProcessTest(unittest.TestCase):
    def process(self):
        process = mock.Mock(pid=812345)
        process.poll.return_value = None
        return process

    def test_exited_process_is_reaped_without_signalling_old_pid(self):
        process = self.process()
        process.poll.return_value = 0
        with mock.patch.object(owned_process.os, "killpg") as kill:
            owned_process.stop(process)
        kill.assert_not_called()
        process.wait.assert_called_once_with()

    def test_term_then_kill_are_bounded_to_the_owned_session(self):
        process = self.process()
        process.wait.side_effect = [subprocess.TimeoutExpired("owned", 5),
                                    subprocess.TimeoutExpired("owned", 5), 0]
        with mock.patch.object(owned_process.os, "killpg") as kill:
            owned_process.stop(process, grace=5)
        self.assertEqual(kill.call_args_list,
                         [mock.call(process.pid, signal.SIGTERM),
                          mock.call(process.pid, signal.SIGKILL)])

    def test_unreaped_or_unsignallable_process_reports_cleanup_failure(self):
        for signal_error in (None, PermissionError("denied"), KeyboardInterrupt()):
            process = self.process()
            process.wait.side_effect = subprocess.TimeoutExpired("owned", 5)
            with self.subTest(error=signal_error), \
                    mock.patch.object(owned_process.os, "killpg", side_effect=signal_error), \
                    self.assertRaises(owned_process.CleanupError):
                owned_process.stop(process)

    def test_failed_devtools_start_stops_its_spawned_process(self):
        process = self.process()
        process.wait.return_value = 0
        with mock.patch.object(runner.subprocess, "Popen", return_value=process) as spawn, \
                mock.patch.object(cdp, "wait_for_endpoint", side_effect=cdp.CDPError("startup")), \
                mock.patch.object(owned_process.os, "killpg") as kill, \
                self.assertRaises(cdp.CDPError):
            runner.Browser({"executable": "/fixture/browser"}, pathlib.Path("/fixture/profile"),
                           9355, "about:blank", ())
        self.assertTrue(spawn.call_args.kwargs["start_new_session"])
        kill.assert_called_once_with(process.pid, signal.SIGTERM)

    def test_quit_closes_failed_cdp_session_and_still_reaps_browser(self):
        browser = object.__new__(runner.Browser)
        browser.process = self.process()
        browser.process.wait.return_value = 0
        session = mock.Mock()
        session.send.side_effect = cdp.CDPError("close failed")
        with mock.patch.object(browser, "session", return_value=session), \
                mock.patch.object(owned_process.os, "killpg") as kill:
            browser.quit()
        session.close.assert_called_once()
        browser.process.wait.assert_called_once_with(timeout=5)
        kill.assert_not_called()

    def test_trace_driver_timeout_cleans_its_own_group(self):
        process = self.process()
        process.wait.side_effect = [subprocess.TimeoutExpired("driver", 900), 0]
        with mock.patch.object(runner.subprocess, "Popen", return_value=process) as spawn, \
                mock.patch.object(owned_process.os, "killpg") as kill, \
                self.assertRaises(subprocess.TimeoutExpired):
            runner.run_driver("fixture driver", {})
        self.assertTrue(spawn.call_args.kwargs["start_new_session"])
        kill.assert_called_once_with(process.pid, signal.SIGTERM)


class ScenarioCleanupTest(unittest.TestCase):
    def test_every_scenario_closes_browser_on_failure(self):
        failures = {"startup": "wait_mark", "memory": "open_tabs", "idle": "tree_totals",
                    "speedometer": "http_json", "trace": "run_driver"}
        with tempfile.TemporaryDirectory() as directory:
            for name, operation in failures.items():
                fixture = mock.Mock()
                fixture.url.return_value = "about:blank"
                target = fixture if name == "startup" else (cdp if name == "speedometer" else runner)
                with self.subTest(scenario=name), \
                        mock.patch.object(runner.subprocess, "Popen"), \
                        mock.patch.object(cdp, "wait_for_endpoint", return_value=0), \
                        mock.patch.object(cdp, "browser_session", return_value=mock.Mock()), \
                        mock.patch.object(runner.time, "sleep"), \
                        mock.patch.object(runner.Browser, "quit", autospec=True) as quit_browser, \
                        mock.patch.object(target, operation, side_effect=RuntimeError("fixture failure")), \
                        self.assertRaises(RuntimeError):
                    runner.SCENARIOS[name]({"executable": "/fixture/browser"}, fixture, 9355,
                                           (), "test", pathlib.Path(directory))
                quit_browser.assert_called_once()


class AbortedEvidenceTest(unittest.TestCase):
    def test_partial_run_is_never_written_as_budget_evidence(self):
        for failure in (RuntimeError("SECRET_TEST_TEXT"), KeyboardInterrupt(),
                        owned_process.CleanupError("cannot reap")):
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                work = root / "profile-work"
                work.mkdir()
                output = root / "evidence"
                scenario = mock.Mock(side_effect=[{"startup_warm_ms": 1}, failure])
                with mock.patch.object(runner, "app_identity", return_value={"path": "/fixture"}), \
                        mock.patch.object(runner, "preflight", return_value={}), \
                        mock.patch.object(runner, "FixtureServer") as fixtures, \
                        mock.patch.object(runner.runtime_guard, "LeaseGuard") as guard, \
                        mock.patch.object(runner.tempfile, "mkdtemp", return_value=str(work)), \
                        mock.patch.dict(runner.SCENARIOS, {"startup": scenario}):
                    guard.return_value.summary.return_value = {"completed": False, "cancelled": True}
                    result = runner.main(["--app", "/fixture", "--scenario", "startup",
                                          "--validation-run", "--runs", "2", "--output", str(output)])
                self.assertEqual(result, 130 if isinstance(failure, KeyboardInterrupt) else 1)
                self.assertEqual({p.name for p in output.iterdir()}, {"aborted-run.json"})
                text = (output / "aborted-run.json").read_text()
                record = json.loads(text)
                self.assertFalse(record["pass"])
                self.assertEqual(record["partialRuns"][0]["metrics"]["startup_warm_ms"], [1])
                self.assertNotIn("SECRET_TEST_TEXT", text)
                retained = isinstance(failure, owned_process.CleanupError)
                self.assertEqual(work.exists(), retained)
                self.assertEqual("retainedProfileDirectory" in record, retained)
                fixtures.return_value.close.assert_called_once()

    def test_existing_evidence_is_never_reused_or_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "evidence"
            output.mkdir()
            old = output / "evaluation.json"
            old.write_text("old evidence")
            with mock.patch.object(runner, "app_identity", return_value={}), \
                    mock.patch.object(runner, "preflight", return_value={}), \
                    mock.patch.object(runner, "FixtureServer") as fixtures:
                result = runner.main(["--app", "/fixture", "--scenario", "startup",
                                      "--validation-run", "--output", str(output)])
            self.assertEqual(result, 7)
            self.assertEqual(old.read_text(), "old evidence")
            fixtures.assert_not_called()


if __name__ == "__main__":
    unittest.main()
