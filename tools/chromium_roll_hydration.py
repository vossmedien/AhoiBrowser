#!/usr/bin/env python3
"""Bounded, verified Gitiles blob hydration for Chromium roll preflights."""

from __future__ import annotations

import base64
import binascii
import hashlib
import pathlib
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Sequence


GITILES_BASE = "https://chromium.googlesource.com/chromium/src"
DEFAULT_MAX_RESPONSE_BYTES = 8 * 1024 * 1024
DEFAULT_MAX_TOTAL_RESPONSE_BYTES = 64 * 1024 * 1024
MAX_RESPONSE_BYTES = 32 * 1024 * 1024
MAX_TOTAL_RESPONSE_BYTES = 256 * 1024 * 1024
MAX_TARGET_PATHS = 4096
MAX_BLOB_REQUESTS = 2048
MAX_TOTAL_TIMEOUT_SECONDS = 3600
NETWORK_ATTEMPTS = 4
PROMOTION_BATCH_SIZE = 16
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")


class HydrationError(ValueError):
    """A target blob could not be hydrated without weakening safety bounds."""


class _RejectRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        del req, fp, code, msg, headers, newurl
        return None


GitCall = Callable[[Sequence[str], bytes | None, bool], bytes]
ResponseLoader = Callable[[str, str, str, int, int], bytes]


def patch_stack_report(entries: Sequence[tuple[str, bytes]]) -> dict[str, Any]:
    fingerprint = hashlib.sha256(b"ahoi-patch-stack-v1\0")
    patches: list[dict[str, Any]] = []
    for name, payload in entries:
        digest = hashlib.sha256(payload).hexdigest()
        fingerprint.update(name.encode("utf-8") + b"\0" + bytes.fromhex(digest))
        patches.append(
            {"path": name, "sha256": digest, "bytes": len(payload)}
        )
    return {"patches": patches, "sha256": fingerprint.hexdigest()}


def validate_git_path(path: str) -> str:
    if not isinstance(path, str) or not path:
        raise HydrationError("hydration path is empty or too long")
    try:
        encoded_path = path.encode("utf-8", "strict")
    except UnicodeEncodeError as error:
        raise HydrationError("hydration path is not valid UTF-8") from error
    if len(encoded_path) > 4096:
        raise HydrationError("hydration path is empty or too long")
    if path.startswith("/") or path.endswith("/") or "\0" in path:
        raise HydrationError(f"unsafe hydration path: {path!r}")
    if any(part in {"", ".", ".."} for part in path.split("/")):
        raise HydrationError(f"unsafe hydration path: {path!r}")
    if any(ord(character) < 32 or ord(character) == 127 for character in path):
        raise HydrationError(f"unsafe hydration path: {path!r}")
    return path


def gitiles_blob_url(target: str, path: str) -> str:
    if SHA1_RE.fullmatch(target) is None:
        raise HydrationError("Gitiles hydration target must be an exact SHA-1")
    validated = validate_git_path(path)
    encoded = "/".join(
        urllib.parse.quote(part, safe="", encoding="utf-8", errors="strict")
        for part in validated.split("/")
    )
    return f"{GITILES_BASE}/+/{target}/{encoded}?format=TEXT"


def validate_gitiles_blob_url(url: str) -> None:
    parsed = urllib.parse.urlparse(url)
    prefix = "/chromium/src/+/"
    if (
        parsed.scheme != "https"
        or parsed.hostname != "chromium.googlesource.com"
        or parsed.username is not None
        or parsed.password is not None
        or parsed.port is not None
        or parsed.fragment
        or not parsed.path.startswith(prefix)
        or urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
        != [("format", "TEXT")]
    ):
        raise HydrationError(f"refusing non-official Gitiles blob URL: {url}")
    target, separator, encoded_path = parsed.path[len(prefix) :].partition("/")
    try:
        decoded_path = urllib.parse.unquote(
            encoded_path, encoding="utf-8", errors="strict"
        )
        canonical = gitiles_blob_url(target, decoded_path)
    except (HydrationError, UnicodeDecodeError) as error:
        raise HydrationError(f"refusing unsafe Gitiles blob URL: {url}") from error
    if separator != "/" or canonical != url:
        raise HydrationError(f"refusing unsafe Gitiles blob URL: {url}")


