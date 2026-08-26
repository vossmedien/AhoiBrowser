#!/usr/bin/env python3
"""Idempotent lifecycle CLI for the AhoiBrowser HTTP-auth fixtures."""

from __future__ import annotations

import argparse
import http.client
import json
import os
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Dict, Mapping, Optional, Sequence
from urllib.parse import urlsplit

from fixture_cluster import FixtureCluster
from fixture_server import CREDENTIALS


SCRIPT = Path(__file__).resolve()
FIXTURE_DIRECTORY = SCRIPT.parent
DEFAULT_STATE_DIRECTORY = Path(tempfile.gettempdir()) / (
    "ahoibrowser-http-auth-%d" % os.getuid()
)
STATE_FILE_NAME = "state.json"
_SPAWNED_PROCESSES: Dict[str, subprocess.Popen[str]] = {}


def _state_path(directory: Path) -> Path:
    return directory / STATE_FILE_NAME


def _read_state(directory: Path) -> Optional[Mapping[str, object]]:
    try:
        value = json.loads(_state_path(directory).read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    return value if isinstance(value, dict) else None


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _pid_is_fixture(pid: int, state_directory: Path) -> bool:
    """Avoid signaling an unrelated process after a stale PID is reused."""

    result = subprocess.run(
        ["ps", "-p", str(pid), "-o", "command="],
        check=False,
        capture_output=True,
        text=True,
    )
    command = result.stdout.strip()
    return (
        result.returncode == 0
        and str(SCRIPT) in command
        and "_serve" in command
        and str(state_directory.resolve()) in command
    )


def _health_is_ready(url: str) -> bool:
    parsed = urlsplit(url)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    # This readiness check is internal fixture plumbing.  Browser tests must
    # trust the generated test certificate and may not disable TLS validation.
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    connection = http.client.HTTPSConnection(
        parsed.hostname,
        parsed.port,
        timeout=1,
        context=context,
    )
    try:
        connection.request("GET", "/__fixture/health")
        response = connection.getresponse()
        response.read()
        return response.status == 200
    except OSError:
        return False
    finally:
        connection.close()


def _public_state(cluster: FixtureCluster, instance_id: str) -> Mapping[str, object]:
    description = dict(cluster.describe())
    description.pop("credentials", None)
    description.update(
        {
            "instance_id": instance_id,
            "pid": os.getpid(),
            "started_unix_ms": int(time.time() * 1000),
            "request_log": str(cluster.runtime_directory / "requests.jsonl"),
        }
    )
    return description


def _write_state(directory: Path, state: Mapping[str, object]) -> None:
    pending = directory / (STATE_FILE_NAME + ".pending")
    pending.write_text(
        json.dumps(state, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    pending.replace(_state_path(directory))


def command_start(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state_directory.mkdir(parents=True, exist_ok=True)
    current = _read_state(state_directory)
    if current is not None:
        pid = int(current.get("pid", 0))
        if pid > 0 and _pid_alive(pid) and _pid_is_fixture(pid, state_directory):
            print(json.dumps(current, indent=2, sort_keys=True))
            print("HTTP-auth fixture already running; start is idempotent.")
            return 0
        try:
            _state_path(state_directory).unlink()
        except FileNotFoundError:
            pass

    service_log = state_directory / "service.log"
    command = [
        sys.executable,
        str(SCRIPT),
        "_serve",
        "--state-dir",
        str(state_directory),
        "--primary-port",
        str(args.primary_port),
        "--secondary-port",
        str(args.secondary_port),
        "--cross-port",
        str(args.cross_port),
        "--http-port",
        str(args.http_port),
        "--proxy-port",
        str(args.proxy_port),
    ]
    with service_log.open("a", encoding="utf-8") as output:
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            text=True,
        )
    # A normal `start` invocation exits and init adopts the service.  Retaining
    # the object also lets in-process lifecycle tests reap it deterministically.
    _SPAWNED_PROCESSES[str(state_directory)] = process

    deadline = time.monotonic() + args.startup_timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        state = _read_state(state_directory)
        if state is not None and int(state.get("pid", 0)) == process.pid:
            primary_url = str(state.get("primary_https_url", ""))
            if primary_url and _health_is_ready(primary_url):
                print(json.dumps(state, indent=2, sort_keys=True))
                return 0
        time.sleep(0.1)

    print("fixture did not become ready; see %s" % service_log, file=sys.stderr)
    if process.poll() is None and _pid_is_fixture(process.pid, state_directory):
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # Do not escalate to SIGKILL automatically.  The PID validation in
            # `stop` can be used for another graceful attempt.
            pass
    if process.poll() is not None:
        _SPAWNED_PROCESSES.pop(str(state_directory), None)
    return 1


def command_status(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state = _read_state(state_directory)
    if state is None:
        print("HTTP-auth fixture is not running.")
        return 1
    pid = int(state.get("pid", 0))
    running = pid > 0 and _pid_alive(pid) and _pid_is_fixture(pid, state_directory)
    print(json.dumps(state, indent=2, sort_keys=True))
    print("running=%s" % str(running).lower())
    return 0 if running else 1


def command_stop(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state = _read_state(state_directory)
    if state is None:
        print("HTTP-auth fixture already stopped; stop is idempotent.")
        return 0
    pid = int(state.get("pid", 0))
    if pid <= 0 or not _pid_alive(pid):
        try:
            _state_path(state_directory).unlink()
        except FileNotFoundError:
            pass
        print("Removed stale fixture state; no process was running.")
        return 0
    if not _pid_is_fixture(pid, state_directory):
        print(
            "Refusing to signal PID %d because it is not this fixture process." % pid,
            file=sys.stderr,
        )
        return 2

    os.kill(pid, signal.SIGTERM)
    spawned = _SPAWNED_PROCESSES.get(str(state_directory))
    if spawned is not None and spawned.pid == pid:
        try:
            spawned.wait(timeout=args.shutdown_timeout)
        except subprocess.TimeoutExpired:
            print(
                "fixture did not stop within the timeout; no forced kill was sent",
                file=sys.stderr,
            )
            return 1
        _SPAWNED_PROCESSES.pop(str(state_directory), None)
        print("HTTP-auth fixture stopped.")
        return 0
    deadline = time.monotonic() + args.shutdown_timeout
    while time.monotonic() < deadline:
        if not _pid_alive(pid):
            print("HTTP-auth fixture stopped.")
            return 0
        time.sleep(0.1)
    print("fixture did not stop within the timeout; no forced kill was sent", file=sys.stderr)
    return 1


def command_config(_args: argparse.Namespace) -> int:
    print(
        json.dumps(
            {
                "credentials": CREDENTIALS,
                "warning": (
                    "Synthetic local-test credentials only. Never substitute real secrets."
                ),
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


def command_tests(_args: argparse.Namespace) -> int:
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "-s",
            str(FIXTURE_DIRECTORY / "tests"),
            "-p",
            "test_*.py",
            "-v",
        ],
        cwd=str(FIXTURE_DIRECTORY),
        check=False,
    )
    return result.returncode


def command_serve(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state_directory.mkdir(parents=True, exist_ok=True)
    request_log_path = state_directory / "requests.jsonl"
    instance_id = str(uuid.uuid4())
    stopping = threading.Event()

    def request_stop(_signum: int, _frame: object) -> None:
        stopping.set()

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)

    with request_log_path.open("a", encoding="utf-8") as request_log:
        cluster = FixtureCluster(
            runtime_directory=state_directory,
            primary_port=args.primary_port,
            secondary_port=args.secondary_port,
            cross_port=args.cross_port,
            http_port=args.http_port,
            proxy_port=args.proxy_port,
            log_stream=request_log,
        )
        cluster.start()
        state = _public_state(cluster, instance_id)
        _write_state(state_directory, state)
        print(json.dumps(state, sort_keys=True), flush=True)
        try:
            while not stopping.wait(0.5):
                pass
        finally:
            cluster.stop()
            current = _read_state(state_directory)
            if current is not None and current.get("instance_id") == instance_id:
                try:
                    _state_path(state_directory).unlink()
                except FileNotFoundError:
                    pass
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_state_dir(target: argparse.ArgumentParser) -> None:
        target.add_argument(
            "--state-dir",
            type=Path,
            default=DEFAULT_STATE_DIRECTORY,
            help="runtime state directory (default: %(default)s)",
        )

    start = subparsers.add_parser("start", help="start all origins in the background")
    add_state_dir(start)
    start.add_argument("--primary-port", type=int, default=18443)
    start.add_argument("--secondary-port", type=int, default=18444)
    start.add_argument("--cross-port", type=int, default=19443)
    start.add_argument("--http-port", type=int, default=18080)
    start.add_argument("--proxy-port", type=int, default=18081)
    start.add_argument("--startup-timeout", type=float, default=30.0)
    start.set_defaults(function=command_start)

    status = subparsers.add_parser("status", help="show state and process health")
    add_state_dir(status)
    status.set_defaults(function=command_status)

    stop = subparsers.add_parser("stop", help="stop the background fixture")
    add_state_dir(stop)
    stop.add_argument("--shutdown-timeout", type=float, default=30.0)
    stop.set_defaults(function=command_stop)

    config = subparsers.add_parser(
        "print-config", help="print only the public synthetic credentials"
    )
    config.set_defaults(function=command_config)

    tests = subparsers.add_parser("run-tests", help="run the fixture self-tests")
    tests.set_defaults(function=command_tests)

    serve = subparsers.add_parser("_serve", help=argparse.SUPPRESS)
    add_state_dir(serve)
    serve.add_argument("--primary-port", type=int, required=True)
    serve.add_argument("--secondary-port", type=int, required=True)
    serve.add_argument("--cross-port", type=int, required=True)
    serve.add_argument("--http-port", type=int, required=True)
    serve.add_argument("--proxy-port", type=int, required=True)
    serve.set_defaults(function=command_serve)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.function(args))


if __name__ == "__main__":
    raise SystemExit(main())
