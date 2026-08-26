#!/usr/bin/env python3
"""Deterministic debug-symbol and checksum release artifacts."""

from __future__ import annotations

import os
import pathlib
import stat
import tempfile
import zipfile

from .common import (
    ReleaseError,
    atomic_write,
    atomic_write_json,
    load_json,
    require_sha256,
    resolved_child,
    safe_relative,
    sha256_bytes,
    sha256_file,
)


MATERIAL_FIELDS = (
    "componentInventory",
    "sbom",
    "licenseArchive",
    "thirdPartyNotices",
    "correspondingSourceOffer",
)


def _direct_sibling(path: pathlib.Path, root: pathlib.Path, name: str) -> pathlib.Path:
    resolved = path.resolve()
    if resolved.parent != root.resolve() or not resolved.is_file():
        raise ReleaseError(f"{name} must be a file in the release directory")
    return resolved


def _reference(path: pathlib.Path, root: pathlib.Path, name: str) -> dict:
    path = _direct_sibling(path, root, name)
    return {"file": path.name, "sha256": sha256_file(path)}


def _symbol_files(symbols_root: pathlib.Path) -> tuple[pathlib.Path, ...]:
    root = symbols_root.resolve()
    if not root.is_dir() or root.is_symlink():
        raise ReleaseError("debug-symbol root is missing or invalid")
    bundle_candidates = sorted(root.rglob("*.dSYM"))
    if any(path.is_symlink() for path in bundle_candidates):
        raise ReleaseError("debug-symbol input contains a symlinked .dSYM bundle")
    bundles = [path for path in bundle_candidates if path.is_dir()]
    bundle_files: set[pathlib.Path] = set()
    for bundle in bundles:
        files = []
        for path in sorted(bundle.rglob("*")):
            if path.is_symlink():
                raise ReleaseError(f"debug-symbol input contains a symlink: {path}")
            if path.is_file():
                files.append(path)
        if not files:
            raise ReleaseError(f"debug-symbol bundle is empty: {bundle}")
        bundle_files.update(files)
    standalone = []
    for path in sorted(root.rglob("*.sym")):
        if path.is_symlink():
            raise ReleaseError(f"debug-symbol input contains a symlink: {path}")
        if path.is_file() and not any(parent.suffix == ".dSYM" for parent in path.parents):
            standalone.append(path)
    files = tuple(sorted(bundle_files.union(standalone)))
    if not files:
        raise ReleaseError("release output contains no .dSYM bundle or .sym file")
    return files


def _zip_info(relative: str, mode: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | (mode & 0o777)) << 16
    return info


def create_symbol_archive(
    symbols_root: pathlib.Path,
    archive_output: pathlib.Path,
) -> tuple[dict, ...]:
    root = symbols_root.resolve()
    output = archive_output.resolve()
    try:
        output.relative_to(root)
    except ValueError:
        pass
    else:
        raise ReleaseError("debug-symbol archive must be outside the symbol root")
    files = _symbol_files(root)
    members = []
    archive_output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{archive_output.name}.", dir=str(archive_output.parent)
    )
    os.close(descriptor)
    temporary = pathlib.Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w") as archive:
            for path in files:
                relative = path.relative_to(root).as_posix()
                payload = path.read_bytes()
                mode = path.stat().st_mode & 0o777
                archive.writestr(_zip_info(relative, mode), payload)
                members.append(
                    {
                        "file": relative,
                        "sha256": sha256_bytes(payload),
                        "size": len(payload),
                        "mode": f"{mode:04o}",
                    }
                )
        os.replace(temporary, archive_output)
    finally:
        if temporary.exists():
            temporary.unlink()
    return tuple(members)


def _artifact_reference(
    root: pathlib.Path,
    reference: object,
    name: str,
) -> dict:
    if not isinstance(reference, dict):
        raise ReleaseError(f"{name} reference is missing")
    path = resolved_child(root, reference.get("file"), f"{name}.file")
    if any(character in path.name for character in ("\n", "\r")):
        raise ReleaseError(f"{name} filename contains a line break")
    expected = require_sha256(reference.get("sha256"), f"{name}.sha256")
    if path.resolve().parent != root.resolve() or sha256_file(path) != expected:
        raise ReleaseError(f"{name} artifact does not match its receipt")
    size = path.stat().st_size
    recorded_size = reference.get("size")
    if recorded_size is not None and recorded_size != size:
        raise ReleaseError(f"{name} artifact size does not match its receipt")
    return {"file": path.name, "sha256": expected, "size": size}


