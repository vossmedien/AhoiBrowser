#!/usr/bin/env python3
"""Fail-closed manifest, receipt, and certificate-material state handling."""

from __future__ import annotations

import base64
import binascii
import hashlib
import json
import os
import re
import secrets
import stat
from pathlib import Path
from typing import Mapping, Optional, Sequence


CERTIFICATE_MANIFEST = "certificates.json"
TRUST_RECEIPT = "trust-receipt.json"
CLEANUP_JOURNAL = "certificate-cleanup.json"
CERTIFICATE_MATERIAL_FILES = {
    "caCertificate": "ahoi-e2e-ca.pem",
    "caPrivateKey": "ahoi-e2e-ca-key.pem",
    "leafCertificate": "ahoi-e2e-leaf.pem",
    "leafPrivateKey": "ahoi-e2e-leaf-key.pem",
}
PEM_MATERIAL_PATTERN = re.compile(
    rb"\A\s*-----BEGIN (CERTIFICATE|PRIVATE KEY|EC PRIVATE KEY|RSA PRIVATE KEY)-----"
    rb"\s*(.*?)\s*-----END \1-----\s*\Z",
    re.DOTALL,
)
MAX_PEM_MATERIAL_BYTES = 1024 * 1024
MAX_STATE_JSON_BYTES = 1024 * 1024


class CertificateError(RuntimeError):
    """A certificate or explicit-trust operation failed closed."""


def _manifest_path(directory: Path) -> Path:
    return directory / CERTIFICATE_MANIFEST


def _trust_path(directory: Path) -> Path:
    return directory / TRUST_RECEIPT


def _pending_path(path: Path) -> Path:
    return path.with_suffix(path.suffix + ".pending")


def _path_exists(path: Path) -> bool:
    return os.path.lexists(path)


def _fsync_directory(directory: Path) -> None:
    descriptor = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _regular_file_stat(path: Path, *, label: str) -> os.stat_result:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise CertificateError("%s could not be inspected" % label) from error
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
        raise CertificateError(
            "%s must be a regular, single-link state file" % label
        )
    return metadata


def _read_json_regular(path: Path, *, label: str) -> Mapping[str, object]:
    try:
        payload = _read_regular_bytes(
            path,
            label=label,
            require_single_link=True,
            max_bytes=MAX_STATE_JSON_BYTES,
        )
        value = json.loads(payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeError) as error:
        raise CertificateError("%s is unreadable or malformed" % label) from error
    if not isinstance(value, dict):
        raise CertificateError("%s must contain one JSON object" % label)
    return value


def _read_regular_bytes(
    path: Path,
    *,
    label: str,
    require_single_link: bool,
    max_bytes: Optional[int] = None,
) -> bytes:
    """Read one regular file through a no-follow descriptor."""

    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise CertificateError("%s could not be opened safely" % label) from error
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or (
            require_single_link and metadata.st_nlink != 1
        ):
            raise CertificateError(
                "%s must be a regular%s file"
                % (label, ", single-link" if require_single_link else "")
            )
        if max_bytes is not None and metadata.st_size > max_bytes:
            raise CertificateError("%s exceeds its size limit" % label)
        chunks: list[bytes] = []
        total = 0
        while True:
            block = os.read(descriptor, 65536)
            if not block:
                break
            total += len(block)
            if max_bytes is not None and total > max_bytes:
                raise CertificateError("%s exceeds its size limit" % label)
            chunks.append(block)
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _write_json_pending(path: Path, value: Mapping[str, object]) -> Path:
    """Durably stage JSON without following or replacing an existing temp name."""

    pending = _pending_path(path)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(pending, flags, 0o600)
    except OSError as error:
        raise CertificateError(
            "refusing to overwrite an existing or unsafe pending state file"
        ) from error
    try:
        payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode(
            "utf-8"
        )
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise OSError("state-file write made no progress")
            offset += written
        os.fsync(descriptor)
    except BaseException:
        os.close(descriptor)
        raise
    os.close(descriptor)
    _fsync_directory(path.parent)
    return pending


def _commit_json_pending(path: Path) -> None:
    pending = _pending_path(path)
    _regular_file_stat(pending, label="pending state file")
    if _path_exists(path):
        _regular_file_stat(path, label="current state file")
    try:
        os.replace(pending, path)
    except OSError as error:
        raise CertificateError("pending state could not be committed") from error
    _fsync_directory(path.parent)


