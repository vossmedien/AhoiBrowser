#!/usr/bin/env python3
"""Explicit CA/leaf certificate and macOS user-trust lifecycle."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Callable, Mapping, Optional, Sequence


CERTIFICATE_MANIFEST = "certificates.json"
TRUST_RECEIPT = "trust-receipt.json"
TRUST_CONFIRMATION = "I-understand-this-adds-a-local-test-CA"
REMOVE_CONFIRMATION = "remove-the-local-test-CA"
HOST_NAMES = (
    "localhost",
    "first-party.localhost",
    "third-party.localhost",
    "media.localhost",
)


class CertificateError(RuntimeError):
    """A certificate or explicit-trust operation failed closed."""


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


def _manifest_path(directory: Path) -> Path:
    return directory / CERTIFICATE_MANIFEST


def _trust_path(directory: Path) -> Path:
    return directory / TRUST_RECEIPT


def read_manifest(directory: Path) -> Optional[Mapping[str, object]]:
    try:
        value = json.loads(_manifest_path(directory).read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    return value if isinstance(value, dict) else None


def read_trust_receipt(directory: Path) -> Optional[Mapping[str, object]]:
    try:
        value = json.loads(_trust_path(directory).read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    return value if isinstance(value, dict) else None


def generate(directory: Path, *, rotate: bool = False, runner=subprocess.run) -> Mapping[str, object]:
    """Create a unique P-256 CA and CA-signed localhost leaf certificate."""

    directory.mkdir(parents=True, exist_ok=True)
    existing = read_manifest(directory)
    if existing is not None and not rotate:
        required = ("caCertificate", "leafCertificate", "leafPrivateKey")
        if all(Path(str(existing.get(key, ""))).is_file() for key in required):
            return existing
    if rotate and read_trust_receipt(directory) is not None:
        raise CertificateError("remove the trusted CA before rotating certificates")

    openssl = shutil.which("openssl")
    if openssl is None:
        raise CertificateError("openssl is required to generate fixture certificates")
    ca_key = directory / "ahoi-e2e-ca-key.pem"
    ca_certificate = directory / "ahoi-e2e-ca.pem"
    leaf_key = directory / "ahoi-e2e-leaf-key.pem"
    leaf_request = directory / "ahoi-e2e-leaf.csr"
    leaf_certificate = directory / "ahoi-e2e-leaf.pem"
    leaf_extensions = directory / "ahoi-e2e-leaf.ext"
    serial_file = directory / "ahoi-e2e-ca.srl"
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
        [
            openssl,
            "req",
            "-new",
            "-x509",
            "-sha256",
            "-days",
            "7",
            "-key",
            str(ca_key),
            "-out",
            str(ca_certificate),
            "-subj",
            "/CN=AhoiBrowser Local E2E Test CA/O=AhoiBrowser Local Test Only",
            "-addext",
            "basicConstraints=critical,CA:TRUE,pathlen:0",
            "-addext",
            "keyUsage=critical,keyCertSign,cRLSign",
        ],
        [openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(leaf_key)],
        [
            openssl,
            "req",
            "-new",
            "-sha256",
            "-key",
            str(leaf_key),
            "-out",
            str(leaf_request),
            "-subj",
            "/CN=first-party.localhost/O=AhoiBrowser Local Test Only",
        ],
        [
            openssl,
            "x509",
            "-req",
            "-sha256",
            "-days",
            "3",
            "-in",
            str(leaf_request),
            "-CA",
            str(ca_certificate),
            "-CAkey",
            str(ca_key),
            "-CAcreateserial",
            "-out",
            str(leaf_certificate),
            "-extfile",
            str(leaf_extensions),
        ],
        [openssl, "verify", "-CAfile", str(ca_certificate), str(leaf_certificate)],
    )
    for command in commands:
        _run(command, runner=runner)
    os.chmod(ca_key, 0o600)
    os.chmod(leaf_key, 0o600)
    leaf_request.unlink(missing_ok=True)
    leaf_extensions.unlink(missing_ok=True)
    serial_file.unlink(missing_ok=True)
    manifest = {
        "schemaVersion": 1,
        "caCertificate": str(ca_certificate.resolve()),
        "caPrivateKey": str(ca_key.resolve()),
        "leafCertificate": str(leaf_certificate.resolve()),
        "leafPrivateKey": str(leaf_key.resolve()),
        "hostNames": list(HOST_NAMES),
        "caSha256": _fingerprint(ca_certificate, "sha256", runner=runner),
        "caSha1": _fingerprint(ca_certificate, "sha1", runner=runner),
        "leafSha256": _fingerprint(leaf_certificate, "sha256", runner=runner),
    }
    pending = _manifest_path(directory).with_suffix(".json.pending")
    pending.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    pending.replace(_manifest_path(directory))
    return manifest


def default_user_keychain(*, runner=subprocess.run) -> Path:
    output = _run(["security", "default-keychain", "-d", "user"], runner=runner)
    path = output.strip().strip('"')
    if not path:
        raise CertificateError("macOS did not report a default user keychain")
    return Path(path).expanduser().resolve()


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
    if read_trust_receipt(directory) is not None:
        raise CertificateError("this fixture CA already has an installation receipt")
    manifest = read_manifest(directory)
    if manifest is None:
        raise CertificateError("generate certificates before installing trust")
    ca_certificate = Path(str(manifest["caCertificate"])).resolve()
    expected = str(manifest["caSha256"])
    if _fingerprint(ca_certificate, "sha256", runner=runner) != expected:
        raise CertificateError("CA fingerprint changed after certificate generation")
    selected_keychain = (keychain or default_user_keychain(runner=runner)).resolve()
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
    receipt = {
        "schemaVersion": 1,
        "explicitConsent": True,
        "keychain": str(selected_keychain),
        "caSha1": str(manifest["caSha1"]),
        "caSha256": expected,
    }
    _trust_path(directory).write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return receipt


def remove_trust(
    directory: Path,
    *,
    confirmation: str,
    runner=subprocess.run,
) -> bool:
    """Remove exactly the CA identified by the local installation receipt."""

    if confirmation != REMOVE_CONFIRMATION:
        raise CertificateError("refusing trust removal without the exact confirmation phrase")
    receipt = read_trust_receipt(directory)
    if receipt is None:
        return False
    fingerprint = str(receipt.get("caSha256", ""))
    keychain = Path(str(receipt.get("keychain", ""))).resolve()
    if len(fingerprint) != 64 or not all(character in "0123456789abcdef" for character in fingerprint):
        raise CertificateError("trust receipt has an invalid CA fingerprint")
    _run(
        ["security", "delete-certificate", "-t", "-Z", fingerprint, str(keychain)],
        runner=runner,
    )
    _trust_path(directory).unlink()
    return True


def trust_installation_is_valid(directory: Path, *, runner=subprocess.run) -> bool:
    """Verify the exact recorded CA exists and validates the fixture leaf."""

    receipt = read_trust_receipt(directory)
    manifest = read_manifest(directory)
    if receipt is None or manifest is None:
        return False
    fingerprint = str(receipt.get("caSha256", ""))
    keychain = Path(str(receipt.get("keychain", ""))).resolve()
    try:
        inventory = _run(
            ["security", "find-certificate", "-a", "-Z", str(keychain)],
            runner=runner,
        )
        normalized_inventory = "".join(
            character.lower() for character in inventory if character.isalnum()
        )
        if fingerprint not in normalized_inventory:
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
                str(keychain),
                "-L",
                "-q",
            ],
            runner=runner,
        )
    except CertificateError:
        return False
    return True


def remove_certificate_material(directory: Path) -> None:
    if read_trust_receipt(directory) is not None:
        raise CertificateError("remove the trusted CA before deleting certificate material")
    manifest = read_manifest(directory)
    if manifest:
        for key in ("caCertificate", "caPrivateKey", "leafCertificate", "leafPrivateKey"):
            candidate = Path(str(manifest.get(key, "")))
            if candidate.parent.resolve() == directory.resolve():
                candidate.unlink(missing_ok=True)
    _manifest_path(directory).unlink(missing_ok=True)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()
