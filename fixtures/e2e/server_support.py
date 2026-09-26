"""Constants and request parsers shared by the fixture server and its routes."""

from __future__ import annotations

import json
import re
from typing import Optional, Sequence, Tuple
from urllib.parse import parse_qs, urlsplit


IPV4_LOOPBACK_HOST = "127.0.0.1"
IPV6_LOOPBACK_HOST = "::1"
# Retain the public constant used by existing fixture consumers while binding
# an explicit listener for each loopback address family below.
LOOPBACK_HOST = IPV4_LOOPBACK_HOST
FIRST_HOST_NAME = "first-party.localhost"
THIRD_HOST_NAME = "third-party.localhost"
MEDIA_HOST_NAME = "media.localhost"
MAX_UPLOAD_BYTES = 16 * 1024 * 1024
SYNTHETIC_USERNAME = "fixture-user"
SYNTHETIC_PASSWORD = "fixture-password"
HTTP_RECOVERY_STATUSES = frozenset({"404", "500"})
HTTP_RECOVERY_TOKEN_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
SLOW_RESOURCE_BYTES = (
    b"<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'>"
    + (b" " * (4 * 1024 * 1024))
    + b"</svg>"
)
SLOW_RESOURCE_THROTTLE_SECONDS = 0.15
MOBILE_REAL_E2E_CONTRACT_VERSION = 2
PRIVATE_DATA_MARKER_COOKIE_NAMES = {
    "normal": "ahoi_e2e_normal_marker",
    "private": "ahoi_e2e_private_marker",
}
PRIVATE_DATA_MARKER_COOKIE_VALUE = "synthetic-e2e-marker"


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _safe_filename(value: str) -> str:
    basename = value.replace("\\", "/").rsplit("/", 1)[-1]
    cleaned = re.sub(r"[^A-Za-z0-9._ -]", "_", basename)[:120]
    return cleaned or "unnamed-upload.bin"


def _cookie_names(value: str) -> Sequence[str]:
    return sorted(
        {
            part.split("=", 1)[0].strip()
            for part in value.split(";")
            if "=" in part and part.split("=", 1)[0].strip()
        }
    )


def _safe_referrer(value: str) -> Optional[str]:
    if not value:
        return None
    split = urlsplit(value)
    if split.scheme not in {"http", "https"} or not split.netloc:
        return "present-but-invalid"
    return "%s://%s%s" % (split.scheme, split.netloc, split.path)


def _http_recovery_query(query: str) -> Optional[Tuple[int, str]]:
    try:
        values = parse_qs(
            query,
            keep_blank_values=True,
            strict_parsing=True,
            max_num_fields=2,
        )
    except ValueError:
        return None
    if set(values) != {"status", "token"}:
        return None
    statuses = values["status"]
    tokens = values["token"]
    if len(statuses) != 1 or len(tokens) != 1:
        return None
    status = statuses[0]
    token = tokens[0]
    if status not in HTTP_RECOVERY_STATUSES:
        return None
    if HTTP_RECOVERY_TOKEN_PATTERN.fullmatch(token) is None:
        return None
    return int(status), token


def _private_data_marker_kind(body: bytes) -> Optional[str]:
    try:
        values = parse_qs(
            body.decode("ascii"),
            keep_blank_values=True,
            strict_parsing=True,
            max_num_fields=1,
        )
    except (UnicodeDecodeError, ValueError):
        return None
    if set(values) != {"marker"} or len(values["marker"]) != 1:
        return None
    marker = values["marker"][0]
    return marker if marker in PRIVATE_DATA_MARKER_COOKIE_NAMES else None
