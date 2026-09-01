#!/usr/bin/env python3
"""Durable, recoverable, manifest-bound trust receipt journal."""

from __future__ import annotations

import hashlib
import json
import re
import secrets
from pathlib import Path
from typing import Mapping, Optional

from certificate_state import (
    CertificateError,
    _commit_json_pending,
    _path_exists,
    _pending_path,
    _read_json_regular,
    _trust_path,
    _unlink_regular,
    _write_json_pending,
)


RECEIPT_SCHEMA_VERSION = 3
TRUST_TYPE_MACOS = "macosUserKeychain"
TRUST_TYPE_SIMULATOR = "iosSimulator"
INSTALLATION_PENDING = "pending"
INSTALLATION_INSTALLED = "installed"
ROOT_STORE_VERIFICATION = "coreSimulatorTrustStoreSha256AndDER"
HEX_32 = re.compile(r"[0-9a-f]{32}")
HEX_40 = re.compile(r"[0-9a-f]{40}")
HEX_64 = re.compile(r"[0-9a-f]{64}")
SIMULATOR_UDID = re.compile(
    r"[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}"
)
BASE_FIELDS = {
    "schemaVersion",
    "receiptId",
    "receiptRevision",
    "installationState",
    "explicitConsent",
    "trustType",
    "manifestSha256",
    "caSha1",
    "caSha256",
    "leafSha256",
    "previousReceiptSha256",
    "migrationSourceSchemaVersion",
    "legacyReceiptSha256",
}
LEGACY_BASE_FIELDS = {
    "schemaVersion",
    "installationState",
    "explicitConsent",
    "trustType",
    "caSha1",
    "caSha256",
    "leafSha256",
}
MACOS_FIELDS = {"keychain"}
SIMULATOR_FIELDS = {
    "simulatorUDID",
    "simulatorName",
    "simulatorDeviceTypeIdentifier",
    "simulatorRuntimeIdentifier",
    "simulatorDataPath",
    "rootStoreVerification",
}


def _canonical_json(value: Mapping[str, object]) -> bytes:
    return json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")


def receipt_sha256(receipt: Mapping[str, object]) -> str:
    return hashlib.sha256(_canonical_json(receipt)).hexdigest()


def manifest_sha256(manifest: Mapping[str, object]) -> str:
    paths = {}
    for key in (
        "caCertificate",
        "caPrivateKey",
        "leafCertificate",
        "leafPrivateKey",
    ):
        paths[key] = Path(str(manifest.get(key, ""))).name
    binding = {
        "schemaVersion": manifest.get("schemaVersion"),
        "paths": paths,
        "hostNames": manifest.get("hostNames"),
        "caSha1": manifest.get("caSha1"),
        "caSha256": manifest.get("caSha256"),
        "leafSha256": manifest.get("leafSha256"),
    }
    return hashlib.sha256(_canonical_json(binding)).hexdigest()