def validate_limits(timeout: int, total_timeout: int, maximum: int, aggregate: int) -> None:
    if timeout < 1 or timeout > 60:
        raise HydrationError("network-timeout must be between 1 and 60 seconds")
    if maximum < 1 or maximum > MAX_RESPONSE_BYTES:
        raise HydrationError("max-response-bytes is outside the 1..32 MiB bound")
    if aggregate < maximum or aggregate > MAX_TOTAL_RESPONSE_BYTES:
        raise HydrationError(
            "max-total-response-bytes must be at least the per-response limit "
            "and at most 256 MiB"
        )
    if total_timeout < timeout or total_timeout > MAX_TOTAL_TIMEOUT_SECONDS:
        raise HydrationError(
            "total-timeout must be at least network-timeout and at most 3600 seconds"
        )


def fetch_gitiles_response(
    url: str,
    timeout: int,
    maximum: int,
    *,
    opener: Any | None = None,
) -> bytes:
    validate_gitiles_blob_url(url)
    if timeout < 1 or timeout > 60:
        raise HydrationError("network-timeout must be between 1 and 60 seconds")
    if maximum < 1 or maximum > MAX_RESPONSE_BYTES:
        raise HydrationError("Gitiles response limit is outside the safe bound")
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "AhoiBrowser-Chromium-Roll/1",
            "Accept-Encoding": "identity",
            "Connection": "close",
        },
    )
    client = opener or urllib.request.build_opener(_RejectRedirects())
    deadline = time.monotonic() + timeout
    for attempt in range(NETWORK_ATTEMPTS):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise HydrationError("official Gitiles blob request exceeded its deadline")
        socket_timeout = min(60, max(1, int(remaining + 0.999)))
        try:
            with client.open(request, timeout=socket_timeout) as response:
                if response.geturl() != url:
                    raise HydrationError("Gitiles blob request was redirected")
                status = response.getcode()
                if status is not None and status != 200:
                    raise HydrationError(
                        f"Gitiles blob request returned HTTP {status}"
                    )
                raw = response.read(maximum + 1)
        except HydrationError:
            raise
        except urllib.error.HTTPError as error:
            retryable = error.code == 429 or 500 <= error.code <= 504
            retry_after = error.headers.get("Retry-After") if error.headers else None
            error.close()
            if not retryable or attempt + 1 == NETWORK_ATTEMPTS:
                raise HydrationError(
                    f"official Gitiles blob request failed: {error}"
                ) from error
            try:
                delay = int(retry_after) if retry_after is not None else 2**attempt
            except ValueError:
                delay = 2**attempt
            delay = min(30, max(1, delay))
            if delay >= deadline - time.monotonic():
                raise HydrationError(
                    "official Gitiles retry would exceed the request deadline"
                ) from error
            time.sleep(delay)
            continue
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise HydrationError(
                f"official Gitiles blob request failed: {error}"
            ) from error
        if time.monotonic() > deadline:
            raise HydrationError("official Gitiles blob request exceeded its deadline")
        if len(raw) > maximum:
            raise HydrationError("Gitiles blob response exceeds the per-response limit")
        return raw
    raise HydrationError("official Gitiles blob request exhausted its retry budget")


def read_fixture_response(directory: pathlib.Path, oid: str, maximum: int) -> bytes:
    if directory.is_symlink() or not directory.is_dir():
        raise HydrationError("offline response directory is missing or unsafe")
    path = directory / f"{oid}.b64"
    if path.is_symlink() or not path.is_file():
        raise HydrationError(f"offline response is missing for blob {oid}")
    try:
        with path.open("rb") as handle:
            raw = handle.read(maximum + 1)
    except OSError as error:
        raise HydrationError(f"could not read offline blob response: {error}") from error
    if len(raw) > maximum:
        raise HydrationError("offline blob response exceeds the per-response limit")
    return raw


def _blob_oid(content: bytes) -> str:
    framed = b"blob " + str(len(content)).encode("ascii") + b"\0" + content
    return hashlib.sha1(framed, usedforsecurity=False).hexdigest()


