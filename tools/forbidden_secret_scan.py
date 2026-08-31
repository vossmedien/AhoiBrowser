#!/usr/bin/env python3
"""Late, value-safe scan for synthetic secrets in captured diagnostics."""

from __future__ import annotations

import argparse
import errno
import json
import os
import re
import stat
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List, Mapping, Optional, Sequence, Tuple


CHUNK_BYTES = 1024 * 1024
OVERLAP_BYTES = 16 * 1024
MAX_CANARIES = 64
MIN_CANARY_BYTES = 24
MAX_CANARY_BYTES = 4096
MAX_CANARY_FILE_BYTES = MAX_CANARIES * (MAX_CANARY_BYTES + 1)
SYNTHETIC_PREFIX = b"AHOI_SYNTHETIC_SECRET_"

PATTERNS: Sequence[Tuple[str, re.Pattern[bytes]]] = (
    (
        "authorization-header",
        re.compile(
            rb"(?i)(?:proxy-)?authorization[ \t]*:[ \t]*"
            rb"(?!(?:<redacted>|\[redacted\]|redacted)(?:[ \t\"'\r\n]|$))"
            rb"(?:basic|bearer|digest|negotiate|ntlm)[ \t]+[^\r\n\"']{1,8192}"
        ),
    ),
    (
        "authorization-field",
        re.compile(
            rb"(?i)[\"'](?:proxy-)?authorization[\"'][ \t]*:[ \t]*[\"']"
            rb"(?!(?:<redacted>|\[redacted\]|redacted)[\"'])"
            rb"(?:basic|bearer|digest|negotiate|ntlm)[ \t]+[^\r\n\"']{1,8192}[\"']"
        ),
    ),
    (
        "cookie-header",
        re.compile(
            rb"(?i)(?:set-)?cookie[ \t]*:[ \t]*"
            rb"(?!(?:<redacted>|\[redacted\]|redacted)(?:[ \t\"'\r\n]|$))"
            rb"[^\r\n\"']{1,8192}"
        ),
    ),
    (
        "cookie-field",
        re.compile(
            rb"(?i)[\"'](?:set-)?cookie[\"'][ \t]*:[ \t]*[\"']"
            rb"(?!(?:<redacted>|\[redacted\]|redacted)[\"'])"
            rb"[^\"'\r\n]{1,8192}[\"']"
        ),
    ),
    (
        "credential-field",
        re.compile(
            rb"(?i)[\"']?(?:password|passwd|access_token|refresh_token|id_token)"
            rb"[\"']?[ \t]*[:=][ \t]*"
            rb"(?!(?:[\"']?(?:<redacted>|\[redacted\]|redacted)[\"']?)"
            rb"(?:[ \t\r\n,;&}\]]|$))"
            rb"(?:[\"'][^\"'\r\n]{1,4096}[\"']|"
            rb"[^\s,;&}\]\"']{1,4096})"
        ),
    ),
    (
        "credential-query",
        re.compile(
            rb"(?i)[?&](?:password|passwd|access_token|refresh_token|id_token)="
            rb"(?!(?:<redacted>|%3Credacted%3E|redacted)(?:[&#\s\"']|$))"
            rb"[^&#\s\"']{1,4096}"
        ),
    ),
)


class ScanConfigurationError(RuntimeError):
    """The scan inputs are unsafe or cannot establish a meaningful gate."""


@dataclass(frozen=True)
class ScanRoot:
    scope: str
    path: Path
    is_directory: bool
    stamp: "FileStamp"


@dataclass(frozen=True)
class FileStamp:
    device: int
    inode: int
    mode: int
    size: int
    modified_ns: int
    changed_ns: int


def _stamp(metadata: os.stat_result) -> FileStamp:
    return FileStamp(
        device=metadata.st_dev,
        inode=metadata.st_ino,
        mode=metadata.st_mode,
        size=metadata.st_size,
        modified_ns=metadata.st_mtime_ns,
        changed_ns=metadata.st_ctime_ns,
    )


def _same_generation(left: FileStamp, right: FileStamp) -> bool:
    return left == right