def _unlink_regular(path: Path, *, label: str) -> None:
    if not _path_exists(path):
        return
    _regular_file_stat(path, label=label)
    try:
        path.unlink()
    except OSError as error:
        raise CertificateError("%s could not be removed" % label) from error
    _fsync_directory(path.parent)


def _trust_artifact_paths(directory: Path) -> tuple[Path, ...]:
    trust_path = _trust_path(directory)
    return tuple(
        path for path in (trust_path, _pending_path(trust_path)) if _path_exists(path)
    )


def _canonical_material_paths(directory: Path) -> Mapping[str, Path]:
    state_directory = directory.expanduser().resolve()
    return {
        key: state_directory / filename
        for key, filename in CERTIFICATE_MATERIAL_FILES.items()
    }


def _manifest_material_paths(
    directory: Path,
    manifest: Mapping[str, object],
    *,
    require_files: bool,
) -> Mapping[str, Path]:
    """Resolve only canonical state-local material paths from one manifest."""

    state_directory = directory.expanduser().resolve()
    expected_paths = _canonical_material_paths(state_directory)
    resolved_paths: dict[str, Path] = {}
    for key, expected in expected_paths.items():
        raw_value = manifest.get(key)
        if not isinstance(raw_value, str) or not raw_value:
            raise CertificateError(
                "certificate manifest is missing the required %s path" % key
            )
        candidate = Path(raw_value).expanduser()
        if candidate.is_absolute():
            candidate_is_canonical = candidate == expected
        else:
            candidate_is_canonical = candidate == Path(expected.name)
            candidate = state_directory / candidate
        if not candidate_is_canonical or candidate != expected:
            raise CertificateError(
                "certificate manifest contains legacy, external, or non-canonical "
                "material paths; no files were touched. Move every referenced file "
                "to its canonical name inside the state directory and update the "
                "manifest before retrying"
            )
        if _path_exists(candidate) and candidate.is_symlink():
            raise CertificateError(
                "certificate manifest material must be a regular state-local file, "
                "not a symbolic link"
            )
        if require_files and not candidate.is_file():
            raise CertificateError(
                "certificate manifest references missing state-local material: %s"
                % key
            )
        resolved_paths[key] = candidate
    return resolved_paths


def read_manifest(directory: Path) -> Optional[Mapping[str, object]]:
    path = _manifest_path(directory)
    if not _path_exists(path):
        return None
    return _read_json_regular(path, label="certificate manifest")


def validated_manifest(
    directory: Path,
    *,
    require_files: bool = True,
) -> Mapping[str, object]:
    """Load a manifest only when every material path is self-contained."""

    state_directory = directory.expanduser().resolve()
    manifest_path = _manifest_path(state_directory)
    if not _path_exists(manifest_path):
        raise CertificateError("generate certificates before using the fixture")
    manifest = read_manifest(state_directory)
    if manifest is None:
        raise CertificateError(
            "certificate manifest is unreadable or malformed; no files were touched"
        )
    if manifest.get("schemaVersion") != 1:
        raise CertificateError(
            "certificate manifest has an unsupported schema; no files were touched"
        )
    material_paths = _manifest_material_paths(
        state_directory,
        manifest,
        require_files=require_files,
    )
    normalized = dict(manifest)
    normalized.update({key: str(path) for key, path in material_paths.items()})
    return normalized


def write_manifest(directory: Path, manifest: Mapping[str, object]) -> None:
    path = _manifest_path(directory)
    if _path_exists(_pending_path(path)):
        raise CertificateError(
            "a pending certificate manifest must be recovered before writing"
        )
    _write_json_pending(path, manifest)
    _commit_json_pending(path)


def file_sha256(path: Path) -> str:
    payload = _read_regular_bytes(
        path,
        label="certificate-state material",
        require_single_link=False,
    )
    return hashlib.sha256(payload).hexdigest()


def _pem_material_identity(path: Path) -> Optional[str]:
    try:
        payload = _read_regular_bytes(
            path,
            label="certificate-state material",
            require_single_link=False,
            max_bytes=MAX_PEM_MATERIAL_BYTES,
        )
    except CertificateError as error:
        if "exceeds its size limit" in str(error):
            return None
        raise
    match = PEM_MATERIAL_PATTERN.fullmatch(payload)
    if match is None:
        return None
    try:
        der = base64.b64decode(re.sub(rb"\s+", b"", match.group(2)), validate=True)
    except (binascii.Error, ValueError) as error:
        raise CertificateError(
            "certificate cleanup found malformed PEM material; no files were touched"
        ) from error
    material_type = (
        "certificate" if match.group(1) == b"CERTIFICATE" else "private-key"
    )
    return "%s:%s" % (material_type, hashlib.sha256(der).hexdigest())


