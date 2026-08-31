#!/usr/bin/env python3
"""Descriptor-pinned state and lock primitives for the protocol fixture."""

from __future__ import annotations

import errno
import fcntl
import json
import os
import secrets
import stat
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Mapping, Optional, Tuple


STATE_DIRECTORY_NAME = "custom-protocol-handler-state"
STATE_MARKER_NAME = "state-identity.json"
LOCK_NAME = ".state.lock"
STATE_SCHEMA_VERSION = 1
MANAGED_BY = "AhoiBrowser fixtures/e2e/protocol_state.py"
MAX_STATE_FILE_BYTES = 512 * 1024
DEFAULT_LOCK_TIMEOUT_SECONDS = 5.0
PROJECT_ROOT = Path(__file__).resolve().parents[2]


class ProtocolStateError(RuntimeError):
    """The fixture state cannot be accessed without widening ownership."""


@dataclass(frozen=True)
class FileStamp:
    device: int
    inode: int
    mode: int
    uid: int
    links: int
    size: int
    modified_ns: int
    changed_ns: int


@dataclass
class ProtocolState:
    root: Path
    path: Path
    directory_descriptor: int
    lock_descriptor: int
    identity: Mapping[str, object]
    marker_payload: bytes


def stamp(metadata: os.stat_result) -> FileStamp:
    return FileStamp(
        device=metadata.st_dev,
        inode=metadata.st_ino,
        mode=metadata.st_mode,
        uid=metadata.st_uid,
        links=metadata.st_nlink,
        size=metadata.st_size,
        modified_ns=metadata.st_mtime_ns,
        changed_ns=metadata.st_ctime_ns,
    )


def _required_flags(*, directory: bool, writable: bool = False) -> int:
    if not hasattr(os, "O_NOFOLLOW") or (directory and not hasattr(os, "O_DIRECTORY")):
        raise ProtocolStateError("safe descriptor-relative state access is unavailable")
    flags = (os.O_RDWR if writable else os.O_RDONLY) | os.O_NOFOLLOW
    flags |= int(getattr(os, "O_CLOEXEC", 0))
    flags |= int(getattr(os, "O_NONBLOCK", 0))
    if directory:
        flags |= os.O_DIRECTORY
    return flags


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def _reject_broad_root(resolved: Path) -> None:
    home = Path.home().resolve()
    project = PROJECT_ROOT.resolve()
    if resolved == Path(resolved.anchor):
        raise ProtocolStateError("refusing a filesystem root as fixture state")
    if resolved == home or _is_within(home, resolved):
        raise ProtocolStateError("refusing a home or home-ancestor fixture state root")
    if (
        resolved == project
        or _is_within(resolved, project)
        or _is_within(project, resolved)
    ):
        raise ProtocolStateError("refusing a project or project-ancestor fixture state root")


def _open_root(root: Path, *, create: bool) -> Tuple[Path, int]:
    absolute = Path(os.path.abspath(root))
    if absolute.is_symlink():
        raise ProtocolStateError("refusing a symlinked fixture state root")
    if create:
        try:
            absolute.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            raise ProtocolStateError("cannot create the fixture state root") from error
    try:
        resolved = absolute.resolve(strict=True)
        initial = os.lstat(absolute)
        resolved_metadata = os.lstat(resolved)
    except (OSError, RuntimeError) as error:
        raise ProtocolStateError("fixture state root is unavailable") from error
    _reject_broad_root(resolved)
    if (
        stat.S_ISLNK(initial.st_mode)
        or not stat.S_ISDIR(initial.st_mode)
        or initial.st_uid != os.getuid()
        or initial.st_dev != resolved_metadata.st_dev
        or initial.st_ino != resolved_metadata.st_ino
    ):
        raise ProtocolStateError("fixture state root ownership or identity is unsafe")
    try:
        descriptor = os.open(resolved, _required_flags(directory=True))
        opened = os.fstat(descriptor)
    except OSError as error:
        raise ProtocolStateError("cannot open the fixture state root safely") from error
    if (
        not stat.S_ISDIR(opened.st_mode)
        or opened.st_uid != os.getuid()
        or opened.st_dev != initial.st_dev
        or opened.st_ino != initial.st_ino
    ):
        os.close(descriptor)
        raise ProtocolStateError("fixture state root changed during validation")
    return resolved, descriptor


