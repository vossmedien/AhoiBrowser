"""Candidate-bound owner lease and cancellation for an active H3 run only.

There is no persistent watcher. A monitor lives inside the measurement scope,
and can stop only Popen sessions registered by that scope.
"""

from __future__ import annotations

import contextvars
import datetime as dt
import json
import math
import os
import pathlib
import re
import subprocess
import threading
import time
import uuid

import build_evidence
import owned_process

_CURRENT = contextvars.ContextVar("ahoi_perf_guard", default=None)
MARKER = "Crest-H3-Lease: "


class LeaseError(ValueError):
    pass


class RunCancelled(RuntimeError):
    pass


def current():
    return _CURRENT.get()


def read_grant(path: pathlib.Path, now=None) -> dict:
    try:
        rows = [line[len(MARKER):] for line in path.read_text().splitlines()
                if line.startswith(MARKER)]
        if len(rows) != 1:
            raise LeaseError("owner checkpoint needs exactly one current Crest-H3-Lease marker")
        grant = json.loads(rows[0])
        if grant["status"] != "open" or not isinstance(grant["id"], str) or not grant["id"]:
            raise LeaseError("owner lease is not open")
        expiry = dt.datetime.fromisoformat(grant["expiresAt"].replace("Z", "+00:00"))
        if expiry.tzinfo is None or expiry <= (now or dt.datetime.now(dt.timezone.utc)):
            raise LeaseError("owner lease expired or lacks a time zone")
        if grant["mode"] not in {"budget", "validation"}:
            raise LeaseError("unknown owner lease mode")
        if not isinstance(grant["resources"], list):
            raise LeaseError("owner lease resources missing")
        if not isinstance(grant["bundles"], list) or not grant["bundles"]:
            raise LeaseError("owner lease bundle identities missing")
        for bundle in grant["bundles"]:
            for key in ("binarySha256", "bundleTreeSha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", bundle[key]):
                    raise LeaseError("owner lease needs exact bundle hashes")
        directory = pathlib.Path(grant["lockDirectory"])
        if not directory.is_absolute() or not directory.is_dir():
            raise LeaseError("owner coordination directory is unavailable")
        return grant
    except (OSError, KeyError, TypeError, AttributeError, ValueError) as error:
        if isinstance(error, LeaseError):
            raise
        raise LeaseError("owner lease unavailable or malformed") from error