def _open_flags(*, directory: bool = False) -> int:
    if not hasattr(os, "O_NOFOLLOW") or (
        directory and not hasattr(os, "O_DIRECTORY")
    ):
        raise ScanConfigurationError(
            "descriptor no-follow traversal is unavailable on this platform"
        )
    flags = os.O_RDONLY | os.O_NOFOLLOW
    for name in ("O_CLOEXEC", "O_NONBLOCK"):
        flags |= int(getattr(os, name, 0))
    if directory:
        flags |= os.O_DIRECTORY
    return flags


def _open_verified(
    path: Path,
    *,
    expected: FileStamp,
    directory: bool,
    label: str,
) -> Tuple[int, FileStamp]:
    try:
        descriptor = os.open(path, _open_flags(directory=directory))
    except OSError as error:
        if error.errno == errno.ELOOP:
            raise ScanConfigurationError("%s must not be a symlink" % label) from error
        raise ScanConfigurationError("%s cannot be opened safely" % label) from error
    try:
        opened = _stamp(os.fstat(descriptor))
    except OSError as error:
        os.close(descriptor)
        raise ScanConfigurationError("%s cannot be inspected safely" % label) from error
    expected_type = stat.S_ISDIR if directory else stat.S_ISREG
    if not expected_type(opened.mode) or not _same_generation(expected, opened):
        os.close(descriptor)
        raise ScanConfigurationError("%s changed before it could be read" % label)
    return descriptor, opened


def _read_bounded_descriptor(
    descriptor: int,
    *,
    maximum_bytes: int,
    label: str,
) -> bytes:
    payload = bytearray()
    try:
        while True:
            chunk = os.read(descriptor, min(64 * 1024, maximum_bytes + 1 - len(payload)))
            if not chunk:
                break
            payload.extend(chunk)
            if len(payload) > maximum_bytes:
                raise ScanConfigurationError("%s is larger than the allowed bound" % label)
    except OSError as error:
        raise ScanConfigurationError("%s cannot be read safely" % label) from error
    return bytes(payload)


def _read_canaries_with_stamp(path: Path) -> Tuple[Sequence[bytes], FileStamp]:
    absolute = Path(os.path.abspath(path))
    try:
        initial_metadata = os.lstat(absolute)
    except OSError as error:
        raise ScanConfigurationError("canary file is unavailable") from error
    if stat.S_ISLNK(initial_metadata.st_mode):
        raise ScanConfigurationError("canary file must not be a symlink")
    initial = _stamp(initial_metadata)
    descriptor, opened = _open_verified(
        absolute,
        expected=initial,
        directory=False,
        label="canary file",
    )
    try:
        if os.fstat(descriptor).st_uid != os.getuid() or stat.S_IMODE(opened.mode) & 0o077:
            raise ScanConfigurationError("canary file must have no group/world access")
        if opened.size > MAX_CANARY_FILE_BYTES:
            raise ScanConfigurationError("canary file is larger than the allowed bound")
        payload = _read_bounded_descriptor(
            descriptor,
            maximum_bytes=MAX_CANARY_FILE_BYTES,
            label="canary file",
        )
        try:
            after = _stamp(os.fstat(descriptor))
        except OSError as error:
            raise ScanConfigurationError("canary file cannot be inspected safely") from error
    finally:
        os.close(descriptor)
    if not _same_generation(opened, after) or len(payload) != opened.size:
        raise ScanConfigurationError("canary file changed while it was read")
    lines = payload.splitlines()
    values: List[bytes] = []
    for value in lines:
        if not value:
            continue
        if not value.startswith(SYNTHETIC_PREFIX):
            raise ScanConfigurationError(
                "every canary must use the AHOI_SYNTHETIC_SECRET_ prefix"
            )
        if not MIN_CANARY_BYTES <= len(value) <= MAX_CANARY_BYTES:
            raise ScanConfigurationError("a canary has an unsafe length")
        values.append(value)
    if not values:
        raise ScanConfigurationError("at least one synthetic canary is required")
    if len(values) > MAX_CANARIES or len(set(values)) != len(values):
        raise ScanConfigurationError("canaries must be unique and bounded")
    return tuple(values), opened