def _open_regular_at(
    directory_descriptor: int,
    name: str,
    *,
    writable: bool = False,
) -> int:
    if Path(name).name != name:
        raise ProtocolStateError("state artifact name is unsafe")
    try:
        return os.open(
            name,
            _required_flags(directory=False, writable=writable),
            dir_fd=directory_descriptor,
        )
    except OSError as error:
        raise ProtocolStateError("state artifact cannot be opened safely") from error


def _validate_regular(metadata: os.stat_result, *, owner_only: bool) -> None:
    if (
        not stat.S_ISREG(metadata.st_mode)
        or metadata.st_uid != os.getuid()
        or metadata.st_nlink != 1
        or (owner_only and stat.S_IMODE(metadata.st_mode) & 0o077)
    ):
        raise ProtocolStateError("state artifact ownership is unsafe")


def read_at(
    state: ProtocolState,
    name: str,
    *,
    maximum_bytes: int = MAX_STATE_FILE_BYTES,
    owner_only: bool = True,
) -> Optional[Tuple[bytes, FileStamp]]:
    try:
        descriptor = _open_regular_at(state.directory_descriptor, name)
    except ProtocolStateError as error:
        if isinstance(error.__cause__, OSError) and error.__cause__.errno == errno.ENOENT:
            return None
        raise
    try:
        before_raw = os.fstat(descriptor)
        _validate_regular(before_raw, owner_only=owner_only)
        before = stamp(before_raw)
        if before.size < 0 or before.size > maximum_bytes:
            raise ProtocolStateError("state artifact exceeds its size bound")
        payload = bytearray()
        while len(payload) <= maximum_bytes:
            chunk = os.read(
                descriptor,
                min(64 * 1024, maximum_bytes + 1 - len(payload)),
            )
            if not chunk:
                break
            payload.extend(chunk)
        after = stamp(os.fstat(descriptor))
    except OSError as error:
        raise ProtocolStateError("state artifact cannot be read safely") from error
    finally:
        os.close(descriptor)
    if len(payload) > maximum_bytes:
        raise ProtocolStateError("state artifact exceeds its size bound")
    if before != after or len(payload) != before.size:
        raise ProtocolStateError("state artifact changed while it was read")
    return bytes(payload), before


def read_json_at(
    state: ProtocolState,
    name: str,
) -> Optional[Tuple[Mapping[str, object], bytes, FileStamp]]:
    value = read_at(state, name)
    if value is None:
        return None
    payload, generation = value
    try:
        decoded = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProtocolStateError("state JSON is invalid") from error
    if not isinstance(decoded, dict):
        raise ProtocolStateError("state JSON must be an object")
    return decoded, payload, generation


def _write_all(descriptor: int, payload: bytes) -> None:
    written = 0
    while written < len(payload):
        count = os.write(descriptor, payload[written:])
        if count <= 0:
            raise ProtocolStateError("state artifact write was incomplete")
        written += count
    os.fsync(descriptor)


def write_new_at(state: ProtocolState, name: str, payload: bytes) -> FileStamp:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    flags |= int(getattr(os, "O_CLOEXEC", 0))
    try:
        descriptor = os.open(name, flags, 0o600, dir_fd=state.directory_descriptor)
    except OSError as error:
        raise ProtocolStateError("cannot create an exclusive state artifact") from error
    try:
        _write_all(descriptor, payload)
        metadata = os.fstat(descriptor)
        _validate_regular(metadata, owner_only=True)
        if stat.S_IMODE(metadata.st_mode) != 0o600:
            raise ProtocolStateError("new state artifact permissions are unsafe")
        generation = stamp(metadata)
        os.fsync(state.directory_descriptor)
        return generation
    except (OSError, ProtocolStateError):
        try:
            os.unlink(name, dir_fd=state.directory_descriptor)
        except OSError:
            pass
        raise
    finally:
        os.close(descriptor)


