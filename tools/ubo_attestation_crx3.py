#!/usr/bin/env python3
"""Strict, dependency-free CRX3 parsing and independent proof verification."""

from __future__ import annotations

import hashlib
import io
import json
import os
import pathlib
import stat
import struct
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from typing import Iterable, Protocol


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAX_CRX_HEADER_BYTES = 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_ZIP_ENTRIES = 100_000
MAX_ZIP_UNCOMPRESSED_BYTES = 512 * 1024 * 1024
SIGNATURE_CONTEXT = b"CRX3 SignedData\x00"
RSA_SPKI_OID = bytes.fromhex("2a864886f70d010101")
EC_PUBLIC_KEY_OID = bytes.fromhex("2a8648ce3d0201")
P256_OID = bytes.fromhex("2a8648ce3d030107")


class AttestationError(RuntimeError):
    """A fail-closed uBO attestation error safe to show to an operator."""


class CrxPins(Protocol):
    version: str
    package_sha256: str
    package_size: int
    public_key_sha256: str
    extension_id: str


@dataclass(frozen=True)
class Proof:
    algorithm: str
    public_key: bytes
    signature: bytes


@dataclass(frozen=True)
class VerifiedCrx:
    public_key: bytes
    public_key_sha256: str
    derived_id: str
    declared_id: str
    manifest_sha256: str
    manifest_version: int
    extension_version: str
    rsa_proof_count: int
    ecdsa_proof_count: int


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _decode_varint(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    start = offset
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise AttestationError("CRX3 protobuf contains a truncated varint")
        byte = data[offset]
        offset += 1
        if shift == 63 and byte > 1:
            raise AttestationError("CRX3 protobuf varint exceeds 64 bits")
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            if data[start:offset] != _encode_varint(value):
                raise AttestationError("CRX3 protobuf contains a non-canonical varint")
            return value, offset
    raise AttestationError("CRX3 protobuf varint exceeds 64 bits")


def _encode_varint(value: int) -> bytes:
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        encoded.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(encoded)


def _protobuf_bytes_fields(data: bytes, label: str) -> dict[int, list[bytes]]:
    result: dict[int, list[bytes]] = {}
    offset = 0
    while offset < len(data):
        tag, offset = _decode_varint(data, offset)
        field_number = tag >> 3
        wire_type = tag & 7
        if field_number == 0 or field_number >= 1 << 29 or wire_type != 2:
            raise AttestationError(f"{label} contains an unsupported protobuf field")
        length, offset = _decode_varint(data, offset)
        end = offset + length
        if end < offset or end > len(data):
            raise AttestationError(f"{label} contains a truncated protobuf field")
        result.setdefault(field_number, []).append(data[offset:end])
        offset = end
    return result


def _exact_field(fields: dict[int, list[bytes]], number: int, label: str) -> bytes:
    values = fields.get(number, [])
    if len(values) != 1:
        raise AttestationError(f"{label} must occur exactly once")
    return values[0]


def _proofs(values: Iterable[bytes], algorithm: str) -> list[Proof]:
    proofs: list[Proof] = []
    for value in values:
        fields = _protobuf_bytes_fields(value, f"CRX3 {algorithm} proof")
        if set(fields) != {1, 2}:
            raise AttestationError(f"CRX3 {algorithm} proof has unknown fields")
        key = _exact_field(fields, 1, f"CRX3 {algorithm} public key")
        signature = _exact_field(fields, 2, f"CRX3 {algorithm} signature")
        if (
            not key
            or len(key) > 64 * 1024
            or not signature
            or len(signature) > 64 * 1024
        ):
            raise AttestationError(f"CRX3 {algorithm} proof exceeds safe bounds")
        proofs.append(Proof(algorithm, key, signature))
    return proofs


def _der_length(data: bytes, offset: int) -> tuple[int, int]:
    if offset >= len(data):
        raise AttestationError("CRX3 public key contains truncated DER")
    first = data[offset]
    offset += 1
    if first < 0x80:
        return first, offset
    octets = first & 0x7F
    if octets == 0 or octets > 4 or offset + octets > len(data):
        raise AttestationError("CRX3 public key contains invalid DER length")
    if data[offset] == 0:
        raise AttestationError("CRX3 public key contains non-canonical DER length")
    length = int.from_bytes(data[offset : offset + octets], "big")
    if length < 0x80:
        raise AttestationError("CRX3 public key contains non-canonical DER length")
    return length, offset + octets


def _der_tlv(data: bytes, offset: int, expected_tag: int) -> tuple[bytes, int]:
    if offset >= len(data) or data[offset] != expected_tag:
        raise AttestationError("CRX3 public key has an unexpected DER type")
    length, content_offset = _der_length(data, offset + 1)
    end = content_offset + length
    if end < content_offset or end > len(data):
        raise AttestationError("CRX3 public key contains truncated DER content")
    return data[content_offset:end], end


def _verify_spki_algorithm(spki: bytes, algorithm: str) -> None:
    outer, outer_end = _der_tlv(spki, 0, 0x30)
    if outer_end != len(spki):
        raise AttestationError("CRX3 public key has trailing DER data")
    algorithm_sequence, offset = _der_tlv(outer, 0, 0x30)
    bit_string, end = _der_tlv(outer, offset, 0x03)
    if end != len(outer) or not bit_string or bit_string[0] != 0:
        raise AttestationError("CRX3 public key has invalid SPKI key bits")
    oid, algorithm_offset = _der_tlv(algorithm_sequence, 0, 0x06)
    if algorithm == "rsa":
        null, algorithm_end = _der_tlv(algorithm_sequence, algorithm_offset, 0x05)
        if oid != RSA_SPKI_OID or null or algorithm_end != len(algorithm_sequence):
            raise AttestationError("CRX3 RSA proof does not contain an RSA SPKI key")
        return
    curve, algorithm_end = _der_tlv(algorithm_sequence, algorithm_offset, 0x06)
    if (
        oid != EC_PUBLIC_KEY_OID
        or curve != P256_OID
        or algorithm_end != len(algorithm_sequence)
    ):
        raise AttestationError("CRX3 ECDSA proof is not an NIST P-256 SPKI key")


def derive_extension_id(public_key: bytes) -> str:
    first_128_bits = hashlib.sha256(public_key).hexdigest()[:32]
    return "".join(chr(ord("a") + int(character, 16)) for character in first_128_bits)


def _canonical_openssl(path: pathlib.Path) -> tuple[pathlib.Path, str]:
    if not path.is_absolute():
        raise AttestationError("OpenSSL path must be absolute")
    try:
        resolved = path.resolve(strict=True)
        metadata = resolved.stat()
    except OSError as error:
        raise AttestationError("OpenSSL binary is unavailable") from error
    if (
        not stat.S_ISREG(metadata.st_mode)
        or not os.access(resolved, os.X_OK)
        or metadata.st_uid != 0
        or metadata.st_mode & 0o022
    ):
        raise AttestationError(
            "OpenSSL binary is not a root-owned immutable executable"
        )
    return resolved, sha256_file(resolved)


def _openssl_environment() -> dict[str, str]:
    return {
        "LANG": "C",
        "LC_ALL": "C",
        "PATH": "/usr/bin:/bin",
        "OPENSSL_CONF": "/dev/null",
    }


def _write_private_temp(path: pathlib.Path, data: bytes) -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())


