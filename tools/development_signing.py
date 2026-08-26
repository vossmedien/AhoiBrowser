#!/usr/bin/env python3
"""Resolve a stable, local-only macOS identity for Ahoi development builds."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
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


def read_security_identities() -> tuple[CodeSigningIdentity, ...]:
    completed = subprocess.run(
        ["security", "find-identity", "-v", "-p", "codesigning"],
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


def boolean_environment(name: str) -> bool:
    value = os.environ.get(name, "0")
    if value not in {"0", "1"}:
        raise DevelopmentSigningError(f"{name} must be 0 or 1")
    return value == "1"


def resolve_from_environment() -> str:
    configured = os.environ.get("AHOI_DEV_CODESIGN_IDENTITY") or None
    return select_identity(
        read_security_identities(),
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
