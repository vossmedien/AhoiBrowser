#!/usr/bin/env python3
"""Bounded, exact-path LaunchServices operations for the protocol fixture."""

from __future__ import annotations

import os
import re
import subprocess
import time
from pathlib import Path
from typing import Callable, FrozenSet, Sequence


BUNDLE_ID = "app.ahoibrowser.fixture.custom-protocol"
SCHEME = "ahoi-e2e-safe"
COMMAND_TIMEOUT_SECONDS = 8.0
MAX_COMMAND_OUTPUT_BYTES = 4 * 1024 * 1024
SETTLE_ATTEMPTS = 40
SETTLE_INTERVAL_SECONDS = 0.05
LSREGISTER_CANDIDATES = (
    Path(
        "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
        "LaunchServices.framework/Support/lsregister"
    ),
    Path(
        "/System/Library/Frameworks/CoreServices.framework/Support/lsregister"
    ),
)


class LaunchServicesError(RuntimeError):
    """LaunchServices state cannot be proven safe for mutation."""


def _output_size(result: subprocess.CompletedProcess[str]) -> int:
    return len((result.stdout or "").encode("utf-8", "replace")) + len(
        (result.stderr or "").encode("utf-8", "replace")
    )


def run_result(
    command: Sequence[str],
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> subprocess.CompletedProcess[str]:
    try:
        result = runner(
            list(command),
            check=True,
            capture_output=True,
            text=True,
            timeout=COMMAND_TIMEOUT_SECONDS,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise LaunchServicesError("required macOS command failed") from error
    except subprocess.TimeoutExpired as error:
        raise LaunchServicesError("required macOS command timed out") from error
    if _output_size(result) > MAX_COMMAND_OUTPUT_BYTES:
        raise LaunchServicesError("required macOS command output exceeded its bound")
    return result


def run(
    command: Sequence[str],
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> None:
    run_result(command, runner=runner)


def lsregister() -> Path:
    for candidate in LSREGISTER_CANDIDATES:
        if candidate.is_file():
            return candidate
    raise LaunchServicesError("macOS lsregister is unavailable")


def _scheme_pattern(scheme: str) -> re.Pattern[str]:
    return re.compile(
        r"(?<![A-Za-z0-9+.-])" + re.escape(scheme) + r":?(?![A-Za-z0-9+.-])",
        re.IGNORECASE,
    )


def parse_registered_handler_paths(
    dump: str,
    *,
    bundle_id: str = BUNDLE_ID,
    scheme: str = SCHEME,
) -> FrozenSet[str]:
    if len(dump.encode("utf-8", "replace")) > MAX_COMMAND_OUTPUT_BYTES:
        raise LaunchServicesError("LaunchServices registry output exceeded its bound")
    records = re.split(r"(?m)^\s*-{20,}\s*$", dump)
    identifier_pattern = re.compile(
        r"(?im)^\s*(?:bundle\s+id|bundle\s+identifier|identifier)\s*:\s*"
        + re.escape(bundle_id)
        + r"\s*$"
    )
    scheme_pattern = _scheme_pattern(scheme)
    claimed_paths = set()
    for record in records:
        claims_identifier = identifier_pattern.search(record) is not None
        claims_scheme = False
        binding_indent = None
        paths = []
        for line in record.splitlines():
            if not line.strip():
                continue
            indentation = len(line) - len(line.lstrip(" \t"))
            if binding_indent is not None and indentation > binding_indent:
                if scheme_pattern.search(line):
                    claims_scheme = True
                continue
            binding_indent = None
            match = re.match(r"^\s*([^:\n]+):\s*(.*)$", line)
            if match is None:
                continue
            key, value = match.groups()
            normalized_key = re.sub(r"[^a-z]", "", key.lower())
            if "binding" in normalized_key or "scheme" in normalized_key:
                binding_indent = indentation
                if scheme_pattern.search(value):
                    claims_scheme = True
            if normalized_key not in ("path", "bundlepath", "originpath"):
                continue
            candidate = value.strip()
            if (
                len(candidate) >= 2
                and candidate[0] == candidate[-1]
                and candidate[0] in ('"', "'")
            ):
                candidate = candidate[1:-1]
            if (
                not candidate
                or "\x00" in candidate
                or not os.path.isabs(candidate)
                or candidate != os.path.normpath(candidate)
            ):
                raise LaunchServicesError(
                    "LaunchServices returned an unsafe or non-canonical handler path"
                )
            paths.append(candidate)
        if not claims_identifier and not claims_scheme:
            continue
        if not paths:
            raise LaunchServicesError(
                "LaunchServices returned a handler claim without an exact path"
            )
        claimed_paths.update(paths)
    return frozenset(claimed_paths)


def registered_handler_paths(
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> FrozenSet[str]:
    try:
        result = run_result([str(lsregister()), "-dump"], runner=runner)
        return parse_registered_handler_paths(
            (result.stdout or "") + "\n" + (result.stderr or "")
        )
    except LaunchServicesError as error:
        raise LaunchServicesError(
            "LaunchServices registry cannot be inspected safely"
        ) from error


def expected_path(app_path: Path) -> str:
    return os.path.normpath(os.path.abspath(app_path))


def assert_registration_scope(paths: FrozenSet[str], app_path: Path) -> None:
    expected = expected_path(app_path)
    if any(path != expected for path in paths):
        raise LaunchServicesError(
            "refusing to modify a foreign custom-protocol registration"
        )


def wait_for_paths(
    expected: FrozenSet[str],
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> None:
    for attempt in range(SETTLE_ATTEMPTS):
        if registered_handler_paths(runner=runner) == expected:
            return
        if attempt + 1 < SETTLE_ATTEMPTS:
            time.sleep(SETTLE_INTERVAL_SECONDS)
    raise LaunchServicesError(
        "LaunchServices did not converge to the exact expected handler path"
    )


def register_exact(
    app_path: Path,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> None:
    run([str(lsregister()), "-f", str(app_path)], runner=runner)
    wait_for_paths(frozenset((expected_path(app_path),)), runner=runner)


def unregister_exact(
    app_path: Path,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> None:
    run([str(lsregister()), "-u", str(app_path)], runner=runner)
    wait_for_paths(frozenset(), runner=runner)


def restore_registration(
    app_path: Path,
    was_registered: bool,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> None:
    current = registered_handler_paths(runner=runner)
    assert_registration_scope(current, app_path)
    expected = frozenset((expected_path(app_path),))
    if was_registered and current != expected:
        register_exact(app_path, runner=runner)
    elif not was_registered and current == expected:
        unregister_exact(app_path, runner=runner)
