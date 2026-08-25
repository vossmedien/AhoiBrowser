#!/usr/bin/env python3
"""Ed25519 detached-signature helpers using the system OpenSSL binary."""

from __future__ import annotations

import base64
import pathlib
import tempfile
from typing import Optional

from .common import ReleaseError, run, sha256_bytes


ALGORITHM = "Ed25519"


def public_key_id(public_key: pathlib.Path) -> str:
    completed = run(
        [
            "openssl",
            "pkey",
            "-pubin",
            "-in",
            str(public_key),
            "-outform",
            "DER",
        ]
    )
    return sha256_bytes(completed.stdout)


def sign_bytes(payload: bytes, private_key: pathlib.Path) -> bytes:
    if not private_key.is_file():
        raise ReleaseError(f"Ed25519 private key is missing: {private_key}")
    with tempfile.TemporaryDirectory(prefix="ahoi-ed25519-sign-") as directory:
        root = pathlib.Path(directory)
        payload_path = root / "payload"
        signature_path = root / "signature"
        payload_path.write_bytes(payload)
        run(
            [
                "openssl",
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(private_key),
                "-in",
                str(payload_path),
                "-out",
                str(signature_path),
            ]
        )
        signature = signature_path.read_bytes()
    if len(signature) != 64:
        raise ReleaseError("OpenSSL did not produce a 64-byte Ed25519 signature")
    return signature


def verify_bytes(payload: bytes, signature: bytes, public_key: pathlib.Path) -> None:
    if not public_key.is_file():
        raise ReleaseError(f"Ed25519 public key is missing: {public_key}")
    if len(signature) != 64:
        raise ReleaseError("Ed25519 signature must be exactly 64 bytes")
    with tempfile.TemporaryDirectory(prefix="ahoi-ed25519-verify-") as directory:
        root = pathlib.Path(directory)
        payload_path = root / "payload"
        signature_path = root / "signature"
        payload_path.write_bytes(payload)
        signature_path.write_bytes(signature)
        run(
            [
                "openssl",
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                str(public_key),
                "-in",
                str(payload_path),
                "-sigfile",
                str(signature_path),
            ]
        )


def signature_object(
    payload: bytes,
    private_key: pathlib.Path,
    public_key: pathlib.Path,
    expected_key_id: Optional[str] = None,
) -> dict:
    key_id = public_key_id(public_key)
    if expected_key_id is not None and key_id != expected_key_id:
        raise ReleaseError("provided public key does not match the expected key ID")
    signature = sign_bytes(payload, private_key)
    verify_bytes(payload, signature, public_key)
    return {
        "algorithm": ALGORITHM,
        "keyId": key_id,
        "value": base64.b64encode(signature).decode("ascii"),
    }


def verify_signature_object(
    payload: bytes,
    signature: object,
    public_key: pathlib.Path,
    trusted_key_ids: Optional[set] = None,
) -> str:
    if not isinstance(signature, dict) or set(signature) != {
        "algorithm",
        "keyId",
        "value",
    }:
        raise ReleaseError("signature object has an invalid shape")
    if signature["algorithm"] != ALGORITHM:
        raise ReleaseError("only Ed25519 signatures are accepted")
    actual_key_id = public_key_id(public_key)
    if signature["keyId"] != actual_key_id:
        raise ReleaseError("signature key ID does not match the provided public key")
    if trusted_key_ids is not None and actual_key_id not in trusted_key_ids:
        raise ReleaseError("signature key is not trusted for this channel")
    try:
        raw_signature = base64.b64decode(signature["value"], validate=True)
    except (ValueError, TypeError) as error:
        raise ReleaseError("signature value is not valid base64") from error
    verify_bytes(payload, raw_signature, public_key)
    return actual_key_id