def _require_fingerprint(value: object, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise CertificateError("trust receipt has an invalid %s" % label)
    return value


def _validate_common_binding(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
    *,
    require_leaf: bool,
) -> None:
    if receipt.get("explicitConsent") is not True:
        raise CertificateError("trust receipt does not record literal explicit consent")
    ca_sha1 = _require_fingerprint(receipt.get("caSha1"), HEX_40, "CA SHA-1")
    ca_sha256 = _require_fingerprint(
        receipt.get("caSha256"), HEX_64, "CA SHA-256"
    )
    if ca_sha1 != manifest.get("caSha1") or ca_sha256 != manifest.get("caSha256"):
        raise CertificateError("trust receipt does not match the certificate manifest")
    if require_leaf:
        leaf_sha256 = _require_fingerprint(
            receipt.get("leafSha256"), HEX_64, "leaf SHA-256"
        )
        if leaf_sha256 != manifest.get("leafSha256"):
            raise CertificateError("trust receipt does not match the certificate manifest")


def _validate_macos_binding(receipt: Mapping[str, object]) -> None:
    keychain = receipt.get("keychain")
    if not isinstance(keychain, str) or not keychain:
        raise CertificateError("macOS trust receipt has no exact keychain path")
    path = Path(keychain).expanduser()
    if not path.is_absolute() or str(path.resolve()) != keychain:
        raise CertificateError("macOS trust receipt keychain path is non-canonical")


def _validate_simulator_binding(receipt: Mapping[str, object]) -> None:
    if not isinstance(receipt.get("simulatorUDID"), str) or SIMULATOR_UDID.fullmatch(
        str(receipt.get("simulatorUDID"))
    ) is None:
        raise CertificateError("simulator trust receipt has an invalid exact UDID")
    for key in (
        "simulatorName",
        "simulatorDeviceTypeIdentifier",
        "simulatorRuntimeIdentifier",
    ):
        if not isinstance(receipt.get(key), str) or not receipt.get(key):
            raise CertificateError("simulator trust receipt is missing %s" % key)
    raw_data_path = receipt.get("simulatorDataPath")
    if not isinstance(raw_data_path, str) or not raw_data_path:
        raise CertificateError("simulator trust receipt has no exact data path")
    data_path = Path(raw_data_path).expanduser()
    if not data_path.is_absolute() or str(data_path.resolve()) != raw_data_path:
        raise CertificateError("simulator trust receipt data path is non-canonical")
    if receipt.get("rootStoreVerification") != ROOT_STORE_VERIFICATION:
        raise CertificateError("simulator trust receipt verification contract changed")


def validate_legacy_receipt(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
) -> Mapping[str, object]:
    schema = receipt.get("schemaVersion")
    if schema == 1:
        allowed = {"schemaVersion", "explicitConsent", "keychain", "caSha1", "caSha256"}
        if set(receipt) - allowed:
            raise CertificateError("legacy v1 trust receipt has unknown fields")
        _validate_common_binding(receipt, manifest, require_leaf=False)
        _validate_macos_binding(receipt)
        return dict(receipt)
    if schema != 2:
        raise CertificateError("trust receipt has an unsupported schema")
    trust_type = receipt.get("trustType")
    allowed = LEGACY_BASE_FIELDS | (
        MACOS_FIELDS if trust_type == TRUST_TYPE_MACOS else SIMULATOR_FIELDS
    )
    if set(receipt) - allowed:
        raise CertificateError("legacy v2 trust receipt has unknown fields")
    if receipt.get("installationState") not in (
        INSTALLATION_PENDING,
        INSTALLATION_INSTALLED,
    ):
        raise CertificateError("legacy v2 trust receipt has an invalid state")
    _validate_common_binding(receipt, manifest, require_leaf=True)
    if trust_type == TRUST_TYPE_MACOS:
        _validate_macos_binding(receipt)
    elif trust_type == TRUST_TYPE_SIMULATOR:
        _validate_simulator_binding(receipt)
    else:
        raise CertificateError("legacy v2 trust receipt has an invalid trust type")
    return dict(receipt)


def validate_receipt(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
    *,
    allow_legacy: bool,
) -> Mapping[str, object]:
    if receipt.get("schemaVersion") != RECEIPT_SCHEMA_VERSION:
        if allow_legacy:
            return validate_legacy_receipt(receipt, manifest)
        raise CertificateError("legacy trust receipt requires explicit migration")
    trust_type = receipt.get("trustType")
    allowed = BASE_FIELDS | (
        MACOS_FIELDS if trust_type == TRUST_TYPE_MACOS else SIMULATOR_FIELDS
    )
    if set(receipt) - allowed:
        raise CertificateError("trust receipt has unknown fields")
    receipt_id = receipt.get("receiptId")
    revision = receipt.get("receiptRevision")
    state = receipt.get("installationState")
    if not isinstance(receipt_id, str) or HEX_32.fullmatch(receipt_id) is None:
        raise CertificateError("trust receipt has an invalid receipt ID")
    if not isinstance(revision, int) or isinstance(revision, bool) or revision < 1:
        raise CertificateError("trust receipt has an invalid revision")
    if state not in (INSTALLATION_PENDING, INSTALLATION_INSTALLED):
        raise CertificateError("trust receipt has an invalid installation state")
    if receipt.get("manifestSha256") != manifest_sha256(manifest):
        raise CertificateError("trust receipt manifest binding changed")
    _validate_common_binding(receipt, manifest, require_leaf=True)
    if trust_type == TRUST_TYPE_MACOS:
        _validate_macos_binding(receipt)
    elif trust_type == TRUST_TYPE_SIMULATOR:
        _validate_simulator_binding(receipt)
    else:
        raise CertificateError("trust receipt has an invalid trust type")

    previous = receipt.get("previousReceiptSha256")
    migration_schema = receipt.get("migrationSourceSchemaVersion")
    legacy_sha = receipt.get("legacyReceiptSha256")
    if revision == 1:
        if previous is not None:
            raise CertificateError("initial trust receipt cannot name a predecessor")
        if migration_schema is None:
            if legacy_sha is not None:
                raise CertificateError("trust receipt has an orphaned legacy hash")
        elif (
            migration_schema not in (1, 2)
            or not isinstance(legacy_sha, str)
            or HEX_64.fullmatch(legacy_sha) is None
        ):
            raise CertificateError("trust receipt has an invalid migration binding")
    elif (
        not isinstance(previous, str)
        or HEX_64.fullmatch(previous) is None
        or migration_schema is not None
        or legacy_sha is not None
    ):
        raise CertificateError("trust receipt predecessor binding is invalid")
    return dict(receipt)


def new_pending_receipt(
    manifest: Mapping[str, object],
    *,
    trust_type: str,
    target: Mapping[str, object],
) -> Mapping[str, object]:
    receipt = {
        "schemaVersion": RECEIPT_SCHEMA_VERSION,
        "receiptId": secrets.token_hex(16),
        "receiptRevision": 1,
        "installationState": INSTALLATION_PENDING,
        "explicitConsent": True,
        "trustType": trust_type,
        "manifestSha256": manifest_sha256(manifest),
        "caSha1": manifest["caSha1"],
        "caSha256": manifest["caSha256"],
        "leafSha256": manifest["leafSha256"],
        **dict(target),
    }
    return validate_receipt(receipt, manifest, allow_legacy=False)


def installed_successor(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
) -> Mapping[str, object]:
    current = validate_receipt(receipt, manifest, allow_legacy=False)
    if current.get("installationState") != INSTALLATION_PENDING:
        raise CertificateError("only a pending trust receipt can become installed")
    successor = dict(current)
    successor["receiptRevision"] = int(current["receiptRevision"]) + 1
    successor["installationState"] = INSTALLATION_INSTALLED
    successor["previousReceiptSha256"] = receipt_sha256(current)
    return validate_receipt(successor, manifest, allow_legacy=False)


def _legal_successor(
    current: Mapping[str, object],
    candidate: Mapping[str, object],
) -> bool:
    current_schema = current.get("schemaVersion")
    candidate_schema = candidate.get("schemaVersion")
    if current_schema in (1, 2) and candidate_schema == RECEIPT_SCHEMA_VERSION:
        return (
            candidate.get("receiptRevision") == 1
            and candidate.get("migrationSourceSchemaVersion") == current_schema
            and candidate.get("legacyReceiptSha256") == receipt_sha256(current)
        )
    if current_schema == candidate_schema == 2:
        before = dict(current)
        after = dict(candidate)
        before_state = before.pop("installationState", None)
        after_state = after.pop("installationState", None)
        return (
            before_state == INSTALLATION_PENDING
            and after_state == INSTALLATION_INSTALLED
            and before == after
        )
    if current_schema != RECEIPT_SCHEMA_VERSION or candidate_schema != RECEIPT_SCHEMA_VERSION:
        return False
    immutable = dict(current)
    for key in ("receiptRevision", "installationState", "previousReceiptSha256"):
        immutable.pop(key, None)
    candidate_immutable = dict(candidate)
    for key in ("receiptRevision", "installationState", "previousReceiptSha256"):
        candidate_immutable.pop(key, None)
    return (
        candidate.get("receiptId") == current.get("receiptId")
        and candidate.get("receiptRevision") == int(current["receiptRevision"]) + 1
        and current.get("installationState") == INSTALLATION_PENDING
        and candidate.get("installationState") == INSTALLATION_INSTALLED
        and candidate.get("previousReceiptSha256") == receipt_sha256(current)
        and immutable == candidate_immutable
    )


def read_receipt(
    directory: Path,
    manifest: Mapping[str, object],
    *,
    allow_legacy: bool = True,
) -> Optional[Mapping[str, object]]:
    path = _trust_path(directory)
    pending = _pending_path(path)
    final_exists = _path_exists(path)
    pending_exists = _path_exists(pending)
    if not final_exists and not pending_exists:
        return None

    final_value: Optional[Mapping[str, object]] = None
    if final_exists:
        final_value = validate_receipt(
            _read_json_regular(path, label="trust receipt"),
            manifest,
            allow_legacy=allow_legacy,
        )
    if pending_exists:
        try:
            pending_raw = _read_json_regular(pending, label="pending trust receipt")
        except CertificateError:
            _unlink_regular(pending, label="truncated pending trust receipt")
            return final_value
        pending_value = validate_receipt(
            pending_raw,
            manifest,
            allow_legacy=allow_legacy,
        )
        if final_value is None:
            _commit_json_pending(path)
            return pending_value
        if receipt_sha256(final_value) == receipt_sha256(pending_value):
            _unlink_regular(pending, label="duplicate pending trust receipt")
            return final_value
        if not _legal_successor(final_value, pending_value):
            raise CertificateError("trust receipt journal contains an invalid transition")
        _commit_json_pending(path)
        return pending_value
    return final_value


def write_initial_pending(
    directory: Path,
    manifest: Mapping[str, object],
    receipt: Mapping[str, object],
) -> None:
    validated = validate_receipt(receipt, manifest, allow_legacy=False)
    if validated.get("installationState") != INSTALLATION_PENDING:
        raise CertificateError("initial trust receipt must be pending")
    path = _trust_path(directory)
    if _path_exists(path) or _path_exists(_pending_path(path)):
        raise CertificateError("a trust receipt already exists")
    _write_json_pending(path, validated)
    _commit_json_pending(path)


def write_successor(
    directory: Path,
    manifest: Mapping[str, object],
    current: Mapping[str, object],
    successor: Mapping[str, object],
) -> None:
    recovered = read_receipt(directory, manifest, allow_legacy=False)
    validated = validate_receipt(successor, manifest, allow_legacy=False)
    if recovered is not None and receipt_sha256(recovered) == receipt_sha256(validated):
        return
    if recovered is None or receipt_sha256(recovered) != receipt_sha256(current):
        raise CertificateError("trust receipt changed before its state transition")
    if not _legal_successor(recovered, validated):
        raise CertificateError("refusing an invalid trust receipt state transition")
    _write_json_pending(_trust_path(directory), validated)
    _commit_json_pending(_trust_path(directory))


def migrated_receipt(
    legacy: Mapping[str, object],
    manifest: Mapping[str, object],
) -> Mapping[str, object]:
    source = validate_legacy_receipt(legacy, manifest)
    source_schema = int(source["schemaVersion"])
    trust_type = source.get("trustType", TRUST_TYPE_MACOS)
    state = source.get("installationState", INSTALLATION_INSTALLED)
    target_fields = (
        {"keychain": source["keychain"]}
        if trust_type == TRUST_TYPE_MACOS
        else {key: source[key] for key in SIMULATOR_FIELDS}
    )
    receipt = {
        "schemaVersion": RECEIPT_SCHEMA_VERSION,
        "receiptId": secrets.token_hex(16),
        "receiptRevision": 1,
        "installationState": state,
        "explicitConsent": True,
        "trustType": trust_type,
        "manifestSha256": manifest_sha256(manifest),
        "caSha1": manifest["caSha1"],
        "caSha256": manifest["caSha256"],
        "leafSha256": manifest["leafSha256"],
        "migrationSourceSchemaVersion": source_schema,
        "legacyReceiptSha256": receipt_sha256(source),
        **target_fields,
    }
    return validate_receipt(receipt, manifest, allow_legacy=False)


def write_migration(
    directory: Path,
    manifest: Mapping[str, object],
    legacy: Mapping[str, object],
    migrated: Mapping[str, object],
) -> None:
    current = read_receipt(directory, manifest, allow_legacy=True)
    validated = validate_receipt(migrated, manifest, allow_legacy=False)
    if current is not None and receipt_sha256(current) == receipt_sha256(validated):
        return
    if current is None or receipt_sha256(current) != receipt_sha256(legacy):
        raise CertificateError("legacy trust receipt changed before migration")
    if not _legal_successor(current, validated):
        raise CertificateError("legacy trust receipt migration binding is invalid")
    _write_json_pending(_trust_path(directory), validated)
    _commit_json_pending(_trust_path(directory))


def remove_receipt(
    directory: Path,
    manifest: Mapping[str, object],
) -> None:
    receipt = read_receipt(directory, manifest, allow_legacy=True)
    if receipt is None:
        return
    _unlink_regular(_trust_path(directory), label="trust receipt")
    if _path_exists(_pending_path(_trust_path(directory))):
        raise CertificateError("pending trust receipt remained after removal")
