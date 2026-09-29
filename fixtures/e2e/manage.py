#!/usr/bin/env python3
"""Explicit lifecycle CLI for the general AhoiBrowser local HTTPS fixture."""

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
from pathlib import Path
from typing import Dict, Mapping, Optional, Sequence
from urllib.parse import urlsplit

from certificates import (
    REMOVE_CONFIRMATION,
    SIMULATOR_TRUST_CANCEL_CONFIRMATION,
    SIMULATOR_TRUST_CONFIRMATION,
    SIMULATOR_TRUST_FINALIZE_CONFIRMATION,
    TRUST_CONFIRMATION,
    TRUST_RECEIPT_MIGRATION_CONFIRMATION,
    CertificateError,
    cancel_pending_simulator_trust,
    finalize_deleted_simulator_trust,
    generate,
    install_simulator_trust,
    install_trust,
    migrate_legacy_trust_receipt,
    read_trust_receipt,
    remove_certificate_material,
    remove_trust,
    trust_installation_is_valid,
    validated_manifest,
)
import custom_protocol
from server import FixtureCluster, SYNTHETIC_PASSWORD, SYNTHETIC_USERNAME


SCRIPT = Path(__file__).resolve()
FIXTURE_DIRECTORY = SCRIPT.parent
DEFAULT_STATE_DIRECTORY = Path(tempfile.gettempdir()) / (
    "ahoibrowser-e2e-%d" % os.getuid()
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


def _write_state(directory: Path, state: Mapping[str, object]) -> None:
    pending = directory / (STATE_FILE_NAME + ".pending")
    pending.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    pending.replace(_state_path(directory))


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _pid_is_fixture(pid: int, state_directory: Path) -> bool:
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


def _health_is_ready(url: str, ca_certificate: Path) -> bool:
    parsed = urlsplit(url)
    context = ssl.create_default_context(cafile=str(ca_certificate))
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
    except (OSError, ssl.SSLError):
        return False
    finally:
        connection.close()


def command_generate(args: argparse.Namespace) -> int:
    try:
        manifest = generate(args.state_dir.resolve(), rotate=args.rotate)
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(manifest, indent=2, sort_keys=True))
    print(
        "No trust store was modified. Run trust-install or "
        "simulator-trust-install explicitly before start."
    )
    return 0


def command_trust_install(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("trust-install is supported only for the macOS user keychain", file=sys.stderr)
        return 1
    try:
        receipt = install_trust(
            args.state_dir.resolve(),
            confirmation=args.confirm,
            keychain=args.keychain,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2, sort_keys=True))
    print("The generated local test CA is now trusted in exactly the recorded user keychain.")
    return 0


