"""Object fetching for the Chromium checkout hydration.

Result types, limits, the missing-object check and the two fetch
strategies (adaptive ``git fetch`` batches from the verified promisor and
exact Gitiles blobs), split from chromium_checkout_hydration.py (source
line budget).
"""

from __future__ import annotations

import collections
import math
import os
import pathlib
import re
import signal
import subprocess
import time
import concurrent.futures
from dataclasses import dataclass, field
from typing import Callable, Mapping, Sequence

from chromium_checkout_state import (
    CheckoutHydrationError,
    GitRunner,
    clean_detail as _clean_detail,
)
from chromium_roll_hydration import (
    HydrationError,
    _decode_verified,
    validate_git_path,
    validate_limits,
)

SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
MAX_BATCH_SIZE = 128
MAX_ATTEMPTS = 6
MAX_FETCH_TIMEOUT_SECONDS = 1800
MAX_FETCH_COMMANDS = 100_000
MAX_BLOBS = 4_000_000
MAX_GITILES_JOBS = 8


@dataclass(frozen=True)
class FetchResult:
    success: bool
    detail: str = ""


@dataclass
class FetchStatistics:
    command_count: int = 0
    successful_commands: int = 0
    failed_commands: int = 0
    retry_count: int = 0
    adaptive_splits: int = 0
    completed_top_level_batches: int = 0
    command_budget_exhausted: bool = False
    response_budget_exhausted: bool = False
    interrupted: bool = False
    hydrated: set[str] = field(default_factory=set)
    singleton_failures: dict[str, str] = field(default_factory=dict)
    response_bytes: int = 0
    decoded_bytes: int = 0


FetchRunner = Callable[[Sequence[str]], FetchResult]
MissingChecker = Callable[[Sequence[str]], tuple[str, ...]]
ProgressCallback = Callable[[FetchStatistics], None]
GitilesResponseLoader = Callable[[str, str, str, int, int], bytes]


def _require_sha1(value: str, label: str) -> str:
    if SHA1_RE.fullmatch(value) is None:
        raise CheckoutHydrationError(
            f"{label} must be an exact lowercase 40-character SHA-1"
        )
    return value


def _safe_int(value: int, minimum: int, maximum: int, label: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or value > maximum
    ):
        raise CheckoutHydrationError(
            f"{label} must be between {minimum} and {maximum}"
        )
    return value


def _missing_objects(git: GitRunner, object_ids: Sequence[str]) -> tuple[str, ...]:
    missing: list[str] = []
    for offset in range(0, len(object_ids), 8192):
        batch = tuple(object_ids[offset : offset + 8192])
        if not batch:
            continue
        payload = "".join(f"{oid}\n" for oid in batch).encode("ascii")
        raw = git(
            ("cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)"),
            payload,
            True,
        ).stdout
        try:
            lines = raw.decode("ascii", "strict").splitlines()
        except UnicodeDecodeError as error:
            raise CheckoutHydrationError(
                "git cat-file returned malformed inventory data"
            ) from error
        if len(lines) != len(batch):
            raise CheckoutHydrationError(
                "git cat-file returned an incomplete object inventory"
            )
        for expected, line in zip(batch, lines, strict=True):
            fields = line.split()
            if fields == [expected, "missing"]:
                missing.append(expected)
                continue
            if len(fields) != 3 or fields[0] != expected or fields[1] != "blob":
                raise CheckoutHydrationError(
                    "target blob inventory contains an unexpected object"
                )
            try:
                size = int(fields[2])
            except ValueError as error:
                raise CheckoutHydrationError(
                    "git cat-file returned an invalid blob size"
                ) from error
            if size < 0:
                raise CheckoutHydrationError(
                    "git cat-file returned an invalid blob size"
                )
    return tuple(missing)


def fetch_command() -> tuple[str, ...]:
    """Return the fixed, reviewable transport command used for every batch."""

    return (
        "git",
        "-c",
        "http.version=HTTP/1.1",
        "-c",
        "http.maxRequests=1",
        "-c",
        "fetch.parallel=1",
        "-c",
        "fetch.negotiationAlgorithm=noop",
        "-c",
        "maintenance.auto=false",
        "-c",
        "gc.auto=0",
        "fetch",
        "--no-tags",
        "--no-write-fetch-head",
        "--no-recurse-submodules",
        "--filter=blob:none",
        "origin",
        "--stdin",
    )