def write_atomic_at(state: ProtocolState, name: str, payload: bytes) -> FileStamp:
    temporary = ".pending-%s" % secrets.token_hex(16)
    write_new_at(state, temporary, payload)
    try:
        os.rename(
            temporary,
            name,
            src_dir_fd=state.directory_descriptor,
            dst_dir_fd=state.directory_descriptor,
        )
        os.fsync(state.directory_descriptor)
    except OSError as error:
        try:
            os.unlink(temporary, dir_fd=state.directory_descriptor)
        except OSError:
            pass
        raise ProtocolStateError("cannot commit the state artifact") from error
    value = read_at(state, name)
    if value is None or value[0] != payload:
        raise ProtocolStateError("committed state artifact did not verify")
    return value[1]


def rename_at(state: ProtocolState, source: str, target: str) -> None:
    if Path(source).name != source or Path(target).name != target:
        raise ProtocolStateError("state rename target is unsafe")
    try:
        os.rename(
            source,
            target,
            src_dir_fd=state.directory_descriptor,
            dst_dir_fd=state.directory_descriptor,
        )
        os.fsync(state.directory_descriptor)
    except OSError as error:
        raise ProtocolStateError("state artifact rename failed") from error


def unlink_at(state: ProtocolState, name: str, *, missing_ok: bool = False) -> None:
    if Path(name).name != name:
        raise ProtocolStateError("state unlink target is unsafe")
    try:
        os.unlink(name, dir_fd=state.directory_descriptor)
        os.fsync(state.directory_descriptor)
    except FileNotFoundError:
        if not missing_ok:
            raise ProtocolStateError("state artifact is missing")
    except OSError as error:
        raise ProtocolStateError("state artifact unlink failed") from error


