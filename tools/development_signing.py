#!/usr/bin/env python3
"""Resolve a stable, local-only macOS identity for Ahoi development builds."""

from __future__ import annotations

import argparse
import base64
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Iterable, Optional


APPLE_DEVELOPMENT_PREFIX = "Apple Development: "
IDENTITY_LINE = re.compile(
    r'^\s*\d+\)\s+([0-9A-Fa-f]{40})\s+"([^"]+)"\s*$'
)


class DevelopmentSigningError(RuntimeError):
    """Raised when a stable development identity cannot be selected safely."""


@dataclass(frozen=True)
class CodeSigningIdentity:
    fingerprint: str
    name: str


def parse_identities(output: str) -> tuple[CodeSigningIdentity, ...]:
    """Parse only valid identity rows emitted by security(1)."""
    identities = []
    seen = set()
    for line in output.splitlines():
        match = IDENTITY_LINE.fullmatch(line)
        if not match:
            continue
        identity = CodeSigningIdentity(
            fingerprint=match.group(1).upper(),
            name=match.group(2),
        )
        key = (identity.fingerprint, identity.name)
        if key not in seen:
            identities.append(identity)
            seen.add(key)
    return tuple(identities)


def select_identity(
    identities: Iterable[CodeSigningIdentity],
    *,
    configured: Optional[str] = None,
    allow_adhoc: bool = False,
) -> str:
    """Select exactly one Apple Development identity or an explicit ad-hoc opt-in."""
    available = tuple(identities)
    configured = configured or None
    if configured == "-":
        if not allow_adhoc:
            raise DevelopmentSigningError(
                "AHOI_DEV_CODESIGN_IDENTITY=- requires "
                "AHOI_ALLOW_ADHOC_DEV_SIGNING=1"
            )
        return "-"
    if configured:
        if not configured.startswith(APPLE_DEVELOPMENT_PREFIX):
            raise DevelopmentSigningError(
                "development builds accept only an Apple Development identity; "
                "Developer ID Application is reserved for the release pipeline"
            )
        if configured not in {identity.name for identity in available}:
            raise DevelopmentSigningError(
                "configured Apple Development identity is not valid in the current "
                f"keychain: {configured}"
            )
        return configured

    candidates = tuple(
        identity.name
        for identity in available
        if identity.name.startswith(APPLE_DEVELOPMENT_PREFIX)
    )
    if len(candidates) == 1:
        return candidates[0]
    if not candidates and allow_adhoc:
        return "-"
    if not candidates:
        raise DevelopmentSigningError(
            "no valid Apple Development identity was found; configure one through "
            "Xcode, or explicitly opt into unstable ad-hoc development signing with "
            "AHOI_ALLOW_ADHOC_DEV_SIGNING=1"
        )
    raise DevelopmentSigningError(
        "multiple Apple Development identities were found; set "
        "AHOI_DEV_CODESIGN_IDENTITY to the exact intended identity"
    )


def read_security_identities(
    *, keychain: Optional[str] = None, valid_only: bool = True,
) -> tuple[CodeSigningIdentity, ...]:
    command = ["security", "find-identity"]
    if valid_only:
        command.append("-v")
    command.extend(["-p", "codesigning"])
    if keychain:
        command.append(keychain)
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode:
        detail = (completed.stderr or completed.stdout).strip()
        raise DevelopmentSigningError(
            f"cannot inspect macOS code-signing identities: {detail}"
        )
    return parse_identities(completed.stdout)


def verify_explicit_certificate(
    identity: CodeSigningIdentity, keychain: str,
) -> None:
    """Verify the exact public leaf when an isolated search list omits it."""
    certificates = subprocess.run(
        ["security", "find-certificate", "-a", "-p", "-c", identity.name,
         keychain], capture_output=True, text=True, check=False,
    )
    leaves = []
    for pem in re.findall(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
        certificates.stdout, re.DOTALL,
    ):
        encoded = "".join(pem.splitlines()[1:-1])
        fingerprint = hashlib.sha1(base64.b64decode(encoded)).hexdigest()
        if fingerprint.upper() == identity.fingerprint:
            leaves.append(pem)
    if certificates.returncode or len(leaves) != 1:
        raise DevelopmentSigningError("cannot bind the exact development leaf")
    # Only public certificate bytes leave the Keychain. No search-list, trust,
    # ACL or private-key change is made; native trust evaluation must succeed.
    with tempfile.TemporaryDirectory(prefix="ahoi-development-leaf-") as folder:
        leaf = pathlib.Path(folder) / "leaf.pem"
        leaf.write_text(leaves[0] + "\n")
        verified = subprocess.run(
            ["security", "verify-cert", "-p", "codeSign", "-c", str(leaf),
             "-k", keychain, "-k", "/Library/Keychains/System.keychain"],
            capture_output=True, text=True, check=False,
        )
    if verified.returncode:
        raise DevelopmentSigningError(
            "explicit Apple Development certificate fails native trust evaluation"
        )


def boolean_environment(name: str) -> bool:
    value = os.environ.get(name, "0")
    if value not in {"0", "1"}:
        raise DevelopmentSigningError(f"{name} must be 0 or 1")
    return value == "1"


def resolve_from_environment() -> str:
    configured = os.environ.get("AHOI_DEV_CODESIGN_IDENTITY") or None
    keychain = os.environ.get("AHOI_DEV_CODESIGN_KEYCHAIN") or None
    if keychain and (not pathlib.Path(keychain).is_absolute()
                     or not pathlib.Path(keychain).is_file()):
        raise DevelopmentSigningError("development Keychain must be an absolute file")
    identities = read_security_identities(keychain=keychain)
    if (keychain and configured
            and configured.startswith(APPLE_DEVELOPMENT_PREFIX)
            and configured not in {item.name for item in identities}):
        matches = tuple(item for item in read_security_identities(
            keychain=keychain, valid_only=False,
        ) if item.name == configured)
        if len(matches) != 1:
            raise DevelopmentSigningError("explicit development identity is ambiguous or absent")
        verify_explicit_certificate(matches[0], keychain)
        identities = (*identities, matches[0])
    return select_identity(
        identities,
        configured=configured,
        allow_adhoc=boolean_environment("AHOI_ALLOW_ADHOC_DEV_SIGNING"),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        print(resolve_from_environment())
    except DevelopmentSigningError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