def _read_canaries(path: Path) -> Sequence[bytes]:
    return _read_canaries_with_stamp(path)[0]


def _root(scope: str, value: Path) -> ScanRoot:
    absolute = Path(os.path.abspath(value))
    try:
        initial_metadata = os.lstat(absolute)
    except OSError as error:
        raise ScanConfigurationError(
            "a requested %s scan root is missing" % scope
        ) from error
    if stat.S_ISLNK(initial_metadata.st_mode):
        raise ScanConfigurationError("scan roots must not be symlinks")
    try:
        resolved = absolute.resolve(strict=True)
        resolved_metadata = os.lstat(resolved)
    except (OSError, RuntimeError) as error:
        raise ScanConfigurationError(
            "a requested %s scan root cannot be resolved safely" % scope
        ) from error
    if (
        initial_metadata.st_dev != resolved_metadata.st_dev
        or initial_metadata.st_ino != resolved_metadata.st_ino
    ):
        raise ScanConfigurationError("a requested scan root changed during validation")
    if resolved == Path(resolved.anchor) or resolved == Path.home().resolve():
        raise ScanConfigurationError("refusing a broad filesystem or home scan root")
    is_directory = stat.S_ISDIR(resolved_metadata.st_mode)
    if not is_directory and not stat.S_ISREG(resolved_metadata.st_mode):
        raise ScanConfigurationError("scan roots must be regular files or directories")
    return ScanRoot(
        scope=scope,
        path=resolved,
        is_directory=is_directory,
        stamp=_stamp(resolved_metadata),
    )


def _open_child(
    directory_descriptor: int,
    name: str,
    metadata: os.stat_result,
    *,
    directory: bool,
) -> Tuple[int, FileStamp]:
    expected = _stamp(metadata)
    descriptor: Optional[int] = None
    try:
        descriptor = os.open(
            name,
            _open_flags(directory=directory),
            dir_fd=directory_descriptor,
        )
        opened = _stamp(os.fstat(descriptor))
    except OSError as error:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass
        raise ScanConfigurationError("a scan input changed during enumeration") from error
    expected_type = stat.S_ISDIR if directory else stat.S_ISREG
    if not expected_type(opened.mode) or not _same_generation(expected, opened):
        os.close(descriptor)
        raise ScanConfigurationError("a scan input changed during enumeration")
    return descriptor, opened


def _walk_directory(
    directory_descriptor: int,
) -> Iterator[Tuple[int, FileStamp]]:
    try:
        before = _stamp(os.fstat(directory_descriptor))
        with os.scandir(directory_descriptor) as iterator:
            entries = sorted(iterator, key=lambda entry: entry.name)
    except OSError as error:
        raise ScanConfigurationError("a scan directory cannot be enumerated safely") from error
    for entry in entries:
        try:
            metadata = os.stat(
                entry.name,
                dir_fd=directory_descriptor,
                follow_symlinks=False,
            )
        except OSError as error:
            raise ScanConfigurationError("a scan input changed during enumeration") from error
        if stat.S_ISLNK(metadata.st_mode):
            raise ScanConfigurationError("scan roots must not contain symlinks")
        if stat.S_ISDIR(metadata.st_mode):
            child, _opened = _open_child(
                directory_descriptor,
                entry.name,
                metadata,
                directory=True,
            )
            try:
                yield from _walk_directory(child)
            finally:
                os.close(child)
            continue
        if not stat.S_ISREG(metadata.st_mode):
            continue
        yield _open_child(
            directory_descriptor,
            entry.name,
            metadata,
            directory=False,
        )
    try:
        after = _stamp(os.fstat(directory_descriptor))
    except OSError as error:
        raise ScanConfigurationError("a scan directory cannot be inspected safely") from error
    if not _same_generation(before, after):
        raise ScanConfigurationError("a scan directory changed while it was read")