def command_trust_remove(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("trust-remove is supported only for the macOS user keychain", file=sys.stderr)
        return 1
    try:
        removed = remove_trust(
            args.state_dir.resolve(),
            confirmation=args.confirm,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        "Verified the exact recorded CA absent and cleared its trust obligation."
        if removed
        else "No trust receipt exists; nothing was removed."
    )
    return 0


def command_trust_receipt_migrate(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("trust-receipt-migrate requires macOS trust verification", file=sys.stderr)
        return 1
    try:
        receipt = migrate_legacy_trust_receipt(
            args.state_dir.resolve(),
            confirmation=args.confirm,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    if receipt is None:
        print("No trust receipt exists; nothing was migrated.")
    else:
        print(json.dumps(receipt, indent=2, sort_keys=True))
    return 0


def command_simulator_trust_install(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("simulator-trust-install requires macOS and Xcode simctl", file=sys.stderr)
        return 1
    try:
        receipt = install_simulator_trust(
            args.state_dir.resolve(),
            confirmation=args.confirm,
            udid=args.udid,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2, sort_keys=True))
    print("The generated local test CA is trusted only by the exact recorded simulator.")
    return 0


def command_simulator_trust_finalize(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("simulator-trust-finalize requires macOS and Xcode simctl", file=sys.stderr)
        return 1
    try:
        removed = finalize_deleted_simulator_trust(
            args.state_dir.resolve(),
            confirmation=args.confirm,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        "Removed the receipt for the verified-deleted simulator."
        if removed
        else "No trust receipt exists; nothing was finalized."
    )
    return 0


def command_simulator_trust_cancel(args: argparse.Namespace) -> int:
    if sys.platform != "darwin":
        print("simulator-trust-cancel requires macOS and Xcode simctl", file=sys.stderr)
        return 1
    try:
        removed = cancel_pending_simulator_trust(
            args.state_dir.resolve(),
            confirmation=args.confirm,
        )
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        "Verified the exact CA absent and cleared the pending simulator receipt."
        if removed
        else "No trust receipt exists; nothing was cancelled."
    )
    return 0


def command_start(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    try:
        manifest = validated_manifest(state_directory)
        trust = read_trust_receipt(state_directory)
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    if trust is None:
        print(
            "refusing browser fixture start without an explicit trust-install receipt",
            file=sys.stderr,
        )
        return 1
    if trust.get("schemaVersion") in (1, 2):
        print(
            "refusing start until the legacy trust receipt is explicitly migrated",
            file=sys.stderr,
        )
        return 1
    if not trust_installation_is_valid(state_directory):
        print(
            "refusing start because the exact recorded trust target or CA is not valid",
            file=sys.stderr,
        )
        return 1
    current = _read_state(state_directory)
    if current is not None:
        pid = int(current.get("pid", 0))
        if pid > 0 and _pid_alive(pid) and _pid_is_fixture(pid, state_directory):
            print(json.dumps(current, indent=2, sort_keys=True))
            print("HTTPS E2E fixture already running; start is idempotent.")
            return 0
        _state_path(state_directory).unlink(missing_ok=True)

    service_log = state_directory / "service.log"
    command = [
        sys.executable,
        str(SCRIPT),
        "_serve",
        "--state-dir",
        str(state_directory),
        "--first-port",
        str(args.first_port),
        "--third-port",
        str(args.third_port),
        "--media-port",
        str(args.media_port),
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
    _SPAWNED_PROCESSES[str(state_directory)] = process
    deadline = time.monotonic() + args.startup_timeout
    ca_certificate = Path(str(manifest["caCertificate"]))
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        state = _read_state(state_directory)
        if state is not None and int(state.get("pid", 0)) == process.pid:
            url = str(state.get("firstPartyHttpsUrl", ""))
            if url and _health_is_ready(url, ca_certificate):
                print(json.dumps(state, indent=2, sort_keys=True))
                return 0
        time.sleep(0.1)
    print("fixture did not become ready; see %s" % service_log, file=sys.stderr)
    if process.poll() is None and _pid_is_fixture(process.pid, state_directory):
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
    if process.poll() is not None:
        _SPAWNED_PROCESSES.pop(str(state_directory), None)
    return 1


def _trust_diagnostics(
    state_directory: Path,
) -> tuple[Optional[Mapping[str, object]], bool, Optional[str]]:
    try:
        trust = read_trust_receipt(state_directory)
    except CertificateError as error:
        return None, False, str(error)
    return (
        trust,
        trust is not None and trust_installation_is_valid(state_directory),
        None,
    )


def command_status(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state = _read_state(state_directory)
    trust, trust_valid, trust_error = _trust_diagnostics(state_directory)
    trust_type = (
        trust.get("trustType", "macosUserKeychain") if trust is not None else None
    )
    trust_fields = {
        "trustInstalledByFixture": trust_valid,
        "trustReceiptError": trust_error,
        "trustReceiptMigrationRequired": (
            trust is not None and trust.get("schemaVersion") in (1, 2)
        ),
        "trustReceiptSchemaVersion": (
            trust.get("schemaVersion") if trust is not None else None
        ),
        "trustReceiptState": (
            trust.get("installationState", "installed") if trust is not None else None
        ),
        "trustType": trust_type,
    }
    if state is None:
        print(
            json.dumps(
                {
                    "running": False,
                    **trust_fields,
                    "customProtocolHandler": custom_protocol.status(state_directory),
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 1
    pid = int(state.get("pid", 0))
    running = pid > 0 and _pid_alive(pid) and _pid_is_fixture(pid, state_directory)
    value = dict(state)
    value["running"] = running
    value.update(trust_fields)
    value["customProtocolHandler"] = custom_protocol.status(state_directory)
    print(json.dumps(value, indent=2, sort_keys=True))
    return 0 if running else 1


def command_stop(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    state = _read_state(state_directory)
    if state is None:
        print("HTTPS E2E fixture already stopped; stop is idempotent.")
        return 0
    pid = int(state.get("pid", 0))
    if pid <= 0 or not _pid_alive(pid):
        _state_path(state_directory).unlink(missing_ok=True)
        print("Removed stale fixture state; no process was running.")
        return 0
    if not _pid_is_fixture(pid, state_directory):
        print("Refusing to signal PID %d because it is not this fixture." % pid, file=sys.stderr)
        return 2
    os.kill(pid, signal.SIGTERM)
    spawned = _SPAWNED_PROCESSES.get(str(state_directory))
    if spawned is not None and spawned.pid == pid:
        try:
            spawned.wait(timeout=args.shutdown_timeout)
        except subprocess.TimeoutExpired:
            print("fixture did not stop; no forced kill was sent", file=sys.stderr)
            return 1
        _SPAWNED_PROCESSES.pop(str(state_directory), None)
        print("HTTPS E2E fixture stopped. Trust is unchanged; use its matching cleanup.")
        return 0
    deadline = time.monotonic() + args.shutdown_timeout
    while time.monotonic() < deadline:
        if not _pid_alive(pid):
            print("HTTPS E2E fixture stopped. Trust is unchanged; use its matching cleanup.")
            return 0
        time.sleep(0.1)
    print("fixture did not stop; no forced kill was sent", file=sys.stderr)
    return 1


def command_cleanup(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    if custom_protocol.is_installed(state_directory):
        print("remove the isolated custom-protocol handler before cleanup", file=sys.stderr)
        return 1
    state = _read_state(state_directory)
    if state is not None:
        pid = int(state.get("pid", 0))
        if pid > 0 and _pid_alive(pid):
            print("stop the fixture before cleanup", file=sys.stderr)
            return 1
        _state_path(state_directory).unlink(missing_ok=True)
    try:
        cleanup = remove_certificate_material(state_directory)
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    removed_count = int(cleanup["removedMaterialCount"])
    if removed_count:
        manifest_result = (
            "removed its manifest"
            if cleanup["manifestRemoved"]
            else "no manifest remained to remove"
        )
        print(
            "Completed journaled removal of %d canonical certificate/key files; %s. "
            "No tracked path, hard link, byte-identical copy, or "
            "same-type PEM/DER-identical state-local copy remained. Alternate key "
            "encodings and raw DER certificate copies are outside this scan. "
            "Privacy-safe logs were retained."
            % (removed_count, manifest_result)
        )
    else:
        print(
            "No tracked fixture certificate/key material was present. "
            "Privacy-safe logs were retained."
        )
    return 0


def command_protocol_install(args: argparse.Namespace) -> int:
    try:
        value = custom_protocol.install(
            args.state_dir, confirmation=args.confirm
        )
    except custom_protocol.ProtocolHandlerError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(value, indent=2, sort_keys=True))
    print("Installed only the exact fixture URL handler in the fixture state directory.")
    return 0


def command_protocol_status(args: argparse.Namespace) -> int:
    value = custom_protocol.status(args.state_dir)
    print(json.dumps(value, indent=2, sort_keys=True))
    return 0 if value["installed"] else 1


def command_protocol_remove(args: argparse.Namespace) -> int:
    try:
        removed = custom_protocol.remove(
            args.state_dir, confirmation=args.confirm
        )
    except custom_protocol.ProtocolHandlerError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        "Removed the exact fixture custom-protocol handler."
        if removed
        else "No fixture custom-protocol handler exists; nothing was removed."
    )
    return 0


def command_config(args: argparse.Namespace) -> int:
    state_directory = args.state_dir.resolve()
    trust, trust_valid, trust_error = _trust_diagnostics(state_directory)
    print(
        json.dumps(
            {
                "syntheticCredentials": {
                    "username": SYNTHETIC_USERNAME,
                    "password": SYNTHETIC_PASSWORD,
                },
                "trustInstalledByFixture": trust_valid,
                "trustReceiptError": trust_error,
                "trustReceiptMigrationRequired": (
                    trust is not None and trust.get("schemaVersion") in (1, 2)
                ),
                "trustType": (
                    trust.get("trustType", "macosUserKeychain")
                    if trust is not None
                    else None
                ),
                "warning": "Repository-public synthetic values only. Never substitute real secrets.",
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
    try:
        manifest = validated_manifest(state_directory)
        trust = read_trust_receipt(state_directory)
    except CertificateError as error:
        print(str(error), file=sys.stderr)
        return 1
    if trust is None or not trust_installation_is_valid(state_directory):
        print("certificate manifest and explicit trust receipt are required", file=sys.stderr)
        return 1
    stopping = threading.Event()

    def request_stop(_signum: int, _frame: object) -> None:
        stopping.set()

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    cluster = FixtureCluster(
        runtime_directory=state_directory,
        leaf_certificate=Path(str(manifest["leafCertificate"])),
        leaf_private_key=Path(str(manifest["leafPrivateKey"])),
        first_port=args.first_port,
        third_port=args.third_port,
        media_port=args.media_port,
    ).start()
    state = dict(
        cluster.describe(
            ca_certificate=Path(str(manifest["caCertificate"])),
            leaf_certificate=Path(str(manifest["leafCertificate"])),
        )
    )
    state.update(
        {
            "pid": os.getpid(),
            "startedUnixMs": int(time.time() * 1000),
            "trustInstalled": True,
            "caSha256": manifest["caSha256"],
            "leafSha256": manifest["leafSha256"],
        }
    )
    _write_state(state_directory, state)
    print(json.dumps(state, sort_keys=True), flush=True)
    try:
        while not stopping.wait(0.5):
            pass
    finally:
        cluster.stop()
        current = _read_state(state_directory)
        if current is not None and current.get("instanceId") == state.get("instanceId"):
            _state_path(state_directory).unlink(missing_ok=True)
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

    generate_parser = subparsers.add_parser("generate-certificates", help="create a local CA and CA-signed leaf; never changes trust")
    add_state_dir(generate_parser)
    generate_parser.add_argument("--rotate", action="store_true", help="replace existing untrusted certificate material")
    generate_parser.set_defaults(function=command_generate)

    trust_install = subparsers.add_parser("trust-install", help="explicitly add the generated CA to one macOS user keychain")
    add_state_dir(trust_install)
    trust_install.add_argument("--confirm", required=True, help="exact consent phrase documented in fixtures/e2e/README.md")
    trust_install.add_argument("--keychain", type=Path, help="specific user keychain; defaults to security default-keychain -d user")
    trust_install.set_defaults(function=command_trust_install)

    trust_remove = subparsers.add_parser("trust-remove", help="remove exactly the CA recorded by trust-install")
    add_state_dir(trust_remove)
    trust_remove.add_argument("--confirm", required=True, help="exact cleanup phrase documented in fixtures/e2e/README.md")
    trust_remove.set_defaults(function=command_trust_remove)

    trust_migrate = subparsers.add_parser(
        "trust-receipt-migrate",
        help="explicitly migrate a strictly validated legacy v1/v2 trust receipt",
    )
    add_state_dir(trust_migrate)
    trust_migrate.add_argument(
        "--confirm",
        required=True,
        help="exact phrase: %s" % TRUST_RECEIPT_MIGRATION_CONFIRMATION,
    )
    trust_migrate.set_defaults(function=command_trust_receipt_migrate)

    simulator_trust_install = subparsers.add_parser(
        "simulator-trust-install",
        help="explicitly add the generated CA to one exact booted iOS Simulator",
    )
    add_state_dir(simulator_trust_install)
    simulator_trust_install.add_argument(
        "--udid",
        required=True,
        help="exact uppercase simulator UUID; names and the 'booted' alias are rejected",
    )
    simulator_trust_install.add_argument(
        "--confirm",
        required=True,
        help="exact phrase: %s" % SIMULATOR_TRUST_CONFIRMATION,
    )
    simulator_trust_install.set_defaults(function=command_simulator_trust_install)

    simulator_trust_finalize = subparsers.add_parser(
        "simulator-trust-finalize",
        help="remove only the receipt after the exact simulator was deleted externally",
    )
    add_state_dir(simulator_trust_finalize)
    simulator_trust_finalize.add_argument(
        "--confirm",
        required=True,
        help="exact phrase: %s" % SIMULATOR_TRUST_FINALIZE_CONFIRMATION,
    )
    simulator_trust_finalize.set_defaults(function=command_simulator_trust_finalize)

    simulator_trust_cancel = subparsers.add_parser(
        "simulator-trust-cancel",
        help="clear a pending receipt only after proving the exact CA is absent",
    )
    add_state_dir(simulator_trust_cancel)
    simulator_trust_cancel.add_argument(
        "--confirm",
        required=True,
        help="exact phrase: %s" % SIMULATOR_TRUST_CANCEL_CONFIRMATION,
    )
    simulator_trust_cancel.set_defaults(function=command_simulator_trust_cancel)

    start = subparsers.add_parser("start", help="start all trusted HTTPS origins in the background")
    add_state_dir(start)
    start.add_argument("--first-port", type=int, default=28443)
    start.add_argument("--third-port", type=int, default=28444)
    start.add_argument("--media-port", type=int, default=28445)
    start.add_argument("--startup-timeout", type=float, default=30.0)
    start.set_defaults(function=command_start)

    status = subparsers.add_parser("status", help="show runtime and explicit trust receipt state")
    add_state_dir(status)
    status.set_defaults(function=command_status)

    stop = subparsers.add_parser("stop", help="gracefully stop the background fixture")
    add_state_dir(stop)
    stop.add_argument("--shutdown-timeout", type=float, default=30.0)
    stop.set_defaults(function=command_stop)

    cleanup = subparsers.add_parser(
        "cleanup",
        help="delete keys/certificates after stop and typed trust cleanup; retain logs",
    )
    add_state_dir(cleanup)
    cleanup.set_defaults(function=command_cleanup)

    protocol_install = subparsers.add_parser(
        "protocol-install",
        help="explicitly install the fixture-only exact custom-protocol handler",
    )
    add_state_dir(protocol_install)
    protocol_install.add_argument(
        "--confirm",
        required=True,
        help="exact consent phrase documented in fixtures/e2e/README.md",
    )
    protocol_install.set_defaults(function=command_protocol_install)

    protocol_status = subparsers.add_parser(
        "protocol-status", help="show the isolated custom-protocol handler state"
    )
    add_state_dir(protocol_status)
    protocol_status.set_defaults(function=command_protocol_status)

    protocol_remove = subparsers.add_parser(
        "protocol-remove", help="unregister and remove only the marked fixture handler"
    )
    add_state_dir(protocol_remove)
    protocol_remove.add_argument(
        "--confirm",
        required=True,
        help="exact cleanup phrase documented in fixtures/e2e/README.md",
    )
    protocol_remove.set_defaults(function=command_protocol_remove)

    config = subparsers.add_parser("print-config", help="print repository-public synthetic credentials")
    add_state_dir(config)
    config.set_defaults(function=command_config)

    tests = subparsers.add_parser("run-tests", help="run isolated self-tests without touching system trust")
    tests.set_defaults(function=command_tests)

    serve = subparsers.add_parser("_serve", help=argparse.SUPPRESS)
    add_state_dir(serve)
    serve.add_argument("--first-port", type=int, required=True)
    serve.add_argument("--third-port", type=int, required=True)
    serve.add_argument("--media-port", type=int, required=True)
    serve.set_defaults(function=command_serve)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.function(args))


if __name__ == "__main__":
    raise SystemExit(main())