def _lock(descriptor: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + max(0.0, timeout_seconds)
    while True:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return
        except BlockingIOError as error:
            if time.monotonic() >= deadline:
                raise ProtocolStateError("protocol state is locked by another operation") from error
            time.sleep(0.025)


def _identity_payload(
    root: Path,
    path: Path,
    directory_metadata: os.stat_result,
    lock_metadata: os.stat_result,
) -> Tuple[Mapping[str, object], bytes]:
    identity: Mapping[str, object] = {
        "schemaVersion": STATE_SCHEMA_VERSION,
        "managedBy": MANAGED_BY,
        "stateId": secrets.token_hex(16),
        "uid": os.getuid(),
        "rootPath": str(root),
        "statePath": str(path),
        "stateDevice": directory_metadata.st_dev,
        "stateInode": directory_metadata.st_ino,
        "lockDevice": lock_metadata.st_dev,
        "lockInode": lock_metadata.st_ino,
    }
    payload = (json.dumps(identity, indent=2, sort_keys=True) + "\n").encode("utf-8")
    return identity, payload


def _validate_identity(
    identity: Mapping[str, object],
    *,
    root: Path,
    path: Path,
    directory_metadata: os.stat_result,
    lock_metadata: os.stat_result,
) -> None:
    expected = {
        "schemaVersion": STATE_SCHEMA_VERSION,
        "managedBy": MANAGED_BY,
        "uid": os.getuid(),
        "rootPath": str(root),
        "statePath": str(path),
        "stateDevice": directory_metadata.st_dev,
        "stateInode": directory_metadata.st_ino,
        "lockDevice": lock_metadata.st_dev,
        "lockInode": lock_metadata.st_ino,
    }
    if any(identity.get(key) != value for key, value in expected.items()):
        raise ProtocolStateError("protocol state identity marker does not match")
    state_id = identity.get("stateId")
    if not isinstance(state_id, str) or len(state_id) != 32:
        raise ProtocolStateError("protocol state identity is invalid")


@contextmanager
def locked_state(
    root: Path,
    *,
    create: bool,
    timeout_seconds: float = DEFAULT_LOCK_TIMEOUT_SECONDS,
) -> Iterator[Optional[ProtocolState]]:
    resolved_root, root_descriptor = _open_root(root, create=create)
    created = False
    state_descriptor: Optional[int] = None
    lock_descriptor: Optional[int] = None
    try:
        try:
            os.mkdir(STATE_DIRECTORY_NAME, 0o700, dir_fd=root_descriptor)
            created = True
            os.fsync(root_descriptor)
        except FileExistsError:
            pass
        except OSError as error:
            raise ProtocolStateError("cannot create dedicated protocol state") from error
        try:
            state_descriptor = os.open(
                STATE_DIRECTORY_NAME,
                _required_flags(directory=True),
                dir_fd=root_descriptor,
            )
        except FileNotFoundError:
            if not create:
                yield None
                return
            raise ProtocolStateError("dedicated protocol state disappeared")
        except OSError as error:
            raise ProtocolStateError("cannot open dedicated protocol state") from error
        directory_metadata = os.fstat(state_descriptor)
        state_path = resolved_root / STATE_DIRECTORY_NAME
        path_metadata = os.lstat(state_path)
        if (
            not stat.S_ISDIR(directory_metadata.st_mode)
            or directory_metadata.st_uid != os.getuid()
            or stat.S_IMODE(directory_metadata.st_mode) != 0o700
            or directory_metadata.st_dev != path_metadata.st_dev
            or directory_metadata.st_ino != path_metadata.st_ino
        ):
            raise ProtocolStateError("dedicated protocol state ownership is unsafe")
        lock_flags = os.O_RDWR | os.O_NOFOLLOW | int(getattr(os, "O_CLOEXEC", 0))
        if created:
            lock_flags |= os.O_CREAT | os.O_EXCL
        try:
            lock_descriptor = os.open(
                LOCK_NAME,
                lock_flags,
                0o600,
                dir_fd=state_descriptor,
            )
        except OSError as error:
            raise ProtocolStateError("protocol state lock is unavailable") from error
        lock_metadata = os.fstat(lock_descriptor)
        _validate_regular(lock_metadata, owner_only=True)
        if stat.S_IMODE(lock_metadata.st_mode) != 0o600:
            raise ProtocolStateError("protocol state lock permissions are unsafe")
        _lock(lock_descriptor, timeout_seconds)
        placeholder = ProtocolState(
            root=resolved_root,
            path=state_path,
            directory_descriptor=state_descriptor,
            lock_descriptor=lock_descriptor,
            identity={},
            marker_payload=b"",
        )
        if created:
            identity, marker_payload = _identity_payload(
                resolved_root,
                state_path,
                directory_metadata,
                lock_metadata,
            )
            write_new_at(placeholder, STATE_MARKER_NAME, marker_payload)
        else:
            marker = read_json_at(placeholder, STATE_MARKER_NAME)
            if marker is None:
                raise ProtocolStateError("protocol state identity marker is missing")
            identity, marker_payload, _generation = marker
        _validate_identity(
            identity,
            root=resolved_root,
            path=state_path,
            directory_metadata=directory_metadata,
            lock_metadata=lock_metadata,
        )
        yield ProtocolState(
            root=resolved_root,
            path=state_path,
            directory_descriptor=state_descriptor,
            lock_descriptor=lock_descriptor,
            identity=identity,
            marker_payload=marker_payload,
        )
    finally:
        if lock_descriptor is not None:
            try:
                fcntl.flock(lock_descriptor, fcntl.LOCK_UN)
            except OSError:
                pass
            os.close(lock_descriptor)
        if state_descriptor is not None:
            os.close(state_descriptor)
        os.close(root_descriptor)