def _checksummed_artifacts(
    root: pathlib.Path,
    package: dict,
    materials: dict,
    symbol_reference: dict,
) -> tuple[dict, ...]:
    package_artifacts = package.get("artifacts")
    if not isinstance(package_artifacts, dict) or not package_artifacts:
        raise ReleaseError("package receipt contains no artifacts")
    candidates = [
        _artifact_reference(root, reference, f"package.{name}")
        for name, reference in sorted(package_artifacts.items())
    ]
    for field in MATERIAL_FIELDS:
        candidates.append(
            _artifact_reference(root, materials.get(field), f"materials.{field}")
        )
    candidates.append(symbol_reference)
    by_name = {item["file"]: item for item in candidates}
    if len(by_name) != len(candidates):
        raise ReleaseError("release artifacts contain duplicate filenames")
    return tuple(by_name[name] for name in sorted(by_name))


def _checksum_payload(entries: tuple[dict, ...]) -> bytes:
    return "".join(
        f"{entry['sha256']}  {entry['file']}\n" for entry in entries
    ).encode("utf-8")


def create_release_assets(
    *,
    symbols_root: pathlib.Path,
    package_receipt_path: pathlib.Path,
    materials_receipt_path: pathlib.Path,
    symbol_archive_output: pathlib.Path,
    checksums_output: pathlib.Path,
    receipt_output: pathlib.Path,
) -> dict:
    root = receipt_output.resolve().parent
    paths = (
        package_receipt_path,
        materials_receipt_path,
        symbol_archive_output,
        checksums_output,
        receipt_output,
    )
    if any(path.resolve().parent != root for path in paths):
        raise ReleaseError("release assets and receipts must share one directory")
    if len({path.resolve() for path in paths}) != len(paths):
        raise ReleaseError("release asset input and output paths must be distinct")
    package = load_json(package_receipt_path)
    materials = load_json(materials_receipt_path)
    if package.get("kind") != "package-provenance":
        raise ReleaseError("package receipt kind is invalid")
    if materials.get("kind") != "release-materials":
        raise ReleaseError("materials receipt kind is invalid")
    package_artifacts = package.get("artifacts")
    if not isinstance(package_artifacts, dict):
        raise ReleaseError("package receipt contains no artifact inventory")
    protected = set()
    for reference in package_artifacts.values():
        if not isinstance(reference, dict):
            raise ReleaseError("package artifact reference is invalid")
        protected.add(resolved_child(root, reference.get("file"), "package artifact"))
    for field in MATERIAL_FIELDS:
        reference = materials.get(field)
        if not isinstance(reference, dict):
            raise ReleaseError(f"materials receipt is missing {field}")
        protected.add(resolved_child(root, reference.get("file"), f"materials.{field}"))
    outputs = {
        symbol_archive_output.resolve(),
        checksums_output.resolve(),
        receipt_output.resolve(),
    }
    if protected.intersection(outputs):
        raise ReleaseError("release asset output would overwrite a source artifact")
    members = create_symbol_archive(symbols_root, symbol_archive_output)
    symbol_reference = {
        "file": symbol_archive_output.name,
        "sha256": sha256_file(symbol_archive_output),
        "size": symbol_archive_output.stat().st_size,
    }
    entries = _checksummed_artifacts(root, package, materials, symbol_reference)
    atomic_write(checksums_output, _checksum_payload(entries))
    receipt = {
        "schemaVersion": 1,
        "kind": "release-assets",
        "packageReceipt": _reference(package_receipt_path, root, "package receipt"),
        "materialsReceipt": _reference(
            materials_receipt_path, root, "materials receipt"
        ),
        "symbols": {
            **symbol_reference,
            "format": "zip",
            "members": list(members),
        },
        "checksums": {
            "file": checksums_output.name,
            "sha256": sha256_file(checksums_output),
            "format": "sha256sum-v1",
            "entries": list(entries),
        },
    }
    atomic_write_json(receipt_output, receipt)
    return receipt