def _verify_proof(
    proof: Proof,
    signed_message: bytes,
    openssl: pathlib.Path,
    directory: pathlib.Path,
    index: int,
) -> None:
    _verify_spki_algorithm(proof.public_key, proof.algorithm)
    key_path = directory / f"proof-{index}.spki.der"
    signature_path = directory / f"proof-{index}.signature"
    _write_private_temp(key_path, proof.public_key)
    _write_private_temp(signature_path, proof.signature)
    try:
        completed = subprocess.run(
            [
                str(openssl),
                "dgst",
                "-sha256",
                "-verify",
                str(key_path),
                "-keyform",
                "DER",
                "-signature",
                str(signature_path),
            ],
            cwd=directory,
            env=_openssl_environment(),
            input=signed_message,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=60,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AttestationError(
            "independent OpenSSL CRX3 verification failed"
        ) from error
    if completed.returncode != 0:
        raise AttestationError(f"CRX3 {proof.algorithm} proof {index} is invalid")


def _manifest_from_archive(
    archive: bytes, expected_version: str
) -> tuple[str, int, str]:
    if not archive.startswith(b"PK"):
        raise AttestationError("CRX3 payload is not a ZIP archive")
    try:
        with zipfile.ZipFile(io.BytesIO(archive), "r") as package:
            infos = package.infolist()
            if not infos or len(infos) > MAX_ZIP_ENTRIES:
                raise AttestationError("CRX3 ZIP entry count exceeds safe bounds")
            names: set[str] = set()
            total_size = 0
            for info in infos:
                name = info.filename
                parts = pathlib.PurePosixPath(name).parts
                if (
                    not name
                    or "\\" in name
                    or "\x00" in name
                    or name.startswith("/")
                    or any(part in {"", ".", ".."} for part in parts)
                    or name in names
                    or info.flag_bits & 1
                    or stat.S_IFMT(info.external_attr >> 16) == stat.S_IFLNK
                ):
                    raise AttestationError("CRX3 ZIP contains an unsafe entry")
                names.add(name)
                total_size += info.file_size
                if total_size > MAX_ZIP_UNCOMPRESSED_BYTES:
                    raise AttestationError(
                        "CRX3 ZIP exceeds the uncompressed size limit"
                    )
            manifests = [info for info in infos if info.filename == "manifest.json"]
            if len(manifests) != 1 or manifests[0].file_size > MAX_MANIFEST_BYTES:
                raise AttestationError(
                    "CRX3 ZIP lacks one safely bounded manifest.json"
                )
            manifest_bytes = package.read(manifests[0])
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise AttestationError("CRX3 ZIP archive is invalid") from error
    if len(manifest_bytes) > MAX_MANIFEST_BYTES:
        raise AttestationError("CRX3 manifest exceeds the size limit")
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AttestationError("CRX3 manifest is not strict UTF-8 JSON") from error
    if not isinstance(manifest, dict):
        raise AttestationError("CRX3 manifest must be a JSON object")
    manifest_version = manifest.get("manifest_version")
    extension_version = manifest.get("version")
    if type(manifest_version) is not int or manifest_version != 2:
        raise AttestationError("uBO package is not exactly Manifest V2")
    if extension_version != expected_version:
        raise AttestationError("uBO manifest version differs from the release pin")
    if "update_url" in manifest:
        raise AttestationError("uBO package controls its own update_url")
    return sha256_bytes(manifest_bytes), manifest_version, extension_version


def verify_crx(
    path: pathlib.Path, pins: CrxPins, openssl_path: pathlib.Path
) -> tuple[VerifiedCrx, dict]:
    if (
        path.stat().st_size != pins.package_size
        or sha256_file(path) != pins.package_sha256
    ):
        raise AttestationError("temporary CRX changed before verification")
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"Cr24" or struct.unpack("<I", data[4:8])[0] != 3:
        raise AttestationError("package is not a full CRX3 file")
    header_size = struct.unpack("<I", data[8:12])[0]
    if (
        header_size == 0
        or header_size > MAX_CRX_HEADER_BYTES
        or 12 + header_size >= len(data)
    ):
        raise AttestationError("CRX3 header size exceeds safe bounds")
    header_bytes = data[12 : 12 + header_size]
    if b"PK\x05\x06" in header_bytes or b"PK\x06\x07" in header_bytes:
        raise AttestationError("CRX3 header contains a forbidden ZIP end marker")
    fields = _protobuf_bytes_fields(header_bytes, "CRX3 header")
    if not set(fields).issubset({2, 3, 4, 10000}) or set(fields) & {2, 3} == set():
        raise AttestationError("CRX3 header contains unknown fields or no proofs")
    if len(fields.get(4, [])) > 1 or len(fields.get(10000, [])) != 1:
        raise AttestationError("CRX3 header has an ambiguous signed-data section")
    signed_header = _exact_field(fields, 10000, "CRX3 signed header")
    signed_fields = _protobuf_bytes_fields(signed_header, "CRX3 signed header")
    if set(signed_fields) != {1}:
        raise AttestationError("CRX3 signed header contains unknown fields")
    declared_id_bytes = _exact_field(signed_fields, 1, "CRX3 declared ID")
    if len(declared_id_bytes) != 16:
        raise AttestationError("CRX3 declared ID is not exactly 128 bits")
    proofs = _proofs(fields.get(2, []), "rsa") + _proofs(fields.get(3, []), "ecdsa")
    archive = data[12 + header_size :]
    signed_message = (
        SIGNATURE_CONTEXT
        + struct.pack("<I", len(signed_header))
        + signed_header
        + archive
    )
    openssl, openssl_sha256 = _canonical_openssl(openssl_path)
    with tempfile.TemporaryDirectory(prefix="ahoi-ubo-crx3-proof-") as raw_directory:
        directory = pathlib.Path(raw_directory)
        directory.chmod(0o700)
        for index, proof in enumerate(proofs):
            _verify_proof(proof, signed_message, openssl, directory, index)
    if sha256_file(openssl) != openssl_sha256:
        raise AttestationError("OpenSSL binary changed during CRX3 verification")
    matching = [
        proof
        for proof in proofs
        if hashlib.sha256(proof.public_key).digest()[:16] == declared_id_bytes
    ]
    if len(matching) != 1 or matching[0].algorithm != "rsa":
        raise AttestationError("CRX3 has no unique RSA developer-key proof")
    developer_key = matching[0].public_key
    key_sha256 = sha256_bytes(developer_key)
    derived_id = derive_extension_id(developer_key)
    declared_id = "".join(
        chr(ord("a") + int(character, 16)) for character in declared_id_bytes.hex()
    )
    if (
        key_sha256 != pins.public_key_sha256
        or derived_id != pins.extension_id
        or declared_id != pins.extension_id
    ):
        raise AttestationError(
            "CRX3 key, declared ID, or derived ID differs from the pin"
        )
    manifest_sha256, manifest_version, extension_version = _manifest_from_archive(
        archive, pins.version
    )
    try:
        version = subprocess.run(
            [str(openssl), "version"],
            cwd=ROOT,
            env=_openssl_environment(),
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AttestationError("OpenSSL version cannot be recorded") from error
    if version.returncode != 0 or not version.stdout.strip():
        raise AttestationError("OpenSSL version cannot be recorded")
    if sha256_file(openssl) != openssl_sha256:
        raise AttestationError("OpenSSL binary changed while recording its version")
    verified = VerifiedCrx(
        public_key=developer_key,
        public_key_sha256=key_sha256,
        derived_id=derived_id,
        declared_id=declared_id,
        manifest_sha256=manifest_sha256,
        manifest_version=manifest_version,
        extension_version=extension_version,
        rsa_proof_count=len(fields.get(2, [])),
        ecdsa_proof_count=len(fields.get(3, [])),
    )
    verifier = {
        "implementation": "independent OpenSSL verification of every CRX3 proof",
        "opensslPath": str(openssl),
        "opensslSha256": openssl_sha256,
        "opensslVersion": version.stdout.strip(),
        "signatureContext": "CRX3 SignedData\\0",
    }
    return verified, verifier
