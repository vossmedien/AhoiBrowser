"""Focused checks for owned-command cleanup and reserve admission."""
import importlib.util
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/run_with_disk_reserve.py'
spec = importlib.util.spec_from_file_location('disk_reserve', TOOL)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class DiskReserveTests(unittest.TestCase):
    def test_separate_output_volume_owns_reserve_and_source_guard_is_retained(self):
        with tempfile.TemporaryDirectory() as raw:
            source = pathlib.Path(raw) / 'source'
            output = pathlib.Path(raw) / 'outputs'
            source.mkdir()
            output.mkdir()
            with mock.patch.object(monitor, 'check_work_root') as guard, \
                    mock.patch.object(monitor.shutil, 'disk_usage', return_value=SimpleNamespace(free=9)) as usage, \
                    mock.patch.object(monitor.subprocess, 'Popen') as start:
                self.assertEqual(2, monitor.run(['unused'], source, 10, disk_root=output))
                self.assertEqual([mock.call(source), mock.call(output)], guard.call_args_list)
                usage.assert_called_once_with(output)
                start.assert_not_called()
            with mock.patch.object(monitor, 'check_work_root') as guard, \
                    mock.patch.object(monitor.shutil, 'disk_usage', side_effect=[SimpleNamespace(free=100), SimpleNamespace(free=9)]) as usage:
                self.assertEqual(2, monitor.run([sys.executable, '-c', 'import time; time.sleep(60)'],
                                                source, 10, interval=.05, disk_root=output))
                self.assertEqual([mock.call(source), mock.call(output)] * 2, guard.call_args_list)
                self.assertEqual([mock.call(output)] * 2, usage.call_args_list)

    def test_pressure_refuses_before_start_and_preserves_foreign_process(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            with mock.patch.object(monitor.shutil, 'disk_usage', return_value=SimpleNamespace(free=9)):
                self.assertEqual(2, monitor.run([sys.executable, '-c', 'raise SystemExit(99)'], root, 10))
            foreign = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
            try:
                with mock.patch.object(monitor.shutil, 'disk_usage', side_effect=[SimpleNamespace(free=100), SimpleNamespace(free=9)]):
                    self.assertEqual(2, monitor.run([sys.executable, '-c', 'import time; time.sleep(60)'], root, 10, interval=.05))
                self.assertIsNone(foreign.poll())
            finally:
                foreign.terminate()
                foreign.wait(timeout=5)

    def test_exit_status_and_mount_loss_are_preserved(self):
        with tempfile.TemporaryDirectory() as raw:
            self.assertEqual(7, monitor.run([sys.executable, '-c', 'raise SystemExit(7)'], raw, 1))
            with mock.patch.object(monitor, 'check_work_root', side_effect=[None, ValueError('mount lost')]):
                self.assertEqual(2, monitor.run([sys.executable, '-c', 'import time; time.sleep(60)'], raw, 1, interval=.05))

    def test_parent_signal_cleans_owned_child(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            policy = root / 'policy.json'
            policy.write_text('{"diskSpace":{"safetyReserveBytes":1}}')
            marker = root / 'child.pid'
            child_code = f'import os,pathlib,time; pathlib.Path({str(marker)!r}).write_text(str(os.getpid())); time.sleep(60)'
            parent = subprocess.Popen([sys.executable, str(TOOL), '--work-root', raw,
                                       '--policy', str(policy), '--', sys.executable, '-c', child_code])
            try:
                deadline = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue(marker.exists())
                child_pid = int(marker.read_text())
                parent.send_signal(signal.SIGTERM)
                self.assertEqual(2, parent.wait(timeout=5))
                with self.assertRaises(ProcessLookupError):
                    os.kill(child_pid, 0)
            finally:
                if parent.poll() is None:
                    parent.terminate()
                    parent.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
