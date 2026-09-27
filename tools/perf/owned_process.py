"""Bounded cleanup for subprocesses launched in a session owned by the harness."""

import os
import signal
import subprocess


class CleanupError(RuntimeError):
    """The harness must retain the profile when its process cannot be reaped."""


def stop(process: subprocess.Popen, grace: float = 0,
         escalation: float = 5) -> None:
    """Only pass a Popen created by this harness with start_new_session=True.

    No process lookup, foreign PID discovery or blanket kill is performed.
    A process that already exited is reaped without signalling its old PID.
    """
    try:
        _stop(process, grace, escalation)
    except (OSError, KeyboardInterrupt) as error:
        raise CleanupError("cannot stop owned process; temporary profile retained") from error


def _stop(process: subprocess.Popen, grace: float, escalation: float) -> None:
    if process.poll() is not None:
        process.wait()
        return
    if grace:
        try:
            process.wait(timeout=grace)
            return
        except subprocess.TimeoutExpired:
            pass
    for sig in (signal.SIGTERM, signal.SIGKILL):
        if process.poll() is not None:
            process.wait()
            return
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=escalation)
            return
        except subprocess.TimeoutExpired:
            pass
    raise CleanupError("owned process did not exit; temporary profile retained")
