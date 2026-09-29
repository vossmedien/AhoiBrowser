#!/usr/bin/env python3
"""Exact-pin HTTPS transport for the uBlock Origin Classic release asset.

The pinned values come from ubo_release_attestation; this module only knows
how to follow exactly one GitHub redirect to a pinned asset and stream it
into a new file while checking its size and SHA-256.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import http.client
import os
import pathlib
import ssl
import urllib.parse
from typing import Callable, Protocol

from ubo_attestation_crx3 import AttestationError


class PackagePins(Protocol):
    package_url: str
    package_size: int
    package_sha256: str


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def _safe_url(value: str, label: str) -> urllib.parse.SplitResult:
    if (
        not value.isascii()
        or len(value) > 8192
        or any(ord(character) < 0x20 for character in value)
    ):
        raise AttestationError(f"{label} contains unsafe characters")
    parsed = urllib.parse.urlsplit(value)
    try:
        port = parsed.port
    except ValueError as error:
        raise AttestationError(f"{label} has an invalid port") from error
    if (
        parsed.scheme != "https"
        or parsed.username is not None
        or parsed.password is not None
        or port is not None
        or parsed.fragment
        or not parsed.hostname
        or parsed.geturl() != value
    ):
        raise AttestationError(f"{label} is not a canonical credentialless HTTPS URL")
    return parsed


def validate_response_chain(
    start_url: str,
    status: int,
    locations: list[str],
    *,
    expected_start_url: str,
    asset_host: str,
    asset_path: str,
) -> str:
    start = _safe_url(start_url, "package start URL")
    if start_url != expected_start_url or start.hostname != "github.com":
        raise AttestationError("package request did not start at the exact GitHub pin")
    if status != 302 or len(locations) != 1:
        raise AttestationError("package request requires exactly one HTTP 302 Location")
    final_url = locations[0]
    final = _safe_url(final_url, "package final URL")
    if (
        final.hostname != asset_host
        or final.netloc != asset_host
        or final.path != asset_path
        or not final.query
    ):
        raise AttestationError("package redirect differs from the pinned release asset")
    return final_url


def _request_target(parsed: urllib.parse.SplitResult) -> str:
    return parsed.path + (f"?{parsed.query}" if parsed.query else "")


def _connection(host: str, timeout: float) -> http.client.HTTPSConnection:
    context = ssl.create_default_context()
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    return http.client.HTTPSConnection(host, timeout=timeout, context=context)


def fetch_package(
    path: pathlib.Path,
    pins: PackagePins,
    timeout: float,
    *,
    validate_chain: Callable[[str, int, list[str]], str],
    max_bytes: int,
) -> dict:
    started_at = utc_now()
    headers = {
        "Accept": "application/octet-stream",
        "Accept-Encoding": "identity",
        "Cache-Control": "no-cache, no-store",
        "Pragma": "no-cache",
        "User-Agent": "AhoiBrowser-uBO-release-attestation/1",
    }
    start = _safe_url(pins.package_url, "package start URL")
    first = _connection(start.hostname or "", timeout)
    try:
        first.request("GET", _request_target(start), headers=headers)
        response = first.getresponse()
        locations = [
            value
            for name, value in response.getheaders()
            if name.lower() == "location"
        ]
        final_url = validate_chain(pins.package_url, response.status, locations)
    except (OSError, ssl.SSLError, http.client.HTTPException) as error:
        raise AttestationError("initial GitHub package request failed") from error
    finally:
        first.close()

    final = _safe_url(final_url, "package final URL")
    second = _connection(final.hostname or "", timeout)
    digest = hashlib.sha256()
    byte_count = 0
    try:
        second.request("GET", _request_target(final), headers=headers)
        response = second.getresponse()
        if response.status != 200:
            raise AttestationError("release asset did not return HTTP 200")
        if any(name.lower() == "location" for name, _ in response.getheaders()):
            raise AttestationError("release asset attempted a second redirect")
        encoding = response.getheader("Content-Encoding")
        if encoding not in (None, "", "identity"):
            raise AttestationError("release asset used an unsupported content encoding")
        content_length = response.getheader("Content-Length")
        if content_length is not None:
            try:
                declared_length = int(content_length)
            except ValueError as error:
                raise AttestationError(
                    "release asset Content-Length is invalid"
                ) from error
            if declared_length != pins.package_size:
                raise AttestationError(
                    "release asset Content-Length differs from the pin"
                )
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(descriptor, "wb") as handle:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    byte_count += len(chunk)
                    if byte_count > max_bytes or byte_count > pins.package_size:
                        raise AttestationError(
                            "release asset exceeded its exact size pin"
                        )
                    digest.update(chunk)
                    handle.write(chunk)
                handle.flush()
                os.fsync(handle.fileno())
        except BaseException:
            path.unlink(missing_ok=True)
            raise
    except (OSError, ssl.SSLError, http.client.HTTPException) as error:
        raise AttestationError("final GitHub release-asset request failed") from error
    finally:
        second.close()
    completed_at = utc_now()
    package_sha256 = digest.hexdigest()
    if byte_count != pins.package_size:
        raise AttestationError("downloaded CRX size differs from the exact pin")
    if package_sha256 != pins.package_sha256:
        raise AttestationError("downloaded CRX SHA-256 differs from the exact pin")
    return {
        "startedAt": started_at,
        "completedAt": completed_at,
        "method": "GET",
        "credentialsMode": "omit",
        "cacheMode": "no-store",
        "startUrl": pins.package_url,
        "finalUrl": final_url,
        "redirectCount": 1,
        "responses": [
            {"url": pins.package_url, "status": 302, "method": "GET"},
            {"url": final_url, "status": 200, "method": "GET"},
        ],
        "downloadedBytes": byte_count,
        "sha256": package_sha256,
    }