class LeaseGuard:
    def __init__(self, checkpoint, bundles, initial, probe, min_idle=300,
                 validation=False, installed=False, interval=1.0):
        self.checkpoint = pathlib.Path(checkpoint)
        self.bundles = bundles
        self.initial = initial
        self.probe = probe
        self.min_idle = max(300, min_idle)  # The current owner contract requires at least 300 s.
        self.validation = validation
        self.installed = installed
        self.interval = interval
        self.reason = None
        self.samples = 0
        self.completed = False
        self._mutex = threading.RLock()
        self._processes = {}  # Only harness-spawned Popen objects.
        self.driver_ax_clients = set()  # AX clients seen inside owned sessions.
        self._cancelled = threading.Event()
        self._finished = threading.Event()
        self._thread = None
        self._fd = None
        self._context_token = None
        self._lock_identity = None
        self._grant = None
        self._input_epoch = time.monotonic() - initial.get("hidIdleSeconds", 0)

    def _validate(self, grant):
        if grant["mode"] != ("validation" if self.validation else "budget"):
            raise LeaseError("owner lease mode differs from requested measurement")
        required = set() if self.validation else {"host-quiet"}
        if self.installed:
            required.add("installed-app")
        if not required.issubset(grant["resources"]):
            raise LeaseError("owner lease lacks required shared resources")
        if self._grant is not None:
            for key in ("id", "mode", "bundles", "lockDirectory"):
                if grant[key] != self._grant[key]:
                    raise LeaseError("owner lease replaced during measurement")
        expected = [{key: bundle[key] for key in ("binarySha256", "bundleTreeSha256")}
                    for bundle in self.bundles]
        if grant["bundles"] != expected:
            raise LeaseError("owner lease belongs to different candidate/baseline bundles")

    def _conflicting_lock(self):
        return any((self.directory / name).exists() or (self.directory / name).is_symlink()
                   for name in ("build.lock", "e2e.lock"))

    def _owns_lock(self):
        try:
            info = self.lock_path.lstat()
            return ((info.st_dev, info.st_ino) == self._lock_identity
                    and self.lock_path.read_bytes() == self._lock_bytes)
        except OSError:
            return False

    def _release(self):
        if self._fd is not None:
            try:
                if self._owns_lock():
                    self.lock_path.unlink()
            finally:
                os.close(self._fd)
                self._fd = None

    def __enter__(self):
        grant = read_grant(self.checkpoint)
        # Validation runs may lack a release receipt, but the lease still binds
        # the whole actual bundle. Budget runs reuse the already verified hash.
        self.bundles = [dict(bundle) for bundle in self.bundles]
        for bundle in self.bundles:
            if not bundle.get("bundleTreeSha256"):
                bundle["bundleTreeSha256"] = build_evidence.tree_sha256(pathlib.Path(bundle["path"]))
        self._validate(grant)
        self._grant = grant
        self.directory = pathlib.Path(grant["lockDirectory"])
        self.lock_path = self.directory / "h3.lock"
        if self._conflicting_lock():
            raise LeaseError("owner build/E2E lock blocks measurement")
        self._lock_bytes = json.dumps({"pid": os.getpid(), "leaseId": grant["id"],
                                       "token": uuid.uuid4().hex}).encode()
        try:
            self._fd = os.open(self.lock_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError as error:
            raise LeaseError("H3 lock already held; no stale-lock removal attempted") from error
        try:
            info = os.fstat(self._fd)
            self._lock_identity = (info.st_dev, info.st_ino)
            os.write(self._fd, self._lock_bytes)
            self.poll_once()  # Check again after claiming, before any launch.
            self.check()
            self._context_token = _CURRENT.set(self)
            self._start_monitor()
        except BaseException:
            if self._context_token is not None:
                _CURRENT.reset(self._context_token)
                self._context_token = None
            self._release()
            raise
        return self

    def _start_monitor(self):
        self._thread = threading.Thread(target=self._monitor, name="ahoi-perf-lease", daemon=True)
        self._thread.start()

    def _monitor(self):
        while not self._finished.wait(self.interval):
            self.poll_once()
            if self.reason is not None:
                return

    def poll_once(self):
        if self.reason is not None:
            return
        try:
            self._validate(read_grant(self.checkpoint))
            if not self._owns_lock():
                raise LeaseError("H3 lock removed or replaced")
            if self._conflicting_lock():
                raise LeaseError("owner build/E2E lock appeared")
            state = self.probe()
            self.samples += 1
            self.driver_ax_clients.update(state.get("ownedAccessibilityClients", ()))
            idle = state["hidIdleSeconds"]
            if type(idle) not in (int, float) or not math.isfinite(idle) or idle < 0:
                raise LeaseError("owner-input probe unavailable")
            if ((not self.validation and idle < self.min_idle)
                    or time.monotonic() - idle > self._input_epoch + 2):
                raise LeaseError("owner input detected")
            if not self.validation:
                if state["busyProcesses"]:
                    raise LeaseError("compiler/build activity detected")
                if state["powerSource"] != "ac":
                    raise LeaseError("AC power lost")
                if state["accessibilityClients"] != self.initial["accessibilityClients"]:
                    raise LeaseError("accessibility state changed")
        except LeaseError as error:
            self.abort(str(error))
        except Exception:
            self.abort("runtime lease/host probe failed")

    def abort(self, reason):
        with self._mutex:
            if self.reason is None:
                self.reason = reason
            self._cancelled.set()
        # Interrupt sleeping scenarios immediately and close only owned GUI
        # processes even when the main thread is blocked in a CDP socket call.
        self._stop_all(raise_error=False)

    def check(self):
        if self._cancelled.is_set():
            raise RunCancelled(self.reason)

    def wait(self, seconds):
        self.check()
        self._cancelled.wait(seconds)
        self.check()

    def spawn(self, *args, **kwargs):
        self.poll_once()
        with self._mutex:
            self.check()
            if kwargs.get("start_new_session") is not True:
                raise ValueError("guarded subprocess must own its session")
            process = subprocess.Popen(*args, **kwargs)
            self._processes[process] = threading.RLock()
            return process

    def owned_process_groups(self):
        """Session/group IDs of live harness-spawned processes (start_new_session)."""
        with self._mutex:
            return {process.pid for process in self._processes if process.poll() is None}

    def stop_process(self, process, grace=0):
        with self._mutex:
            lock = self._processes.get(process)
        if lock is None:
            raise ValueError("process is not owned by this measurement")
        with lock:
            owned_process.stop(process, grace=grace)

    def _stop_all(self, raise_error):
        with self._mutex:
            processes = list(self._processes)
        failed = False
        for process in processes:
            try:
                self.stop_process(process)
            except owned_process.CleanupError:
                failed = True
        if failed and raise_error:
            raise owned_process.CleanupError("owned process remains; H3 lock/profile retained")

    def wait_process(self, process, timeout):
        deadline = time.monotonic() + timeout
        while True:
            self.check()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired("owned trace driver", timeout)
            try:
                result = process.wait(timeout=min(0.25, remaining))
                self.check()
                return result
            except subprocess.TimeoutExpired:
                continue

    def summary(self):
        return {"checkpoint": str(self.checkpoint), "leaseId": (self._grant or {}).get("id"),
                "grantSha256": build_evidence.sha(json.dumps(
                    self._grant, sort_keys=True, separators=(",", ":")).encode()) if self._grant else None,
                "expiresAt": (self._grant or {}).get("expiresAt"),
                "pollIntervalSeconds": self.interval, "checks": self.samples,
                "completed": self.completed,
                "cancelled": self.reason is not None, "reason": self.reason,
                "lockRetained": self._fd is not None,
                "driverAccessibilityClients": sorted(self.driver_ax_clients)}

    def __exit__(self, exc_type, exc, traceback):
        if exc_type is None:
            self.poll_once()
        self._finished.set()
        try:
            if self._thread:
                self._thread.join(timeout=30)
                if self._thread.is_alive():
                    raise owned_process.CleanupError("guard did not stop; H3 lock/profile retained")
            self._stop_all(raise_error=True)
            self._release()
        finally:
            if self._context_token is not None:
                _CURRENT.reset(self._context_token)
                self._context_token = None
        if exc_type is None:
            self.check()
            self.completed = True