def _files(root: ScanRoot) -> Iterator[Tuple[int, FileStamp]]:
    descriptor, opened = _open_verified(
        root.path,
        expected=root.stamp,
        directory=root.is_directory,
        label="scan root",
    )
    if not root.is_directory:
        yield descriptor, opened
        return
    try:
        yield from _walk_directory(descriptor)
    finally:
        os.close(descriptor)


def _scan_file(
    descriptor: int,
    initial: FileStamp,
    root: ScanRoot,
    canaries: Sequence[bytes],
    file_id: str,
) -> Sequence[Mapping[str, object]]:
    findings: List[Mapping[str, object]] = []
    seen = set()
    overlap = b""
    absolute_offset = 0
    try:
        while True:
            chunk = os.read(descriptor, CHUNK_BYTES)
            if not chunk:
                break
            data = overlap + chunk
            base_offset = absolute_offset - len(overlap)
            for index, canary in enumerate(canaries, start=1):
                cursor = 0
                while True:
                    found = data.find(canary, cursor)
                    if found < 0:
                        break
                    key = ("synthetic-canary-%d" % index, base_offset + found)
                    seen.add(key)
                    cursor = found + 1
            for rule, pattern in PATTERNS:
                for match in pattern.finditer(data):
                    seen.add((rule, base_offset + match.start()))
            overlap = data[-OVERLAP_BYTES:]
            absolute_offset += len(chunk)
        after = _stamp(os.fstat(descriptor))
    except OSError as error:
        raise ScanConfigurationError("a scan input cannot be read safely") from error
    if not _same_generation(initial, after) or absolute_offset != initial.size:
        raise ScanConfigurationError("a scan input changed while it was read")
    for rule, offset in sorted(seen, key=lambda value: (value[1], value[0])):
        findings.append(
            {
                "scope": root.scope,
                "fileId": file_id,
                "rule": rule,
                "byteOffset": offset,
            }
        )
    return findings


def scan(
    roots: Sequence[ScanRoot],
    *,
    canary_file: Path,
) -> Mapping[str, object]:
    if not roots:
        raise ScanConfigurationError("at least one scan root is required")
    canaries, canary_stamp = _read_canaries_with_stamp(canary_file)
    excluded = (canary_stamp.device, canary_stamp.inode)
    findings: List[Mapping[str, object]] = []
    scanned = 0
    deduplicated = set()
    for root in roots:
        root_scanned = 0
        for descriptor, file_stamp in _files(root):
            try:
                file_identity = (file_stamp.device, file_stamp.inode)
                if file_identity == excluded:
                    continue
                identity = (root.scope,) + file_identity
                if identity in deduplicated:
                    continue
                deduplicated.add(identity)
                scanned += 1
                root_scanned += 1
                findings.extend(
                    _scan_file(
                        descriptor,
                        file_stamp,
                        root,
                        canaries,
                        "file-%06d" % scanned,
                    )
                )
            finally:
                os.close(descriptor)
        if root_scanned == 0:
            raise ScanConfigurationError(
                "every requested scan root must contain an eligible regular file"
            )
    if scanned == 0:
        raise ScanConfigurationError("no eligible evidence files were scanned")
    return {
        "schemaVersion": 1,
        "clean": not findings,
        "filesScanned": scanned,
        "rootCount": len(roots),
        "canaryCount": len(canaries),
        "canaryValuesRetained": False,
        "findings": findings,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--canary-file",
        type=Path,
        required=True,
        help="owner-only file containing one AHOI_SYNTHETIC_SECRET_ canary per line",
    )
    parser.add_argument("--netlog", action="append", type=Path, default=[])
    parser.add_argument("--crash", action="append", type=Path, default=[])
    parser.add_argument("--evidence", action="append", type=Path, default=[])
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    requested = (
        [("netlog", path) for path in args.netlog]
        + [("crash", path) for path in args.crash]
        + [("evidence", path) for path in args.evidence]
    )
    if not requested:
        print("at least one --netlog, --crash, or --evidence root is required", file=sys.stderr)
        return 2
    try:
        roots = tuple(_root(scope, path) for scope, path in requested)
        result = scan(roots, canary_file=args.canary_file)
    except ScanConfigurationError as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["clean"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