def _fetch_object_ids(
    checkout: pathlib.Path,
    environment: Mapping[str, str],
    object_ids: Sequence[str],
    timeout_seconds: int,
) -> FetchResult:
    if not object_ids:
        raise CheckoutHydrationError("refusing an empty object fetch")
    for oid in object_ids:
        _require_sha1(oid, "fetch object")
    payload = "".join(f"{oid}\n" for oid in object_ids).encode("ascii")
    process = subprocess.Popen(
        fetch_command(),
        cwd=checkout,
        env=dict(environment),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(payload, timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
        detail = _clean_detail(stderr or stdout)
        suffix = f": {detail}" if detail else ""
        return FetchResult(False, f"fetch timed out after {timeout_seconds}s{suffix}")
    detail = _clean_detail(stderr or stdout)
    if process.returncode != 0:
        suffix = f": {detail}" if detail else ""
        return FetchResult(False, f"fetch exited {process.returncode}{suffix}")
    return FetchResult(True, detail)


def _hydrate_gitiles_blobs(
    *,
    git: GitRunner,
    target: str,
    object_ids: Sequence[str],
    blob_paths: Mapping[str, str],
    jobs: int,
    max_requests: int,
    network_timeout: int,
    total_timeout: int,
    max_response_bytes: int,
    max_total_response_bytes: int,
    load_response: GitilesResponseLoader,
    progress: ProgressCallback | None = None,
) -> FetchStatistics:
    """Download, verify, and promote missing blobs from exact Gitiles paths."""

    _safe_int(jobs, 1, MAX_GITILES_JOBS, "jobs")
    _safe_int(max_requests, 1, MAX_FETCH_COMMANDS, "max-fetch-commands")
    try:
        validate_limits(
            network_timeout,
            total_timeout,
            max_response_bytes,
            max_total_response_bytes,
        )
    except HydrationError as error:
        raise CheckoutHydrationError(str(error)) from error

    pending = collections.deque(
        _require_sha1(oid, "Gitiles object ID")
        for oid in sorted(set(object_ids))
    )
    for oid in pending:
        if oid not in blob_paths:
            raise CheckoutHydrationError(
                f"target inventory has no Gitiles path for blob {oid}"
            )
        try:
            validate_git_path(blob_paths[oid])
        except HydrationError as error:
            raise CheckoutHydrationError(str(error)) from error

    statistics = FetchStatistics()
    deadline = time.monotonic() + total_timeout

    def fetch_one(
        oid: str, maximum: int, timeout: int
    ) -> tuple[str, bytes, int]:
        path = blob_paths[oid]
        raw = load_response(target, path, oid, timeout, maximum)
        if not isinstance(raw, bytes):
            raise HydrationError("Gitiles blob response is not bytes")
        if len(raw) > maximum:
            raise HydrationError(
                "Gitiles blob response exceeds the per-response limit"
            )
        content = _decode_verified(raw, oid)
        return oid, content, len(raw)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        while pending:
            if statistics.command_count >= max_requests:
                statistics.command_budget_exhausted = True
                break
            remaining_total = (
                max_total_response_bytes - statistics.response_bytes
            )
            if remaining_total < 1:
                statistics.response_budget_exhausted = True
                break
            remaining_time = deadline - time.monotonic()
            if remaining_time <= 0:
                raise CheckoutHydrationError(
                    "Gitiles hydration exceeded the total-timeout deadline"
                )
            effective_timeout = min(
                network_timeout, max(1, int(remaining_time + 0.999))
            )
            wave: list[tuple[str, int]] = []
            available = remaining_total
            available_requests = max_requests - statistics.command_count
            while (
                pending
                and len(wave) < jobs
                and len(wave) < available_requests
                and available > 0
            ):
                oid = pending.popleft()
                maximum = min(max_response_bytes, available)
                wave.append((oid, maximum))
                available -= maximum

            futures = {
                executor.submit(fetch_one, oid, maximum, effective_timeout): oid
                for oid, maximum in wave
            }
            statistics.command_count += len(futures)
            verified: list[tuple[str, bytes]] = []
            for future in concurrent.futures.as_completed(futures):
                oid = futures[future]
                try:
                    _, content, response_bytes = future.result()
                except HydrationError as error:
                    statistics.failed_commands += 1
                    statistics.singleton_failures[oid] = _clean_detail(str(error))
                    continue
                statistics.successful_commands += 1
                statistics.response_bytes += response_bytes
                statistics.decoded_bytes += len(content)
                if statistics.response_bytes > max_total_response_bytes:
                    raise CheckoutHydrationError(
                        "Gitiles blob responses exceed the aggregate response limit"
                    )
                verified.append((oid, content))

            for oid, content in sorted(verified):
                written = git(("hash-object", "-w", "--stdin"), content, True).stdout
                try:
                    actual = written.decode("ascii", "strict").strip()
                except UnicodeDecodeError as error:
                    raise CheckoutHydrationError(
                        "git hash-object returned invalid output"
                    ) from error
                if actual != oid:
                    raise CheckoutHydrationError(
                        "git hash-object did not write the verified Gitiles blob"
                    )
                if _missing_objects(git, (oid,)):
                    raise CheckoutHydrationError(
                        "verified Gitiles blob promotion could not be confirmed"
                    )
                statistics.hydrated.add(oid)

            statistics.completed_top_level_batches += 1
            if progress is not None:
                progress(statistics)

    return statistics


def fetch_adaptively(
    object_ids: Sequence[str],
    *,
    batch_size: int,
    attempts: int,
    max_fetch_commands: int,
    fetch: FetchRunner,
    missing: MissingChecker,
    retry_backoff_seconds: float = 1.0,
    sleeper: Callable[[float], None] = time.sleep,
    progress: ProgressCallback | None = None,
) -> FetchStatistics:
    """Fetch bounded batches, retry them, then bisect only persistent failures."""

    _safe_int(batch_size, 1, MAX_BATCH_SIZE, "batch-size")
    _safe_int(attempts, 1, MAX_ATTEMPTS, "attempts")
    _safe_int(max_fetch_commands, 1, MAX_FETCH_COMMANDS, "max-fetch-commands")
    if (
        not math.isfinite(retry_backoff_seconds)
        or retry_backoff_seconds < 0
        or retry_backoff_seconds > 30
    ):
        raise CheckoutHydrationError(
            "retry-backoff-seconds must be between 0 and 30"
        )
    ordered = tuple(sorted({_require_sha1(oid, "object ID") for oid in object_ids}))
    statistics = FetchStatistics()

    def process(group: tuple[str, ...]) -> None:
        current = tuple(missing(group))
        if not current:
            statistics.hydrated.update(group)
            return
        last_detail = "object remained missing after fetch"
        for attempt_index in range(attempts):
            if statistics.command_count >= max_fetch_commands:
                statistics.command_budget_exhausted = True
                return
            if attempt_index:
                statistics.retry_count += 1
                if retry_backoff_seconds:
                    sleeper(min(30.0, retry_backoff_seconds * (2 ** (attempt_index - 1))))
            result = fetch(current)
            statistics.command_count += 1
            if result.success:
                statistics.successful_commands += 1
            else:
                statistics.failed_commands += 1
            remaining = tuple(missing(current))
            statistics.hydrated.update(set(current) - set(remaining))
            if not remaining:
                return
            current = remaining
            last_detail = result.detail or "object remained missing after fetch"
        if len(current) == 1:
            statistics.singleton_failures[current[0]] = _clean_detail(last_detail)
            return
        statistics.adaptive_splits += 1
        midpoint = len(current) // 2
        process(current[:midpoint])
        if not statistics.command_budget_exhausted:
            process(current[midpoint:])

    for offset in range(0, len(ordered), batch_size):
        if statistics.command_budget_exhausted:
            break
        process(ordered[offset : offset + batch_size])
        statistics.completed_top_level_batches += 1
        if progress is not None:
            progress(statistics)
    return statistics