def _decode_verified(raw: bytes, expected: str) -> bytes:
    try:
        content = base64.b64decode(raw, validate=True)
    except (binascii.Error, ValueError) as error:
        raise HydrationError("Gitiles blob response is not strict base64") from error
    actual = _blob_oid(content)
    if actual != expected:
        raise HydrationError(
            f"Gitiles blob hash mismatch: expected {expected}, received {actual}"
        )
    return content


def _target_entries(
    git: GitCall, target: str, paths: Sequence[str]
) -> dict[str, tuple[str, str]]:
    entries: dict[str, tuple[str, str]] = {}
    batches: list[list[str]] = []
    for path in paths:
        for batch in batches:
            if all(
                not (path.startswith(f"{other}/") or other.startswith(f"{path}/"))
                for other in batch
            ):
                batch.append(path)
                break
        else:
            batches.append([path])
    for batch in batches:
        for offset in range(0, len(batch), 128):
            chunk = batch[offset : offset + 128]
            raw = git(
                ("ls-tree", "-z", "--full-tree", target, "--", *chunk),
                None,
                True,
            )
            for record in raw.split(b"\0"):
                if not record:
                    continue
                try:
                    metadata, raw_path = record.split(b"\t", 1)
                    raw_mode, raw_kind, raw_oid = metadata.split(b" ", 2)
                    path = raw_path.decode("utf-8", "strict")
                    oid = raw_oid.decode("ascii", "strict")
                    mode = raw_mode.decode("ascii", "strict")
                    kind = raw_kind.decode("ascii", "strict")
                except (UnicodeDecodeError, ValueError) as error:
                    raise HydrationError("git ls-tree returned malformed data") from error
                if path not in chunk or SHA1_RE.fullmatch(oid) is None:
                    raise HydrationError("git ls-tree returned an unexpected entry")
                expected_kind = {
                    "040000": "tree",
                    "100644": "blob",
                    "100755": "blob",
                    "120000": "blob",
                    "160000": "commit",
                }.get(mode)
                if expected_kind != kind:
                    raise HydrationError("git ls-tree returned an invalid mode/type pair")
                if path in entries:
                    raise HydrationError(f"target tree returned duplicate path: {path}")
                entries[path] = (kind, oid)
    return entries


def _present_blobs(
    git: GitCall, object_ids: Sequence[str]
) -> dict[str, tuple[bool, int | None]]:
    if not object_ids:
        return {}
    raw = git(
        ("cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)"),
        "".join(f"{oid}\n" for oid in object_ids).encode("ascii"),
        True,
    )
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as error:
        raise HydrationError("git cat-file returned malformed data") from error
    if len(lines) != len(object_ids):
        raise HydrationError("git cat-file returned an incomplete object inventory")
    states: dict[str, tuple[bool, int | None]] = {}
    for expected, line in zip(object_ids, lines, strict=True):
        fields = line.split()
        if fields == [expected, "missing"]:
            states[expected] = (False, None)
        elif len(fields) == 3 and fields[0] == expected and fields[1] == "blob":
            try:
                states[expected] = (True, int(fields[2]))
            except ValueError as error:
                raise HydrationError("git cat-file returned an invalid blob size") from error
        else:
            raise HydrationError("target object inventory contains a non-blob object")
    return states