def _material_identity_profile(
    material_paths: Sequence[Path],
) -> tuple[Mapping[int, frozenset[str]], frozenset[str]]:
    raw_by_size: dict[int, set[str]] = {}
    pem_identities: set[str] = set()
    for path in material_paths:
        try:
            stat = path.stat()
        except OSError as error:
            raise CertificateError(
                "certificate cleanup could not stat referenced state-local material"
            ) from error
        if stat.st_nlink != 1:
            raise CertificateError(
                "certificate cleanup found hard-linked certificate/key material; "
                "deleting the state-local name would leave another reference"
            )
        try:
            digest = file_sha256(path)
        except OSError as error:
            raise CertificateError(
                "certificate cleanup could not hash referenced state-local material"
            ) from error
        raw_by_size.setdefault(stat.st_size, set()).add(digest)
        pem_identity = _pem_material_identity(path)
        if pem_identity is not None:
            pem_identities.add(pem_identity)
    return (
        {size: frozenset(digests) for size, digests in raw_by_size.items()},
        frozenset(pem_identities),
    )


def _state_files(directory: Path) -> tuple[Path, ...]:
    files: list[Path] = []

    def fail(error: OSError) -> None:
        raise CertificateError(
            "certificate cleanup could not inventory the state directory"
        ) from error

    for root, directories, filenames in os.walk(
        directory,
        topdown=True,
        onerror=fail,
        followlinks=False,
    ):
        root_path = Path(root)
        symlinked_directories = [
            name for name in directories if (root_path / name).is_symlink()
        ]
        if symlinked_directories:
            raise CertificateError(
                "certificate cleanup cannot prove duplicate absence through a "
                "symlinked state-directory subtree; no files were touched"
            )
        files.extend(root_path / name for name in filenames)
    return tuple(files)


def _duplicate_material_paths(
    directory: Path,
    material_paths: Sequence[Path],
    profile: tuple[Mapping[int, frozenset[str]], frozenset[str]],
) -> tuple[Path, ...]:
    raw_by_size, pem_identities = profile
    excluded = {Path(os.path.abspath(str(path))) for path in material_paths}
    duplicates: list[Path] = []
    for path in _state_files(directory):
        candidate = Path(os.path.abspath(str(path)))
        if candidate in excluded or not candidate.is_file():
            continue
        try:
            size = candidate.stat().st_size
        except OSError as error:
            raise CertificateError(
                "certificate cleanup could not stat a state-directory file"
            ) from error
        try:
            raw_match = (
                size in raw_by_size
                and file_sha256(candidate) in raw_by_size[size]
            )
        except OSError as error:
            raise CertificateError(
                "certificate cleanup could not hash a state-directory file"
            ) from error
        pem_identity = (
            _pem_material_identity(candidate) if pem_identities else None
        )
        if raw_match or (
            pem_identity is not None and pem_identity in pem_identities
        ):
            duplicates.append(candidate)
    return tuple(sorted(duplicates))


def _assert_no_duplicate_material(
    directory: Path,
    material_paths: Sequence[Path],
) -> tuple[Mapping[int, frozenset[str]], frozenset[str]]:
    profile = _material_identity_profile(material_paths)
    duplicates = _duplicate_material_paths(directory, material_paths, profile)
    if duplicates:
        relative = ", ".join(
            str(path.relative_to(directory)) for path in duplicates
        )
        raise CertificateError(
            "certificate cleanup found duplicate certificate/key material (%s); "
            "no files were touched" % relative
        )
    return profile


def _cleanup_journal_path(directory: Path) -> Path:
    return directory / CLEANUP_JOURNAL


def _json_sha256(value: Mapping[str, object]) -> str:
    payload = json.dumps(value, separators=(",", ":"), sort_keys=True).encode(
        "utf-8"
    )
    return hashlib.sha256(payload).hexdigest()


def _cleanup_material_entry(directory: Path, path: Path) -> Mapping[str, object]:
    metadata = _regular_file_stat(path, label="certificate/key material")
    try:
        raw_sha256 = file_sha256(path)
    except OSError as error:
        raise CertificateError("certificate/key material could not be hashed") from error
    return {
        "relativePath": str(path.relative_to(directory)),
        "size": metadata.st_size,
        "rawSha256": raw_sha256,
        "pemIdentity": _pem_material_identity(path),
    }