def _validate_symbol_archive(
    root: pathlib.Path,
    symbols: object,
) -> dict:
    if not isinstance(symbols, dict) or set(symbols) != {
        "file",
        "sha256",
        "size",
        "format",
        "members",
    }:
        raise ReleaseError("release symbol evidence has an invalid shape")
    if symbols["format"] != "zip":
        raise ReleaseError("release symbol archive format is unsupported")
    reference = _artifact_reference(root, symbols, "symbols")
    members = symbols["members"]
    if not isinstance(members, list) or not members:
        raise ReleaseError("release symbol archive has no member inventory")
    expected = {}
    for member in members:
        if not isinstance(member, dict) or set(member) != {
            "file",
            "sha256",
            "size",
            "mode",
        }:
            raise ReleaseError("release symbol member has an invalid shape")
        relative = safe_relative(member["file"], "release symbol member").as_posix()
        if relative in expected:
            raise ReleaseError("release symbol member inventory contains duplicates")
        require_sha256(member["sha256"], "release symbol member SHA-256")
        if not isinstance(member["size"], int) or member["size"] < 0:
            raise ReleaseError("release symbol member size is invalid")
        if not isinstance(member["mode"], str) or len(member["mode"]) != 4:
            raise ReleaseError("release symbol member mode is invalid")
        expected[relative] = member
    archive_path = resolved_child(root, reference["file"], "symbols.file")
    try:
        archive = zipfile.ZipFile(archive_path)
    except zipfile.BadZipFile as error:
        raise ReleaseError("release symbol archive is invalid") from error
    with archive:
        infos = archive.infolist()
        if [item.filename for item in infos] != sorted(expected):
            raise ReleaseError("release symbol archive inventory mismatch")
        for info in infos:
            member = expected[info.filename]
            payload = archive.read(info)
            mode = (info.external_attr >> 16) & 0o777
            if (
                sha256_bytes(payload) != member["sha256"]
                or len(payload) != member["size"]
                or f"{mode:04o}" != member["mode"]
            ):
                raise ReleaseError(
                    f"release symbol archive member mismatch: {info.filename}"
                )
    return reference


def validate_release_assets(
    root: pathlib.Path,
    receipt: dict,
    *,
    package_reference: dict,
    materials_reference: dict,
) -> None:
    if set(receipt) != {
        "schemaVersion",
        "kind",
        "packageReceipt",
        "materialsReceipt",
        "symbols",
        "checksums",
    }:
        raise ReleaseError("release-assets receipt has an invalid shape")
    if receipt.get("schemaVersion") != 1 or receipt.get("kind") != "release-assets":
        raise ReleaseError("release-assets receipt schema/kind is unsupported")
    if receipt["packageReceipt"] != package_reference:
        raise ReleaseError("release assets are not bound to the package receipt")
    if receipt["materialsReceipt"] != materials_reference:
        raise ReleaseError("release assets are not bound to the materials receipt")
    package = load_json(
        resolved_child(root, package_reference["file"], "packageReceipt.file")
    )
    materials = load_json(
        resolved_child(root, materials_reference["file"], "materialsReceipt.file")
    )
    symbol_reference = _validate_symbol_archive(root, receipt["symbols"])
    expected_entries = _checksummed_artifacts(
        root, package, materials, symbol_reference
    )
    checksums = receipt["checksums"]
    if not isinstance(checksums, dict) or set(checksums) != {
        "file",
        "sha256",
        "format",
        "entries",
    }:
        raise ReleaseError("release checksum evidence has an invalid shape")
    if checksums["format"] != "sha256sum-v1":
        raise ReleaseError("release checksum format is unsupported")
    if checksums["entries"] != list(expected_entries):
        raise ReleaseError("release checksum inventory is incomplete or stale")
    checksum_path = resolved_child(root, checksums["file"], "checksums.file")
    if checksum_path.resolve().parent != root.resolve():
        raise ReleaseError("release checksum file is outside the release directory")
    if sha256_file(checksum_path) != require_sha256(
        checksums["sha256"], "checksums.sha256"
    ):
        raise ReleaseError("release checksum file SHA-256 mismatch")
    if checksum_path.read_bytes() != _checksum_payload(expected_entries):
        raise ReleaseError("release checksum file content is not canonical")
