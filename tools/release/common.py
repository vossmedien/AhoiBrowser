#!/usr/bin/env python3
"""Shared deterministic primitives for AhoiBrowser release evidence."""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import os
import pathlib
import plistlib
import subprocess
import tempfile
from typing import Any, Iterable, Optional, Sequence


class ReleaseError(RuntimeError):
    """A fail-closed release contract violation."""


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def load_json(path: pathlib.Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReleaseError(f"cannot read valid JSON: {path}: {error}") from error


def require_object(value: object, name: str) -> dict:
    if not isinstance(value, dict):
        raise ReleaseError(f"{name} must be an object")
    return value


def require_exact_keys(value: dict, expected: Iterable[str], name: str) -> None:
    expected_set = set(expected)
    actual_set = set(value)
    missing = sorted(expected_set - actual_set)
    extra = sorted(actual_set - expected_set)
    if missing:
        raise ReleaseError(f"{name} is missing fields: {', '.join(missing)}")
    if extra:
        raise ReleaseError(f"{name} has unexpected fields: {', '.join(extra)}")


def require_string(value: object, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ReleaseError(f"{name} must be a non-empty string")
    return value


def require_sha256(value: object, name: str) -> str:
    text = require_string(value, name)
    if len(text) != 64 or any(character not in "0123456789abcdef" for character in text):
        raise ReleaseError(f"{name} must be a lowercase SHA-256")
    return text


def require_utc_timestamp(value: object, name: str) -> str:
    text = require_string(value, name)
    if "T" not in text or not text.endswith("Z"):
        raise ReleaseError(f"{name} must use canonical UTC Z notation")
    try:
        parsed = dt.datetime.fromisoformat(text[:-1] + "+00:00")
    except ValueError as error:
        raise ReleaseError(f"{name} must be a valid ISO-8601 timestamp") from error
    if parsed.utcoffset() != dt.timedelta(0):
        raise ReleaseError(f"{name} must be UTC")
    return text


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    if not path.is_file():
        raise ReleaseError(f"file is missing: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_sha256(root: pathlib.Path) -> str:
    """Hash names, types, modes, symlink targets, and bytes for an exact tree."""
    if not root.is_dir() or root.is_symlink():
        raise ReleaseError(f"bundle tree is missing or invalid: {root}")
    digest = hashlib.sha256(b"ahoi-tree-sha256-v1\0")
    paths = sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix())
    for path in paths:
        relative = path.relative_to(root).as_posix().encode("utf-8")
        metadata = path.lstat()
        digest.update(relative)
        digest.update(b"\0")
        digest.update(oct(metadata.st_mode & 0o7777).encode("ascii"))
        digest.update(b"\0")
        if path.is_symlink():
            digest.update(b"L\0")
            digest.update(os.readlink(path).encode("utf-8"))
        elif path.is_dir():
            digest.update(b"D")
        elif path.is_file():
            digest.update(b"F\0")
            with path.open("rb") as handle:
                for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(chunk)
        else:
            raise ReleaseError(f"unsupported filesystem entry in bundle: {path}")
        digest.update(b"\0")
    return digest.hexdigest()


def legacy_bundle_sha256(root: pathlib.Path) -> str:
    """Match the schema-v2 build provenance bundle hash exactly."""
    if not root.is_dir():
        raise ReleaseError(f"bundle tree is missing: {root}")
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def atomic_write(path: pathlib.Path, content: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=str(path.parent)
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def atomic_write_json(path: pathlib.Path, value: object) -> None:
    atomic_write(path, canonical_json(value))


def run(
    arguments: Sequence[str],
    *,
    cwd: Optional[pathlib.Path] = None,
    input_bytes: Optional[bytes] = None,
) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(
            list(arguments),
            cwd=str(cwd) if cwd else None,
            input=input_bytes,
            check=True,
            capture_output=True,
        )
    except FileNotFoundError as error:
        raise ReleaseError(f"required command is missing: {arguments[0]}") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or b"").decode(
            "utf-8", "replace"
        ).strip()
        raise ReleaseError(
            f"command failed ({' '.join(arguments)}): {detail or error.returncode}"
        ) from error


def safe_relative(reference: object, name: str) -> pathlib.PurePosixPath:
    text = require_string(reference, name)
    path = pathlib.PurePosixPath(text)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ReleaseError(f"{name} must be a safe relative path")
    return path


def resolved_child(root: pathlib.Path, reference: object, name: str) -> pathlib.Path:
    relative = safe_relative(reference, name)
    candidate = (root / pathlib.Path(*relative.parts)).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as error:
        raise ReleaseError(f"{name} escapes its evidence directory") from error
    return candidate


def read_bundle_plist(app: pathlib.Path) -> dict:
    path = app / "Contents/Info.plist"
    try:
        with path.open("rb") as handle:
            value = plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException) as error:
        raise ReleaseError(f"cannot read bundle Info.plist: {path}") from error
    if not isinstance(value, dict):
        raise ReleaseError("bundle Info.plist must contain a dictionary")
    return value


def bundle_identity(app: pathlib.Path) -> dict:
    plist = read_bundle_plist(app)
    fields = {
        "name": "CFBundleName",
        "identifier": "CFBundleIdentifier",
        "marketingVersion": "CFBundleShortVersionString",
        "buildNumber": "CFBundleVersion",
        "productVersion": "AhoiProductVersion",
        "channel": "AhoiUpdateChannel",
        "sourceCommit": "AhoiSourceCommit",
        "chromiumVersion": "AhoiChromiumVersion",
        "chromiumCommit": "AhoiChromiumCommit",
        "gnArgsSha256": "AhoiGNArgsSHA256",
        "buildProfile": "AhoiBuildProfile",
    }
    identity = {}
    for destination, source in fields.items():
        identity[destination] = require_string(plist.get(source), f"Info.plist {source}")
    executable_name = require_string(
        plist.get("CFBundleExecutable"), "Info.plist CFBundleExecutable"
    )
    executable = app / "Contents/MacOS" / executable_name
    if not executable.is_file():
        raise ReleaseError(f"main executable is missing: {executable}")
    identity["executable"] = executable_name
    identity["executableSha256"] = sha256_file(executable)
    identity["bundleTreeSha256"] = tree_sha256(app)
    return identity