def _validate_cleanup_journal(
    directory: Path,
    value: Mapping[str, object],
) -> tuple[tuple[Path, ...], tuple[Mapping[int, frozenset[str]], frozenset[str]]]:
    if value.get("schemaVersion") != 1:
        raise CertificateError("certificate cleanup journal has an unsupported schema")
    transaction_id = value.get("transactionId")
    if not isinstance(transaction_id, str) or not re.fullmatch(
        r"[0-9a-f]{32}", transaction_id
    ):
        raise CertificateError("certificate cleanup journal has an invalid transaction ID")
    manifest_sha256 = value.get("manifestSha256")
    if manifest_sha256 is not None and (
        not isinstance(manifest_sha256, str)
        or not re.fullmatch(r"[0-9a-f]{64}", manifest_sha256)
    ):
        raise CertificateError("certificate cleanup journal has an invalid manifest hash")
    entries = value.get("material")
    if not isinstance(entries, list) or not 1 <= len(entries) <= len(
        CERTIFICATE_MATERIAL_FILES
    ):
        raise CertificateError("certificate cleanup journal has invalid material entries")

    canonical_by_relative = {
        path.name: path for path in _canonical_material_paths(directory).values()
    }
    paths: list[Path] = []
    raw_by_size: dict[int, set[str]] = {}
    pem_identities: set[str] = set()
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise CertificateError("certificate cleanup journal entry is invalid")
        relative = entry.get("relativePath")
        size = entry.get("size")
        raw_sha256 = entry.get("rawSha256")
        pem_identity = entry.get("pemIdentity")
        if (
            not isinstance(relative, str)
            or relative not in canonical_by_relative
            or relative in seen
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size < 0
            or not isinstance(raw_sha256, str)
            or not re.fullmatch(r"[0-9a-f]{64}", raw_sha256)
            or (
                pem_identity is not None
                and (
                    not isinstance(pem_identity, str)
                    or not re.fullmatch(
                        r"(?:certificate|private-key):[0-9a-f]{64}",
                        pem_identity,
                    )
                )
            )
        ):
            raise CertificateError("certificate cleanup journal entry is invalid")
        seen.add(relative)
        paths.append(canonical_by_relative[relative])
        raw_by_size.setdefault(size, set()).add(raw_sha256)
        if pem_identity is not None:
            pem_identities.add(pem_identity)
    return (
        tuple(paths),
        (
            {size: frozenset(digests) for size, digests in raw_by_size.items()},
            frozenset(pem_identities),
        ),
    )


def _read_cleanup_journal(
    directory: Path,
) -> Optional[Mapping[str, object]]:
    path = _cleanup_journal_path(directory)
    pending = _pending_path(path)
    final_exists = _path_exists(path)
    pending_exists = _path_exists(pending)
    if not final_exists and not pending_exists:
        return None
    if pending_exists:
        try:
            pending_value = _read_json_regular(
                pending,
                label="pending certificate cleanup journal",
            )
            _validate_cleanup_journal(directory, pending_value)
        except CertificateError:
            if final_exists:
                final_value = _read_json_regular(
                    path,
                    label="certificate cleanup journal",
                )
                _validate_cleanup_journal(directory, final_value)
            _unlink_regular(pending, label="truncated pending cleanup journal")
            return final_value if final_exists else None
        if final_exists:
            final_value = _read_json_regular(path, label="certificate cleanup journal")
            _validate_cleanup_journal(directory, final_value)
            if _json_sha256(final_value) != _json_sha256(pending_value):
                raise CertificateError(
                    "certificate cleanup journal conflict requires manual inspection"
                )
            _unlink_regular(pending, label="duplicate pending cleanup journal")
            return final_value
        _commit_json_pending(path)
    value = _read_json_regular(path, label="certificate cleanup journal")
    _validate_cleanup_journal(directory, value)
    return value


def _create_cleanup_journal(
    directory: Path,
    material_paths: Sequence[Path],
    *,
    manifest_present: bool,
) -> Mapping[str, object]:
    manifest_sha256: Optional[str] = None
    if manifest_present:
        try:
            manifest_sha256 = file_sha256(_manifest_path(directory))
        except OSError as error:
            raise CertificateError("certificate manifest could not be hashed") from error
    journal = {
        "schemaVersion": 1,
        "transactionId": secrets.token_hex(16),
        "manifestSha256": manifest_sha256,
        "duplicateScanScope": "byte-or-same-pem-der-and-hard-links",
        "material": [
            _cleanup_material_entry(directory, path) for path in material_paths
        ],
    }
    path = _cleanup_journal_path(directory)
    _write_json_pending(path, journal)
    _commit_json_pending(path)
    return journal


