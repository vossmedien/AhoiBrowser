"""Filesystem, process and lock primitives of the release app installation.

Split from installation.py (source line budget): canonical path checks,
owned transaction paths, exclusive and swapping renames, the quiescence
check for running bundle processes, the install lock and the signal guard
used during rollback.
"""

from __future__ import annotations

import contextlib
import ctypes
import errno
import fcntl
import os
import pathlib
import re
import shutil
import signal
import stat
import threading
from typing import Callable, Iterator, Optional, Sequence

from .common import ReleaseError, run, sha256_file, tree_sha256

AT_FDCWD = -2
RENAME_SWAP = 0x00000002
RENAME_EXCL = 0x00000004
LOCK_NAME = ".AhoiBrowser.install.lock"
STAGE_PREFIX = ".AhoiBrowser.stage-"
BACKUP_PREFIX = ".AhoiBrowser.rollback-"
APP_NAME = "AhoiBrowser.app"

ProcessInspector = Callable[[Sequence[pathlib.Path]], list[dict]]


def _exists(path: pathlib.Path) -> bool:
    return os.path.lexists(path)


def _require_canonical_parent(path: pathlib.Path, name: str) -> pathlib.Path:
    if not path.is_absolute():
        raise ReleaseError(f"{name} must be an absolute path")
    parent = path.parent
    if not parent.is_dir() or parent.is_symlink():
        raise ReleaseError(f"{name} parent must be a real directory: {parent}")
    try:
        canonical_parent = parent.resolve(strict=True)
    except OSError as error:
        raise ReleaseError(f"cannot resolve {name} parent: {parent}: {error}") from error
    if canonical_parent != parent:
        raise ReleaseError(f"{name} parent must use its canonical path: {parent}")
    return parent


