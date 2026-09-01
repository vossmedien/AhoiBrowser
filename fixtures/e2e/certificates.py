#!/usr/bin/env python3
"""Explicit CA/leaf certificate and macOS/simulator trust lifecycle."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sqlite3
import ssl
import subprocess
import tempfile
from pathlib import Path
from typing import Callable, Mapping, Optional, Sequence
from urllib.parse import quote

from certificate_state import (
    CERTIFICATE_MANIFEST,  # noqa: F401 - compatibility re-export
    CERTIFICATE_MATERIAL_FILES,
    TRUST_RECEIPT,  # noqa: F401 - compatibility re-export
    CertificateError,
    _assert_no_duplicate_material,
    _canonical_material_paths,
    _fsync_directory,
    _manifest_material_paths,
    _manifest_path,
    _path_exists,
    _pending_path,
    _regular_file_stat,
    _trust_artifact_paths,
    read_manifest,
    remove_certificate_material,  # noqa: F401 - compatibility re-export
    validated_manifest,
    write_manifest,
)
from trust_receipts import (
    INSTALLATION_INSTALLED,
    INSTALLATION_PENDING,
    RECEIPT_SCHEMA_VERSION,
    ROOT_STORE_VERIFICATION,
    TRUST_TYPE_MACOS,
    TRUST_TYPE_SIMULATOR,
    installed_successor,
    migrated_receipt,
    new_pending_receipt,
    read_receipt,
    remove_receipt,
    validate_receipt,
    write_initial_pending,
    write_migration,
    write_successor,
)

TRUST_CONFIRMATION = "I-understand-this-adds-a-local-test-CA"
REMOVE_CONFIRMATION = "remove-the-local-test-CA"
SIMULATOR_TRUST_CONFIRMATION = (
    "I-understand-this-adds-a-local-test-CA-to-the-selected-iOS-Simulator"
)
SIMULATOR_TRUST_FINALIZE_CONFIRMATION = (
    "I-confirm-the-recorded-iOS-Simulator-was-deleted"
)
SIMULATOR_TRUST_CANCEL_CONFIRMATION = (
    "I-confirm-the-pending-iOS-Simulator-has-no-local-test-CA"
)
TRUST_RECEIPT_MIGRATION_CONFIRMATION = "migrate-the-legacy-local-test-CA-receipt"
SIMULATOR_TRUST_STORE = Path(
    "private/var/protected/trustd/private/TrustStore.sqlite3"
)
SIMULATOR_UDID_PATTERN = re.compile(
    r"^[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}$"
)
HOST_NAMES = (
    "localhost",
    "first-party.localhost",
    "third-party.localhost",
    "media.localhost",
)


def _run(
    command: Sequence[str],
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> str:
    try:
        result = runner(
            list(command),
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as error:
        raise CertificateError("required command is unavailable: %s" % command[0]) from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "unknown error").strip()
        raise CertificateError("command failed: %s: %s" % (command[0], detail)) from error
    return result.stdout.strip()


def _fingerprint(path: Path, algorithm: str, *, runner=subprocess.run) -> str:
    output = _run(
        ["openssl", "x509", "-in", str(path), "-noout", "-fingerprint", "-%s" % algorithm],
        runner=runner,
    )
    try:
        return output.split("=", 1)[1].replace(":", "").lower()
    except IndexError as error:
        raise CertificateError("openssl returned an invalid certificate fingerprint") from error


def _certificate_sha256(path: Path) -> str:
    try:
        der = ssl.PEM_cert_to_DER_cert(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CertificateError("certificate could not be decoded for fingerprinting") from error
    return hashlib.sha256(der).hexdigest()


def generate(directory: Path, *, rotate: bool = False, runner=subprocess.run) -> Mapping[str, object]:
    """Create a unique P-256 CA and CA-signed localhost leaf certificate."""

    directory.mkdir(parents=True, exist_ok=True)
    directory = directory.expanduser().resolve()
    manifest_path = _manifest_path(directory)
    pending_manifest_path = _pending_path(manifest_path)
    if _path_exists(pending_manifest_path):
        raise CertificateError(
            "a pending certificate manifest exists; inspect or remove it before generation"
        )
    if rotate and _trust_artifact_paths(directory):
        raise CertificateError("remove the trusted CA before rotating certificates")
    existing = read_manifest(directory)
    if _path_exists(manifest_path):
        if existing is None:
            raise CertificateError(
                "certificate manifest is unreadable or malformed; no files were touched"
            )
        normalized = validated_manifest(directory)
        if not rotate:
            return normalized
        _assert_no_duplicate_material(
            directory,
            tuple(_manifest_material_paths(
                directory,
                normalized,
                require_files=True,
            ).values()),
        )
    else:
        untracked = tuple(
            path
            for path in _canonical_material_paths(directory).values()
            if _path_exists(path)
        )
        if untracked:
            raise CertificateError(
                "untracked canonical certificate material exists without a manifest; "
                "run cleanup or reconcile the state directory before generation"
            )

    openssl = shutil.which("openssl")
    if openssl is None:
        raise CertificateError("openssl is required to generate fixture certificates")
    with tempfile.TemporaryDirectory(
        prefix=".certificate-generation-",
        dir=str(directory),
    ) as staging_name:
        staging = Path(staging_name)
        ca_key = staging / CERTIFICATE_MATERIAL_FILES["caPrivateKey"]
        ca_certificate = staging / CERTIFICATE_MATERIAL_FILES["caCertificate"]
        leaf_key = staging / CERTIFICATE_MATERIAL_FILES["leafPrivateKey"]
        leaf_certificate = staging / CERTIFICATE_MATERIAL_FILES["leafCertificate"]
        leaf_request = staging / "leaf.csr"
        leaf_extensions = staging / "leaf.ext"
        leaf_extensions.write_text(
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            "extendedKeyUsage=serverAuth\n"
            "subjectAltName=DNS:localhost,DNS:first-party.localhost,"
            "DNS:third-party.localhost,DNS:media.localhost,IP:127.0.0.1,IP:::1\n"
            "subjectKeyIdentifier=hash\n"
            "authorityKeyIdentifier=keyid,issuer\n",
            encoding="utf-8",
        )
        commands = (
            [openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(ca_key)],
            [openssl, "req", "-new", "-x509", "-sha256", "-days", "7", "-key", str(ca_key), "-out", str(ca_certificate), "-subj", "/CN=AhoiBrowser Local E2E Test CA/O=AhoiBrowser Local Test Only", "-addext", "basicConstraints=critical,CA:TRUE,pathlen:0", "-addext", "keyUsage=critical,keyCertSign,cRLSign"],
            [openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(leaf_key)],
            [openssl, "req", "-new", "-sha256", "-key", str(leaf_key), "-out", str(leaf_request), "-subj", "/CN=first-party.localhost/O=AhoiBrowser Local Test Only"],
            [openssl, "x509", "-req", "-sha256", "-days", "3", "-in", str(leaf_request), "-CA", str(ca_certificate), "-CAkey", str(ca_key), "-CAcreateserial", "-out", str(leaf_certificate), "-extfile", str(leaf_extensions)],
            [openssl, "verify", "-CAfile", str(ca_certificate), str(leaf_certificate)],
        )
        for command in commands:
            _run(command, runner=runner)
        os.chmod(ca_key, 0o600)
        os.chmod(leaf_key, 0o600)
        manifest = {
            "schemaVersion": 1,
            **CERTIFICATE_MATERIAL_FILES,
            "hostNames": list(HOST_NAMES),
            "caSha256": _fingerprint(ca_certificate, "sha256", runner=runner),
            "caSha1": _fingerprint(ca_certificate, "sha1", runner=runner),
            "leafSha256": _fingerprint(leaf_certificate, "sha256", runner=runner),
        }
        staged_material = {
            "caCertificate": ca_certificate,
            "caPrivateKey": ca_key,
            "leafCertificate": leaf_certificate,
            "leafPrivateKey": leaf_key,
        }
        canonical = _canonical_material_paths(directory)
        for key, source in staged_material.items():
            target = canonical[key]
            if _path_exists(target):
                _regular_file_stat(target, label="existing certificate/key material")
            os.replace(source, target)
        _fsync_directory(directory)
    write_manifest(directory, manifest)
    return validated_manifest(directory)


def default_user_keychain(*, runner=subprocess.run) -> Path:
    output = _run(["security", "default-keychain", "-d", "user"], runner=runner)
    path = output.strip().strip('"')
    if not path:
        raise CertificateError("macOS did not report a default user keychain")
    return Path(path).expanduser().resolve()


def read_trust_receipt(directory: Path) -> Optional[Mapping[str, object]]:
    """Strictly reconcile and read a manifest-bound trust receipt."""

    state_directory = directory.expanduser().resolve()
    if not _trust_artifact_paths(state_directory):
        return None
    manifest = validated_manifest(state_directory, require_files=False)
    return read_receipt(state_directory, manifest, allow_legacy=True)


def _macos_ca_is_present(
    receipt: Mapping[str, object],
    *,
    runner=subprocess.run,
) -> bool:
    fingerprint = str(receipt.get("caSha256", ""))
    keychain = str(receipt.get("keychain", ""))
    inventory = _run(
        ["security", "find-certificate", "-a", "-Z", keychain],
        runner=runner,
    )
    fingerprints = re.findall(
        r"(?im)^\s*SHA-256 hash:\s*([0-9a-f]{64})\s*$",
        inventory,
    )
    matches = [value for value in fingerprints if value.lower() == fingerprint]
    if len(matches) > 1:
        raise CertificateError("the keychain contains duplicate exact fixture CA entries")
    return len(matches) == 1


def _validated_manifest_certificates(
    directory: Path,
    *,
    runner=subprocess.run,
) -> tuple[Mapping[str, object], Path]:
    manifest = validated_manifest(directory)
    ca_certificate = Path(str(manifest["caCertificate"]))
    leaf_certificate = Path(str(manifest["leafCertificate"]))
    expected_ca = str(manifest.get("caSha256", ""))
    expected_leaf = str(manifest.get("leafSha256", ""))
    if _fingerprint(ca_certificate, "sha256", runner=runner) != expected_ca:
        raise CertificateError("CA fingerprint changed after certificate generation")
    if _certificate_sha256(leaf_certificate) != expected_leaf:
        raise CertificateError("leaf fingerprint changed after certificate generation")
    return manifest, ca_certificate


def _simulator_devices(*, runner=subprocess.run) -> list[Mapping[str, object]]:
    output = _run(
        ["xcrun", "simctl", "list", "devices", "--json"],
        runner=runner,
    )
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise CertificateError("simctl returned invalid device inventory JSON") from error
    runtimes = payload.get("devices") if isinstance(payload, dict) else None
    if not isinstance(runtimes, dict):
        raise CertificateError("simctl device inventory is missing the devices map")
    devices: list[Mapping[str, object]] = []
    for runtime_identifier, entries in runtimes.items():
        if not isinstance(runtime_identifier, str) or not isinstance(entries, list):
            raise CertificateError("simctl device inventory has an invalid runtime entry")
        for entry in entries:
            if not isinstance(entry, dict):
                raise CertificateError("simctl device inventory has an invalid device entry")
            value = dict(entry)
            value["runtimeIdentifier"] = runtime_identifier
            devices.append(value)
    return devices


def _validate_simulator_udid(udid: str) -> str:
    if not SIMULATOR_UDID_PATTERN.fullmatch(udid):
        raise CertificateError(
            "simulator UDID must be an exact uppercase UUID, not a name or 'booted' alias"
        )
    return udid


def _booted_simulator_identity(
    udid: str,
    *,
    runner=subprocess.run,
) -> Mapping[str, str]:
    exact_udid = _validate_simulator_udid(udid)
    matches = [
        entry for entry in _simulator_devices(runner=runner)
        if entry.get("udid") == exact_udid
    ]
    if len(matches) != 1:
        raise CertificateError("the exact simulator UDID was not found exactly once")
    entry = matches[0]
    if entry.get("isAvailable") is not True or entry.get("state") != "Booted":
        raise CertificateError("the exact simulator must be available and Booted")
    required = {
        "simulatorUDID": exact_udid,
        "simulatorName": entry.get("name"),
        "simulatorDeviceTypeIdentifier": entry.get("deviceTypeIdentifier"),
        "simulatorRuntimeIdentifier": entry.get("runtimeIdentifier"),
        "simulatorDataPath": entry.get("dataPath"),
    }
    if not all(isinstance(value, str) and value for value in required.values()):
        raise CertificateError("simctl omitted required simulator identity fields")
    identity = {key: str(value) for key, value in required.items()}
    identity["simulatorDataPath"] = str(
        Path(identity["simulatorDataPath"]).expanduser().resolve()
    )
    return identity


def _recorded_simulator_identity(
    receipt: Mapping[str, object],
    *,
    runner=subprocess.run,
) -> Mapping[str, str]:
    udid = _validate_simulator_udid(str(receipt.get("simulatorUDID", "")))
    matches = [entry for entry in _simulator_devices(runner=runner) if entry.get("udid") == udid]
    if len(matches) != 1:
        raise CertificateError("the exact recorded simulator was not found exactly once")
    entry = matches[0]
    identity = {
        "simulatorUDID": udid,
        "simulatorName": entry.get("name"),
        "simulatorDeviceTypeIdentifier": entry.get("deviceTypeIdentifier"),
        "simulatorRuntimeIdentifier": entry.get("runtimeIdentifier"),
        "simulatorDataPath": entry.get("dataPath"),
    }
    if not all(isinstance(value, str) and value for value in identity.values()):
        raise CertificateError("simctl omitted required recorded simulator identity fields")
    normalized = {key: str(value) for key, value in identity.items()}
    normalized["simulatorDataPath"] = str(
        Path(normalized["simulatorDataPath"]).expanduser().resolve()
    )
    if any(receipt.get(key) != value for key, value in normalized.items()):
        raise CertificateError("the exact recorded simulator identity changed")
    return normalized


def _simulator_root_store_contains_ca(data_path: Path, ca_sha256: str) -> bool:
    """Read the exact CoreSimulator trust row without mutating the simulator."""

    if len(ca_sha256) != 64 or any(
        character not in "0123456789abcdef" for character in ca_sha256
    ):
        raise CertificateError("simulator receipt has an invalid CA fingerprint")
    resolved_data = data_path.expanduser().resolve()
    trust_store = (resolved_data / SIMULATOR_TRUST_STORE).resolve()
    try:
        trust_store.relative_to(resolved_data)
    except ValueError as error:
        raise CertificateError("simulator trust-store path escaped the device data path") from error
    if not trust_store.is_file():
        raise CertificateError("the exact simulator trust store is unavailable")
    try:
        connection = sqlite3.connect(
            "file:%s?mode=ro" % quote(trust_store.as_posix(), safe="/"),
            uri=True,
            timeout=1,
        )
        try:
            rows = connection.execute(
                "SELECT data FROM tsettings WHERE lower(hex(sha256)) = ?",
                (ca_sha256,),
            ).fetchall()
        finally:
            connection.close()
    except sqlite3.Error as error:
        raise CertificateError("the simulator trust store could not be read safely") from error
    if len(rows) > 1:
        raise CertificateError("the simulator trust store contains duplicate exact CA rows")
    if not rows:
        return False
    if not isinstance(rows[0][0], bytes):
        raise CertificateError("the simulator trust row has invalid certificate data")
    return hashlib.sha256(rows[0][0]).hexdigest() == ca_sha256


def install_trust(
    directory: Path,
    *,
    confirmation: str,
    keychain: Optional[Path] = None,
    runner=subprocess.run,
) -> Mapping[str, object]:
    """Install only the generated CA after the caller supplies exact consent."""

    if confirmation != TRUST_CONFIRMATION:
        raise CertificateError("refusing trust install without the exact confirmation phrase")
    directory = directory.expanduser().resolve()
    if _trust_artifact_paths(directory):
        raise CertificateError("this fixture CA already has an installation receipt")
    manifest, ca_certificate = _validated_manifest_certificates(
        directory,
        runner=runner,
    )
    selected_keychain = (keychain or default_user_keychain(runner=runner)).resolve()
    pending = new_pending_receipt(
        manifest,
        trust_type=TRUST_TYPE_MACOS,
        target={"keychain": str(selected_keychain)},
    )
    write_initial_pending(directory, manifest, pending)
    _run(
        [
            "security",
            "add-trusted-cert",
            "-r",
            "trustRoot",
            "-k",
            str(selected_keychain),
            str(ca_certificate),
        ],
        runner=runner,
    )
    if not _macos_trust_installation_is_valid(pending, manifest, runner=runner):
        raise CertificateError(
            "security returned success but exact CA/leaf trust verification failed; "
            "the pending cleanup receipt was retained"
        )
    installed = installed_successor(pending, manifest)
    write_successor(directory, manifest, pending, installed)
    return installed


def install_simulator_trust(
    directory: Path,
    *,
    confirmation: str,
    udid: str,
    runner=subprocess.run,
) -> Mapping[str, object]:
    """Add only the generated CA to one exact booted iOS Simulator."""

    if confirmation != SIMULATOR_TRUST_CONFIRMATION:
        raise CertificateError(
            "refusing simulator trust install without the exact confirmation phrase"
        )
    directory = directory.expanduser().resolve()
    if _trust_artifact_paths(directory):
        raise CertificateError("this fixture CA already has an installation receipt")
    manifest, ca_certificate = _validated_manifest_certificates(
        directory,
        runner=runner,
    )
    identity = _booted_simulator_identity(udid, runner=runner)
    data_path = Path(identity["simulatorDataPath"])
    # Fail before mutation if this Xcode runtime does not expose the expected
    # read-only CoreSimulator trust database contract.
    if _simulator_root_store_contains_ca(data_path, str(manifest["caSha256"])):
        raise CertificateError("the exact simulator already trusts this fixture CA")
    pending = new_pending_receipt(
        manifest,
        trust_type=TRUST_TYPE_SIMULATOR,
        target={**identity, "rootStoreVerification": ROOT_STORE_VERIFICATION},
    )
    # Durably persist the exact cleanup obligation before the only trust mutation.
    write_initial_pending(directory, manifest, pending)
    _run(
        [
            "xcrun",
            "simctl",
            "keychain",
            udid,
            "add-root-cert",
            str(ca_certificate),
        ],
        runner=runner,
    )
    if not _simulator_root_store_contains_ca(
        data_path,
        str(manifest["caSha256"]),
    ):
        raise CertificateError(
            "simctl returned success but the exact CA is absent from the simulator root store"
        )
    installed = installed_successor(pending, manifest)
    write_successor(directory, manifest, pending, installed)
    return installed


def remove_trust(
    directory: Path,
    *,
    confirmation: str,
    runner=subprocess.run,
) -> bool:
    """Remove exactly the CA identified by the local installation receipt."""

    if confirmation != REMOVE_CONFIRMATION:
        raise CertificateError("refusing trust removal without the exact confirmation phrase")
    directory = directory.expanduser().resolve()
    if not _trust_artifact_paths(directory):
        return False
    manifest = validated_manifest(directory, require_files=False)
    receipt = read_receipt(directory, manifest, allow_legacy=True)
    if receipt is None:
        return False
    trust_type = receipt.get("trustType")
    if trust_type == TRUST_TYPE_SIMULATOR:
        raise CertificateError(
            "simctl has no single-root removal; delete the exact disposable simulator "
            "and run simulator-trust-finalize"
        )
    if trust_type not in (None, TRUST_TYPE_MACOS):
        raise CertificateError("trust receipt has an unsupported trust type")
    if _macos_ca_is_present(receipt, runner=runner):
        _run(
            [
                "security",
                "delete-certificate",
                "-t",
                "-Z",
                str(receipt["caSha256"]),
                str(receipt["keychain"]),
            ],
            runner=runner,
        )
        if _macos_ca_is_present(receipt, runner=runner):
            raise CertificateError(
                "the exact fixture CA remains in the recorded keychain; receipt retained"
            )
    remove_receipt(directory, manifest)
    return True


def _receipt_matches_manifest(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
    *,
    runner=subprocess.run,
) -> bool:
    try:
        validated = validate_receipt(receipt, manifest, allow_legacy=False)
        if validated.get("installationState") != INSTALLATION_INSTALLED:
            return False
        ca = Path(str(manifest.get("caCertificate", ""))).resolve()
        leaf = Path(str(manifest.get("leafCertificate", ""))).resolve()
        return (
            _fingerprint(ca, "sha256", runner=runner) == manifest.get("caSha256")
            and _certificate_sha256(leaf) == manifest.get("leafSha256")
        )
    except CertificateError:
        return False


def _macos_trust_installation_is_valid(
    receipt: Mapping[str, object],
    manifest: Mapping[str, object],
    *,
    runner=subprocess.run,
) -> bool:
    try:
        if not _macos_ca_is_present(receipt, runner=runner):
            return False
        _run(
            [
                "security",
                "verify-cert",
                "-c",
                str(manifest["leafCertificate"]),
                "-p",
                "ssl",
                "-n",
                "first-party.localhost",
                "-k",
                str(receipt["keychain"]),
                "-L",
                "-q",
            ],
            runner=runner,
        )
    except CertificateError:
        return False
    return True


def _simulator_trust_installation_is_valid(
    receipt: Mapping[str, object],
    *,
    runner=subprocess.run,
) -> bool:
    try:
        udid = _validate_simulator_udid(str(receipt.get("simulatorUDID", "")))
        identity = _booted_simulator_identity(udid, runner=runner)
        for key, value in identity.items():
            if receipt.get(key) != value:
                return False
        if receipt.get("rootStoreVerification") != ROOT_STORE_VERIFICATION:
            return False
        return _simulator_root_store_contains_ca(
            Path(identity["simulatorDataPath"]),
            str(receipt.get("caSha256", "")),
        )
    except CertificateError:
        return False


def trust_installation_is_valid(directory: Path, *, runner=subprocess.run) -> bool:
    """Verify one exact typed trust target and the generated certificates."""

    try:
        manifest = validated_manifest(directory)
        receipt = read_receipt(directory, manifest, allow_legacy=True)
    except CertificateError:
        return False
    if receipt is None or receipt.get("schemaVersion") != RECEIPT_SCHEMA_VERSION:
        return False
    if not _receipt_matches_manifest(receipt, manifest, runner=runner):
        return False
    trust_type = receipt.get("trustType")
    if trust_type == TRUST_TYPE_MACOS:
        return _macos_trust_installation_is_valid(
            receipt,
            manifest,
            runner=runner,
        )
    if trust_type == TRUST_TYPE_SIMULATOR:
        return _simulator_trust_installation_is_valid(receipt, runner=runner)
    return False


def finalize_deleted_simulator_trust(
    directory: Path,
    *,
    confirmation: str,
    runner=subprocess.run,
) -> bool:
    """Forget a simulator receipt only after its exact device no longer exists."""

    if confirmation != SIMULATOR_TRUST_FINALIZE_CONFIRMATION:
        raise CertificateError(
            "refusing simulator trust finalization without the exact confirmation phrase"
        )
    directory = directory.expanduser().resolve()
    if not _trust_artifact_paths(directory):
        return False
    manifest = validated_manifest(directory, require_files=False)
    receipt = read_receipt(directory, manifest, allow_legacy=True)
    if receipt is None:
        return False
    if receipt.get("trustType") != TRUST_TYPE_SIMULATOR:
        raise CertificateError("the trust receipt does not target an iOS Simulator")
    udid = _validate_simulator_udid(str(receipt.get("simulatorUDID", "")))
    if any(entry.get("udid") == udid for entry in _simulator_devices(runner=runner)):
        raise CertificateError(
            "the exact recorded simulator still exists; no receipt was removed"
        )
    data_path = Path(str(receipt["simulatorDataPath"]))
    if _path_exists(data_path):
        raise CertificateError(
            "the recorded simulator data path still exists; no receipt was removed"
        )
    remove_receipt(directory, manifest)
    return True


def cancel_pending_simulator_trust(
    directory: Path,
    *,
    confirmation: str,
    runner=subprocess.run,
) -> bool:
    """Clear a pending simulator receipt only after exact CA absence is proven."""

    if confirmation != SIMULATOR_TRUST_CANCEL_CONFIRMATION:
        raise CertificateError(
            "refusing pending simulator trust cancellation without the exact confirmation phrase"
        )
    directory = directory.expanduser().resolve()
    if not _trust_artifact_paths(directory):
        return False
    manifest = validated_manifest(directory, require_files=False)
    receipt = read_receipt(directory, manifest, allow_legacy=True)
    if receipt is None:
        return False
    if receipt.get("trustType") != TRUST_TYPE_SIMULATOR:
        raise CertificateError("the trust receipt does not target an iOS Simulator")
    if receipt.get("installationState") != INSTALLATION_PENDING:
        raise CertificateError("only a pending simulator trust receipt can be cancelled")
    identity = _recorded_simulator_identity(receipt, runner=runner)
    if _simulator_root_store_contains_ca(
        Path(identity["simulatorDataPath"]),
        str(receipt["caSha256"]),
    ):
        raise CertificateError(
            "the exact simulator still trusts this CA; delete it and finalize instead"
        )
    remove_receipt(directory, manifest)
    return True


def migrate_legacy_trust_receipt(
    directory: Path,
    *,
    confirmation: str,
    runner=subprocess.run,
) -> Optional[Mapping[str, object]]:
    """Explicitly bind a strictly validated v1/v2 receipt to schema v3."""

    if confirmation != TRUST_RECEIPT_MIGRATION_CONFIRMATION:
        raise CertificateError(
            "refusing trust receipt migration without the exact confirmation phrase"
        )
    directory = directory.expanduser().resolve()
    if not _trust_artifact_paths(directory):
        return None
    manifest = validated_manifest(directory)
    legacy = read_receipt(directory, manifest, allow_legacy=True)
    if legacy is None:
        return None
    if legacy.get("schemaVersion") == RECEIPT_SCHEMA_VERSION:
        return legacy
    if legacy.get("installationState", INSTALLATION_INSTALLED) == INSTALLATION_INSTALLED:
        trust_type = legacy.get("trustType", TRUST_TYPE_MACOS)
        if trust_type == TRUST_TYPE_MACOS:
            verified = _macos_trust_installation_is_valid(legacy, manifest, runner=runner)
        else:
            try:
                identity = _recorded_simulator_identity(legacy, runner=runner)
                verified = _simulator_root_store_contains_ca(
                    Path(identity["simulatorDataPath"]),
                    str(legacy["caSha256"]),
                )
            except CertificateError:
                verified = False
        if not verified:
            raise CertificateError(
                "legacy installed trust could not be verified; receipt was not migrated"
            )
    migrated = migrated_receipt(legacy, manifest)
    write_migration(directory, manifest, legacy, migrated)
    return migrated
