"""Deterministic binary payloads and byte-range parsing for E2E fixtures."""

from __future__ import annotations

import base64
import hashlib
import io
import re
import zipfile
from pathlib import Path
from typing import Optional, Tuple


DOWNLOAD_BYTES = bytes(
    (index * 37 + 11) % 256 for index in range(2 * 1024 * 1024)
)
DOWNLOAD_SHA256 = hashlib.sha256(DOWNLOAD_BYTES).hexdigest()
ASSET_CHUNK_BYTES = 64 * 1024
LARGE_ZIP_THROTTLE_SECONDS = 0.015
DISCONNECT_AFTER_BYTES = 384 * 1024
WARNING_BYTES = (
    b"AhoiBrowser harmless dangerous-download warning fixture.\n"
    b"This file is plain text, contains no executable program, no macro, and no malware.\n"
)
WARNING_SHA256 = hashlib.sha256(WARNING_BYTES).hexdigest()
MEDIA_SHA256 = "c195edb6dee6e3465fb5fd5fa0a0b7f3fbbd8ac48d7c953ae7108cff777f5436"


def _build_pdf() -> bytes:
    """Return a small valid PDF without timestamps or generated metadata."""

    content = (
        b"BT /F1 18 Tf 72 742 Td (AhoiBrowser deterministic PDF fixture) Tj "
        b"0 -28 Td /F1 11 Tf (Local synthetic document - no external content.) Tj ET\n"
    )
    objects = (
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    )
    result = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number, value in enumerate(objects, start=1):
        offsets.append(len(result))
        result.extend(b"%d 0 obj\n" % number)
        result.extend(value)
        result.extend(b"\nendobj\n")
    xref_offset = len(result)
    result.extend(b"xref\n0 %d\n" % (len(objects) + 1))
    result.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        result.extend(b"%010d 00000 n \n" % offset)
    result.extend(
        b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n"
        % (len(objects) + 1, xref_offset)
    )
    return bytes(result)


def _build_large_zip() -> bytes:
    """Return a reproducible stored ZIP large enough for resume testing."""

    payload_size = 12 * 1024 * 1024
    byte_period = bytes((index * 29 + 17) % 256 for index in range(256))
    payload = byte_period * (payload_size // len(byte_period))
    output = io.BytesIO()
    entry = zipfile.ZipInfo("payload/deterministic.bin", (2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_STORED
    entry.create_system = 3
    entry.external_attr = 0o100644 << 16
    with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
        archive.writestr(entry, payload)
    return output.getvalue()


def _load_media() -> bytes:
    encoded = (
        Path(__file__).parent / "assets" / "h264-aac.mp4.b64"
    ).read_text(encoding="ascii")
    decoded = base64.b64decode(encoded, validate=False)
    if hashlib.sha256(decoded).hexdigest() != MEDIA_SHA256:
        raise RuntimeError("committed H.264/AAC fixture asset hash mismatch")
    return decoded


PDF_BYTES = _build_pdf()
PDF_SHA256 = hashlib.sha256(PDF_BYTES).hexdigest()
LARGE_ZIP_BYTES = _build_large_zip()
LARGE_ZIP_SHA256 = hashlib.sha256(LARGE_ZIP_BYTES).hexdigest()
MEDIA_BYTES = _load_media()


def parse_range(value: str, size: int) -> Optional[Tuple[int, int]]:
    """Parse one HTTP byte range and reject unsupported or invalid forms."""

    if not value:
        return None
    match = re.fullmatch(r"bytes=(\d*)-(\d*)", value.strip())
    if not match:
        raise ValueError("only one byte range is supported")
    start_text, end_text = match.groups()
    if not start_text and not end_text:
        raise ValueError("empty range")
    if not start_text:
        suffix = int(end_text)
        if suffix <= 0:
            raise ValueError("invalid suffix range")
        start = max(0, size - suffix)
        end = size - 1
    else:
        start = int(start_text)
        end = int(end_text) if end_text else size - 1
    if start >= size or end < start:
        raise ValueError("unsatisfiable range")
    return start, min(end, size - 1)