def _verify_journal_material(
    material_paths: Sequence[Path],
    profile: tuple[Mapping[int, frozenset[str]], frozenset[str]],
) -> None:
    raw_by_size, _pem_identities = profile
    for path in material_paths:
        if not _path_exists(path):
            continue
        metadata = _regular_file_stat(path, label="journaled certificate/key material")
        expected = raw_by_size.get(metadata.st_size, frozenset())
        try:
            matches = file_sha256(path) in expected
        except OSError as error:
            raise CertificateError(
                "journaled certificate/key material could not be hashed"
            ) from error
        if not matches:
            raise CertificateError(
                "journaled certificate/key material changed; cleanup stopped"
            )


def remove_certificate_material(directory: Path) -> Mapping[str, object]:
    """Journal and resume exact state-local certificate/key removal."""

    state_directory = directory.expanduser().resolve()
    if _trust_artifact_paths(state_directory):
        raise CertificateError(
            "remove or finalize the trusted CA's installed/pending trust receipt "
            "before deleting certificate material"
        )
    manifest_path = _manifest_path(state_directory)
    pending_manifest_path = _pending_path(manifest_path)
    if _path_exists(pending_manifest_path):
        raise CertificateError(
            "a pending certificate manifest exists; no certificate material was removed"
        )

    journal = _read_cleanup_journal(state_directory)
    if journal is not None:
        material_paths, profile = _validate_cleanup_journal(
            state_directory,
            journal,
        )
        manifest_sha256 = journal.get("manifestSha256")
        if _path_exists(manifest_path):
            try:
                current_manifest_sha256 = file_sha256(manifest_path)
            except OSError as error:
                raise CertificateError(
                    "journaled certificate manifest could not be hashed"
                ) from error
            if current_manifest_sha256 != manifest_sha256:
                raise CertificateError(
                    "certificate manifest changed during journaled cleanup"
                )
    else:
        manifest_present = _path_exists(manifest_path)
        if manifest_present:
            manifest = validated_manifest(state_directory)
            material_paths = tuple(
                _manifest_material_paths(
                    state_directory,
                    manifest,
                    require_files=True,
                ).values()
            )
        else:
            material_paths = tuple(
                path
                for path in _canonical_material_paths(state_directory).values()
                if _path_exists(path)
            )
            for path in material_paths:
                _regular_file_stat(
                    path,
                    label="untracked canonical certificate/key material",
                )
        if not material_paths:
            return {
                "duplicateScanScope": "byte-or-same-pem-der-and-hard-links",
                "journalResumed": False,
                "manifestRemoved": False,
                "removedMaterialCount": 0,
                "removedThisRun": 0,
            }
        _assert_no_duplicate_material(state_directory, material_paths)
        journal = _create_cleanup_journal(
            state_directory,
            material_paths,
            manifest_present=manifest_present,
        )
        material_paths, profile = _validate_cleanup_journal(
            state_directory,
            journal,
        )

    _verify_journal_material(material_paths, profile)
    if _duplicate_material_paths(state_directory, material_paths, profile):
        raise CertificateError(
            "certificate cleanup found byte- or PEM-identical duplicate material; "
            "the journal was retained"
        )

    removed_this_run = 0
    for path in material_paths:
        if _path_exists(path):
            _unlink_regular(path, label="journaled certificate/key material")
            removed_this_run += 1

    remaining_references = tuple(path for path in material_paths if _path_exists(path))
    remaining_duplicates = _duplicate_material_paths(
        state_directory,
        (),
        profile,
    )
    if (
        remaining_references
        or remaining_duplicates
        or _trust_artifact_paths(state_directory)
    ):
        raise CertificateError(
            "certificate cleanup postcondition failed; the journal was retained"
        )

    manifest_removed = _path_exists(manifest_path)
    if manifest_removed:
        _unlink_regular(manifest_path, label="certificate manifest")
    if _path_exists(manifest_path) or _path_exists(pending_manifest_path):
        raise CertificateError(
            "certificate cleanup postcondition failed because a manifest remains"
        )
    journal_path = _cleanup_journal_path(state_directory)
    _unlink_regular(journal_path, label="certificate cleanup journal")
    if _path_exists(journal_path) or _path_exists(_pending_path(journal_path)):
        raise CertificateError(
            "certificate cleanup postcondition failed because its journal remains"
        )
    return {
        "duplicateScanScope": "byte-or-same-pem-der-and-hard-links",
        "journalResumed": removed_this_run < len(material_paths),
        "manifestRemoved": manifest_removed,
        "removedMaterialCount": len(material_paths),
        "removedThisRun": removed_this_run,
    }