def _is_within(path: pathlib.Path, parent: pathlib.Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def _require_evidence_file(path: pathlib.Path, name: str) -> None:
    _require_canonical_parent(path, name)
    if path.is_symlink() or not path.is_file():
        raise ReleaseError(f"{name} must be a real file: {path}")
    if path.resolve(strict=True) != path:
        raise ReleaseError(f"{name} must use its canonical path: {path}")


def _require_bundle_path(
    path: pathlib.Path,
    name: str,
    *,
    must_exist: bool,
) -> None:
    _require_canonical_parent(path, name)
    if path.name != APP_NAME:
        raise ReleaseError(f"{name} must be named {APP_NAME}")
    present = _exists(path)
    if must_exist and not present:
        raise ReleaseError(f"{name} is missing: {path}")
    if not present:
        return
    if path.is_symlink() or not path.is_dir():
        raise ReleaseError(f"{name} must be a real app directory: {path}")
    try:
        canonical = path.resolve(strict=True)
    except OSError as error:
        raise ReleaseError(f"cannot resolve {name}: {path}: {error}") from error
    if canonical != path:
        raise ReleaseError(f"{name} must use its canonical path: {path}")


def _marker(path: pathlib.Path) -> tuple[int, int]:
    metadata = path.lstat()
    return metadata.st_dev, metadata.st_ino


def _remove_owned_regular_file(
    path: pathlib.Path,
    *,
    expected_marker: tuple[int, int],
    expected_hash: Optional[str] = None,
    expected_content: Optional[bytes] = None,
) -> None:
    if not _exists(path):
        raise ReleaseError(f"owned installer file disappeared: {path}")
    if path.is_symlink() or not path.is_file() or _marker(path) != expected_marker:
        raise ReleaseError(f"refusing to remove a replaced installer file: {path}")
    if expected_hash is not None and sha256_file(path) != expected_hash:
        raise ReleaseError(f"refusing to remove changed installer file: {path}")
    if expected_content is not None and path.read_bytes() != expected_content:
        raise ReleaseError(f"refusing to remove changed installer marker: {path}")
    path.unlink()


def _token(value: object, fallback: str) -> str:
    text = str(value or fallback)
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", text).strip("-.")
    return (cleaned or fallback)[:48]


def _transaction_path(
    parent: pathlib.Path,
    *,
    prefix: str,
    identity: dict,
    bundle_hash: str,
) -> pathlib.Path:
    version = _token(identity.get("marketingVersion"), "unknown")
    build = _token(identity.get("buildNumber"), "unknown")
    source = _token(identity.get("sourceCommit"), "unknown")[:12]
    return parent / (
        f"{prefix}v{version}-b{build}-s{source}-h{bundle_hash[:12]}.app"
    )


def _require_safe_transaction_path(path: pathlib.Path, parent: pathlib.Path) -> None:
    if path.parent != parent or not path.name.endswith(".app"):
        raise ReleaseError(f"unsafe installer transaction path: {path}")
    if not path.name.startswith((STAGE_PREFIX, BACKUP_PREFIX)):
        raise ReleaseError(f"unsafe installer transaction prefix: {path}")


def _reserve_transaction_path(path: pathlib.Path, parent: pathlib.Path) -> None:
    _require_safe_transaction_path(path, parent)
    try:
        os.mkdir(path, 0o700)
    except FileExistsError as error:
        raise ReleaseError(
            f"installer transaction path already exists; inspect it manually: {path}"
        ) from error
    except OSError as error:
        raise ReleaseError(f"cannot reserve installer transaction path: {path}: {error}") from error


def _remove_transaction_path(
    path: pathlib.Path,
    parent: pathlib.Path,
    *,
    expected_hash: Optional[str] = None,
    expected_marker: Optional[tuple[int, int]] = None,
) -> None:
    if not _exists(path):
        return
    _require_safe_transaction_path(path, parent)
    if path.is_symlink() or not path.is_dir():
        raise ReleaseError(f"refusing to remove unsafe transaction object: {path}")
    if expected_marker is not None and _marker(path) != expected_marker:
        raise ReleaseError(
            f"refusing to remove a replaced transaction directory: {path}"
        )
    if expected_hash is not None and tree_sha256(path) != expected_hash:
        raise ReleaseError(
            f"refusing to remove transaction bundle with unexpected bytes: {path}"
        )
    shutil.rmtree(path)


def _copy_with_ditto(source: pathlib.Path, destination: pathlib.Path) -> None:
    run(["ditto", str(source), str(destination)])


def _renameatx(source: pathlib.Path, destination: pathlib.Path, flags: int) -> None:
    library = ctypes.CDLL(None, use_errno=True)
    try:
        operation = library.renameatx_np
    except AttributeError as error:
        raise ReleaseError(
            "renameatx_np is unavailable; atomic installation is unsupported"
        ) from error
    operation.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    ]
    operation.restype = ctypes.c_int
    result = operation(
        AT_FDCWD,
        os.fsencode(source),
        AT_FDCWD,
        os.fsencode(destination),
        flags,
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise ReleaseError(
            f"atomic rename failed: {source} -> {destination}: "
            f"{os.strerror(error_number)}"
        )


def _exchange(source: pathlib.Path, destination: pathlib.Path) -> None:
    _renameatx(source, destination, RENAME_SWAP)


def _move_exclusive(source: pathlib.Path, destination: pathlib.Path) -> None:
    _renameatx(source, destination, RENAME_EXCL)


def _inspect_bundle_processes(paths: Sequence[pathlib.Path]) -> list[dict]:
    completed = run(["/bin/ps", "-axww", "-o", "pid=,command="])
    needles = [(path, f"{path}/Contents/") for path in paths]
    matches = []
    for raw_line in completed.stdout.decode("utf-8", "replace").splitlines():
        fields = raw_line.strip().split(None, 1)
        if len(fields) != 2 or not fields[0].isdigit():
            continue
        pid = int(fields[0])
        if pid == os.getpid():
            continue
        command = fields[1]
        for bundle, needle in needles:
            if needle in command:
                matches.append(
                    {"pid": pid, "bundle": str(bundle), "command": command}
                )
                break
    return matches


def _require_quiescent(
    paths: Sequence[pathlib.Path],
    inspector: ProcessInspector,
) -> None:
    running = inspector(paths)
    if running:
        details = ", ".join(
            f"PID {item.get('pid', '?')} ({item.get('bundle', 'unknown bundle')})"
            for item in running
        )
        raise ReleaseError(f"quit all AhoiBrowser bundle processes before installation: {details}")


@contextlib.contextmanager
def _installation_lock(parent: pathlib.Path) -> Iterator[None]:
    path = parent / LOCK_NAME
    flags = os.O_RDWR | os.O_CREAT
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as error:
        raise ReleaseError(f"cannot open installer lock {path}: {error}") from error
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
            raise ReleaseError(f"installer lock is not a safe regular file: {path}")
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            if error.errno in (errno.EACCES, errno.EAGAIN):
                raise ReleaseError("another AhoiBrowser installation is in progress") from error
            raise ReleaseError(f"cannot lock AhoiBrowser installation: {error}") from error
        yield
    finally:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)


@contextlib.contextmanager
def _rollback_signals() -> Iterator[None]:
    if threading.current_thread() is not threading.main_thread():
        yield
        return
    caught = (signal.SIGHUP, signal.SIGINT, signal.SIGTERM)
    previous = {number: signal.getsignal(number) for number in caught}

    def interrupt(number: int, _frame: object) -> None:
        raise ReleaseError(f"installation interrupted by signal {number}")

    try:
        for number in caught:
            signal.signal(number, interrupt)
        yield
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)