def hydrate_target_blobs(
    *,
    git: GitCall,
    target: str,
    touched_paths: Sequence[str],
    load_response: ResponseLoader,
    timeout: int,
    total_timeout: int,
    max_response_bytes: int,
    max_total_response_bytes: int,
) -> dict[str, Any]:
    validate_limits(
        timeout, total_timeout, max_response_bytes, max_total_response_bytes
    )
    paths = sorted({validate_git_path(path) for path in touched_paths})
    if len(paths) > MAX_TARGET_PATHS:
        raise HydrationError(
            f"hydration target exceeds the {MAX_TARGET_PATHS}-path safety bound"
        )
    entries = _target_entries(git, target, paths)
    blob_ids = sorted({oid for kind, oid in entries.values() if kind == "blob"})
    states = _present_blobs(git, blob_ids)
    missing = [oid for oid in blob_ids if not states[oid][0]]
    if len(missing) > MAX_BLOB_REQUESTS:
        raise HydrationError(
            f"hydration target exceeds the {MAX_BLOB_REQUESTS}-request safety bound"
        )
    source_path = {
        oid: min(path for path, value in entries.items() if value == ("blob", oid))
        for oid in missing
    }

    hydrated_sizes: dict[str, int] = {}
    response_total = 0
    decoded_total = 0
    deadline = time.monotonic() + total_timeout
    for offset in range(0, len(missing), PROMOTION_BATCH_SIZE):
        batch = missing[offset : offset + PROMOTION_BATCH_SIZE]
        staged: list[tuple[str, bytes]] = []
        for oid in batch:
            path = source_path[oid]
            remaining = max_total_response_bytes - response_total
            if remaining < 1:
                raise HydrationError("blob responses exceed the aggregate response limit")
            effective_maximum = min(max_response_bytes, remaining)
            remaining_time = deadline - time.monotonic()
            if remaining_time <= 0:
                raise HydrationError("hydration exceeded the total-timeout deadline")
            effective_timeout = min(timeout, max(1, int(remaining_time + 0.999)))
            try:
                raw = load_response(
                    target, path, oid, effective_timeout, effective_maximum
                )
            except HydrationError as error:
                if (
                    effective_maximum < max_response_bytes
                    and "exceeds the per-response limit" in str(error)
                ):
                    error = HydrationError(
                        "blob responses exceed the aggregate response limit"
                    )
                if hydrated_sizes:
                    raise HydrationError(
                        f"{error}; {len(hydrated_sizes)} verified blob(s) already "
                        "promoted, rerun safely resumes"
                    ) from error
                raise error
            if time.monotonic() > deadline:
                raise HydrationError("hydration exceeded the total-timeout deadline")
            response_total += len(raw)
            if response_total > max_total_response_bytes:
                raise HydrationError("blob responses exceed the aggregate response limit")
            content = _decode_verified(raw, oid)
            decoded_total += len(content)
            staged.append((oid, content))

        for oid, content in staged:
            try:
                written = git(("hash-object", "-w", "--stdin"), content, True)
            except Exception as error:
                raise HydrationError(
                    "object promotion failed after "
                    f"{len(hydrated_sizes)} verified blob(s); rerun safely resumes"
                ) from error
            try:
                actual = written.decode("ascii", "strict").strip()
            except UnicodeDecodeError as error:
                raise HydrationError("git hash-object returned invalid output") from error
            if actual != oid:
                raise HydrationError("git hash-object did not write the verified blob")
            hydrated_sizes[oid] = len(content)
        promoted = _present_blobs(git, batch)
        for oid, content in staged:
            if promoted.get(oid) != (True, len(content)):
                raise HydrationError(
                    "verified blob promotion could not be confirmed; rerun safely resumes"
                )

    path_reports: list[dict[str, Any]] = []
    for path in paths:
        entry = entries.get(path)
        if entry is None:
            path_reports.append({"path": path, "disposition": "absent_from_target"})
            continue
        kind, oid = entry
        if kind != "blob":
            path_reports.append(
                {
                    "path": path,
                    "targetType": kind,
                    "objectId": oid,
                    "disposition": "non_blob",
                }
            )
            continue
        present, size = states[oid]
        path_reports.append(
            {
                "path": path,
                "targetType": "blob",
                "objectId": oid,
                "bytes": size if present else hydrated_sizes[oid],
                "disposition": "already_present" if present else "hydrated",
            }
        )

    return {
        "paths": path_reports,
        "summary": {
            "touchedPathCount": len(paths),
            "targetBlobCount": len(blob_ids),
            "absentFromTargetCount": sum(
                entries.get(path) is None for path in paths
            ),
            "alreadyPresentBlobCount": len(blob_ids) - len(missing),
            "requestedBlobCount": len(missing),
            "hydratedBlobCount": len(hydrated_sizes),
            "responseBytes": response_total,
            "decodedBytes": decoded_total,
            "promotionBatchSize": PROMOTION_BATCH_SIZE,
            "writeModel": "validated_resumable_immutable_object_batches",
        },
    }
