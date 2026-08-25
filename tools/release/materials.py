#!/usr/bin/env python3
"""Deterministic SPDX SBOM and corresponding-source/license evidence."""

from __future__ import annotations

import os
import pathlib
import re
import tempfile
import zipfile

from .common import (
    ReleaseError,
    atomic_write_json,
    load_json,
    require_utc_timestamp,
    resolved_child,
    sha256_file,
)


def _component_id(index: int, name: str) -> str:
    normalized = re.sub(r"[^A-Za-z0-9.-]", "-", name).strip("-")
    if not normalized:
        raise ReleaseError("SBOM component name cannot produce an SPDX identifier")
    return f"SPDXRef-Package-{index:04d}-{normalized}"


def create_materials(
    inventory_path: pathlib.Path,
    *,
    source_root: pathlib.Path,
    source_offer: pathlib.Path,
    notices: pathlib.Path,
    sbom_output: pathlib.Path,
    license_archive_output: pathlib.Path,
    receipt_output: pathlib.Path,
    document_namespace: str,
    created_at: str,
) -> tuple[dict, dict]:
    inventory = load_json(inventory_path)
    if not isinstance(inventory, dict) or set(inventory) != {
        "schemaVersion",
        "components",
    }:
        raise ReleaseError("release component inventory has an invalid shape")
    if inventory["schemaVersion"] != 1:
        raise ReleaseError("release component inventory schema is unsupported")
    components = inventory["components"]
    if not isinstance(components, list) or not components:
        raise ReleaseError("release component inventory must not be empty")
    if not document_namespace.startswith("https://"):
        raise ReleaseError("SPDX document namespace must use HTTPS")
    require_utc_timestamp(created_at, "SPDX createdAt")
    if not source_offer.is_file() or not notices.is_file():
        raise ReleaseError("corresponding source offer and Third-Party Notices are required")
    if source_offer.stat().st_size == 0 or notices.stat().st_size == 0:
        raise ReleaseError("source offer and Third-Party Notices must not be empty")

    packages = []
    license_evidence = []
    seen = set()
    for index, raw in enumerate(components, start=1):
        expected = {
            "name",
            "version",
            "supplier",
            "downloadLocation",
            "licenseConcluded",
            "licenseFiles",
        }
        if not isinstance(raw, dict) or set(raw) != expected:
            raise ReleaseError(f"component {index} has an invalid shape")
        for field in expected - {"licenseFiles"}:
            if not isinstance(raw[field], str) or not raw[field]:
                raise ReleaseError(f"component {index}.{field} must be non-empty")
        identity = (raw["name"], raw["version"])
        if identity in seen:
            raise ReleaseError(f"duplicate SBOM component: {raw['name']} {raw['version']}")
        seen.add(identity)
        if raw["licenseConcluded"] in {"NOASSERTION", "NONE"}:
            raise ReleaseError(f"unresolved license for bundled component: {raw['name']}")
        if raw["supplier"] in {"NOASSERTION", "NONE"}:
            raise ReleaseError(f"unresolved supplier for bundled component: {raw['name']}")
        if raw["downloadLocation"] in {"NOASSERTION", "NONE"}:
            raise ReleaseError(f"unresolved source location for component: {raw['name']}")
        license_files = raw["licenseFiles"]
        if not isinstance(license_files, list) or not license_files:
            raise ReleaseError(f"component {raw['name']} has no license evidence")
        evidence = []
        for evidence_index, reference in enumerate(license_files):
            path = resolved_child(
                source_root,
                reference,
                f"components[{index}].licenseFiles[{evidence_index}]",
            )
            if path.stat().st_size == 0:
                raise ReleaseError(f"empty license evidence for component: {raw['name']}")
            evidence.append(
                {
                    "file": path.relative_to(source_root.resolve()).as_posix(),
                    "sha256": sha256_file(path),
                }
            )
        package_id = _component_id(index, raw["name"])
        packages.append(
            {
                "SPDXID": package_id,
                "name": raw["name"],
                "versionInfo": raw["version"],
                "supplier": raw["supplier"],
                "downloadLocation": raw["downloadLocation"],
                "filesAnalyzed": False,
                "licenseConcluded": raw["licenseConcluded"],
                "licenseDeclared": raw["licenseConcluded"],
                "copyrightText": "NOASSERTION",
            }
        )
        license_evidence.append(
            {
                "SPDXID": package_id,
                "name": raw["name"],
                "license": raw["licenseConcluded"],
                "files": evidence,
            }
        )

    document_id = "SPDXRef-DOCUMENT"
    sbom = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": document_id,
        "name": "AhoiBrowser release SBOM",
        "documentNamespace": document_namespace,
        "creationInfo": {
            "created": created_at,
            "creators": ["Tool: AhoiBrowser release-materials-v1"],
        },
        "packages": packages,
        "relationships": [
            {
                "spdxElementId": document_id,
                "relationshipType": "DESCRIBES",
                "relatedSpdxElement": package["SPDXID"],
            }
            for package in packages
        ],
    }
    atomic_write_json(sbom_output, sbom)
    archive_files = {
        item["file"]: resolved_child(source_root, item["file"], "license evidence")
        for component in license_evidence
        for item in component["files"]
    }
    license_archive_output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{license_archive_output.stem}.",
        suffix=license_archive_output.suffix,
        dir=str(license_archive_output.parent),
    )
    os.close(descriptor)
    temporary_archive = pathlib.Path(temporary_name)
    try:
        with zipfile.ZipFile(
            temporary_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        ) as archive:
            for name, path in sorted(archive_files.items()):
                metadata = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                metadata.compress_type = zipfile.ZIP_DEFLATED
                metadata.external_attr = 0o100644 << 16
                archive.writestr(metadata, path.read_bytes())
        os.replace(temporary_archive, license_archive_output)
    finally:
        if temporary_archive.exists():
            temporary_archive.unlink()
    receipt = {
        "schemaVersion": 1,
        "kind": "release-materials",
        "componentInventory": {
            "file": inventory_path.name,
            "sha256": sha256_file(inventory_path),
            "componentCount": len(components),
        },
        "sbom": {
            "file": sbom_output.name,
            "sha256": sha256_file(sbom_output),
            "format": "SPDX-2.3-json",
        },
        "licenseArchive": {
            "file": license_archive_output.name,
            "sha256": sha256_file(license_archive_output),
            "format": "zip",
        },
        "thirdPartyNotices": {
            "file": notices.name,
            "sha256": sha256_file(notices),
        },
        "correspondingSourceOffer": {
            "file": source_offer.name,
            "sha256": sha256_file(source_offer),
        },
        "licenses": license_evidence,
    }
    atomic_write_json(receipt_output, receipt)
    return sbom, receipt
