#!/usr/bin/env python3
"""Prehydrate every missing blob for a pinned Chromium checkout target.

The command deliberately does not switch revisions.  It inventories the exact
target tree without lazy fetching and asks the verified ``origin`` promisor for
immutable blob object IDs in small batches.  A later ``gclient sync`` can then
update the worktree without issuing thousands of opportunistic blob requests.

Exit codes:
  0   every target blob is present
  1   validation or runtime error
  2   hydration is incomplete (including a dry run with missing blobs)
  3   protected checkout state changed during the operation
  130 interrupted; verified progress remains resumable in the object store
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import pathlib
import re
import signal
import stat
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Mapping, Sequence

from chromium_checkout_state import (
    CheckoutHydrationError,
    GitRunner,
    changed_guard_fields,
    checkout_snapshot,
    clean_detail as _clean_detail,
    git_environment as _git_environment,
    git_runner as _git_runner,
    git_text as _git_text,
)
from chromium_roll_output import PreparedReportOutput, ReportOutputError
from verify_chromium_pin import VerificationError, validate_config


ROOT = pathlib.Path(__file__).resolve().parents[1]
OFFICIAL_ORIGIN = "https://chromium.googlesource.com/chromium/src.git"
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")

DEFAULT_BATCH_SIZE = 16
DEFAULT_ATTEMPTS = 3
DEFAULT_FETCH_TIMEOUT_SECONDS = 300
DEFAULT_MAX_FETCH_COMMANDS = 4096
DEFAULT_CHECKPOINT_BATCHES = 16
DEFAULT_MAX_BLOBS = 2_000_000

MAX_BATCH_SIZE = 128
MAX_ATTEMPTS = 6
MAX_FETCH_TIMEOUT_SECONDS = 1800
MAX_FETCH_COMMANDS = 100_000
MAX_BLOBS = 4_000_000

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_INCOMPLETE = 2
EXIT_MUTATION = 3
EXIT_INTERRUPTED = 130

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
    interrupted: bool = False
    hydrated: set[str] = field(default_factory=set)
    singleton_failures: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class TargetInventory:
    commit: str
    tree: str
    entry_count: int
    submodule_count: int
    blob_ids: tuple[str, ...]
    missing_blob_ids: tuple[str, ...]
    tree_inventory_sha256: str


FetchRunner = Callable[[Sequence[str]], FetchResult]
MissingChecker = Callable[[Sequence[str]], tuple[str, ...]]
ProgressCallback = Callable[[FetchStatistics], None]


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


def _load_verified_pin(repository: pathlib.Path, target: str) -> dict[str, Any]:
    pin_path = repository / "config/chromium.json"
    try:
        metadata = pin_path.lstat()
    except FileNotFoundError as error:
        raise CheckoutHydrationError("config/chromium.json is missing") from error
    if not stat.S_ISREG(metadata.st_mode) or pin_path.is_symlink():
        raise CheckoutHydrationError("config/chromium.json is not a regular file")
    try:
        payload = pin_path.read_bytes()
        parsed = json.loads(payload.decode("utf-8", "strict"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CheckoutHydrationError(f"could not read Chromium pin: {error}") from error
    if not isinstance(parsed, dict):
        raise CheckoutHydrationError("config/chromium.json must contain an object")
    try:
        validate_config(parsed)
    except VerificationError as error:
        raise CheckoutHydrationError(f"invalid Chromium pin: {error}") from error
    if parsed["commit"] != target:
        raise CheckoutHydrationError(
            "target does not match the pinned config/chromium.json commit"
        )
    return {
        "verified": True,
        "path": "config/chromium.json",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "version": parsed["version"],
        "tag": parsed["tag"],
        "channel": parsed["channel"],
        "platform": parsed["platform"],
        "source": parsed["source"],
    }


def _verify_checkout_and_origin(
    checkout: pathlib.Path, git: GitRunner, expected_origin: str
) -> dict[str, Any]:
    if _git_text(git, "rev-parse", "--is-inside-work-tree") != "true":
        raise CheckoutHydrationError("Chromium checkout is not a Git worktree")
    raw_urls = git(("remote", "get-url", "--all", "origin"), None, True).stdout
    try:
        urls = [value for value in raw_urls.decode("utf-8", "strict").splitlines() if value]
    except UnicodeDecodeError as error:
        raise CheckoutHydrationError("origin URL is not valid UTF-8") from error
    if urls != [expected_origin] or expected_origin != OFFICIAL_ORIGIN:
        raise CheckoutHydrationError(
            f"origin must be exactly the official Chromium repository: {OFFICIAL_ORIGIN}"
        )
    promisor = _git_text(git, "config", "--bool", "--get", "remote.origin.promisor")
    if promisor != "true":
        raise CheckoutHydrationError("origin is not configured as a promisor remote")
    partial_filter = _git_text(
        git, "config", "--get", "remote.origin.partialclonefilter"
    )
    if partial_filter != "blob:none":
        raise CheckoutHydrationError(
            "origin partial-clone filter must be exactly blob:none"
        )
    return {
        "verified": True,
        "name": "origin",
        "url": expected_origin,
        "promisor": True,
        "partialCloneFilter": partial_filter,
    }


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


def inventory_target(
    git: GitRunner, target: str, *, max_blobs: int = DEFAULT_MAX_BLOBS
) -> TargetInventory:
    target = _require_sha1(target, "target")
    _safe_int(max_blobs, 1, MAX_BLOBS, "max-blobs")
    object_type = _git_text(git, "cat-file", "-t", target)
    if object_type != "commit":
        raise CheckoutHydrationError("pinned target is not an available commit object")
    tree = _git_text(git, "rev-parse", "--verify", f"{target}^{{tree}}")
    _require_sha1(tree, "target tree")
    raw = git(("ls-tree", "-r", "-z", "--full-tree", target), None, True).stdout
    digest = hashlib.sha256(
        b"ahoi-target-tree-inventory-v1\0"
        + target.encode("ascii")
        + b"\0"
        + tree.encode("ascii")
        + b"\0"
    )
    blob_ids: set[str] = set()
    previous_path: bytes | None = None
    entry_count = 0
    submodule_count = 0
    for record in raw.split(b"\0"):
        if not record:
            continue
        digest.update(record + b"\0")
        entry_count += 1
        try:
            metadata, path = record.split(b"\t", 1)
            raw_mode, raw_type, raw_oid = metadata.split(b" ", 2)
            mode = raw_mode.decode("ascii", "strict")
            object_kind = raw_type.decode("ascii", "strict")
            oid = raw_oid.decode("ascii", "strict")
        except (UnicodeDecodeError, ValueError) as error:
            raise CheckoutHydrationError("git ls-tree returned malformed data") from error
        _require_sha1(oid, "tree entry object")
        if not path or path == previous_path:
            raise CheckoutHydrationError("target tree contains an invalid duplicate path")
        previous_path = path
        if mode in {"100644", "100755", "120000"} and object_kind == "blob":
            blob_ids.add(oid)
        elif mode == "160000" and object_kind == "commit":
            submodule_count += 1
        else:
            raise CheckoutHydrationError(
                f"target tree returned an invalid mode/type pair: {mode} {object_kind}"
            )
        if len(blob_ids) > max_blobs:
            raise CheckoutHydrationError(
                f"target exceeds the configured {max_blobs}-blob inventory bound"
            )
    ordered = tuple(sorted(blob_ids))
    missing = _missing_objects(git, ordered)
    return TargetInventory(
        commit=target,
        tree=tree,
        entry_count=entry_count,
        submodule_count=submodule_count,
        blob_ids=ordered,
        missing_blob_ids=missing,
        tree_inventory_sha256=digest.hexdigest(),
    )


def _oid_digest(object_ids: Sequence[str]) -> str:
    digest = hashlib.sha256(b"ahoi-object-id-inventory-v1\0")
    for oid in sorted(object_ids):
        digest.update(oid.encode("ascii") + b"\0")
    return digest.hexdigest()


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


def _transport_report(statistics: FetchStatistics, args: argparse.Namespace) -> dict[str, Any]:
    failures = [
        {"objectId": oid, "detail": detail}
        for oid, detail in sorted(statistics.singleton_failures.items())[:128]
    ]
    return {
        "batchSize": args.batch_size,
        "attemptsPerBatch": args.attempts,
        "maxFetchCommands": args.max_fetch_commands,
        "fetchTimeoutSeconds": args.fetch_timeout,
        "httpVersion": "HTTP/1.1",
        "httpMaxRequests": 1,
        "fetchParallel": 1,
        "commandCount": statistics.command_count,
        "successfulCommandCount": statistics.successful_commands,
        "failedCommandCount": statistics.failed_commands,
        "retryCount": statistics.retry_count,
        "adaptiveSplitCount": statistics.adaptive_splits,
        "commandBudgetExhausted": statistics.command_budget_exhausted,
        "singletonFailureCount": len(statistics.singleton_failures),
        "singletonFailures": failures,
        "singletonFailuresTruncated": len(statistics.singleton_failures) > len(failures),
    }


def _report(
    *,
    args: argparse.Namespace,
    pin: Mapping[str, Any],
    origin: Mapping[str, Any],
    inventory: TargetInventory,
    statistics: FetchStatistics,
    remaining: Sequence[str],
    phase: str,
    before: Mapping[str, Any],
    after: Mapping[str, Any] | None,
    exit_code: int | None,
) -> dict[str, Any]:
    changed = [] if after is None else changed_guard_fields(before, after)
    guard_verified = after is not None
    return {
        "schemaVersion": 1,
        "command": "chromium_checkout_hydration",
        "phase": phase,
        "complete": not remaining and guard_verified and not changed,
        "dryRun": bool(args.dry_run),
        "exitCode": exit_code,
        "target": {
            "commit": inventory.commit,
            "tree": inventory.tree,
            "pin": dict(pin),
        },
        "origin": dict(origin),
        "inventory": {
            "verified": True,
            "lazyFetchDisabled": True,
            "entryCount": inventory.entry_count,
            "submoduleCount": inventory.submodule_count,
            "uniqueTargetBlobCount": len(inventory.blob_ids),
            "initiallyPresentBlobCount": len(inventory.blob_ids)
            - len(inventory.missing_blob_ids),
            "initiallyMissingBlobCount": len(inventory.missing_blob_ids),
            "initialMissingObjectIdsSha256": _oid_digest(
                inventory.missing_blob_ids
            ),
            "remainingMissingBlobCount": len(remaining),
            "remainingObjectIdsSha256": _oid_digest(remaining),
            "treeInventorySha256": inventory.tree_inventory_sha256,
            "resumeModel": (
                "recompute_the_pinned_target_inventory_and_skip_objects_already_"
                "present_in_the_immutable_object_store"
            ),
        },
        "transport": _transport_report(statistics, args),
        "mutationGuard": {
            "verified": guard_verified,
            "unchanged": None if not guard_verified else not changed,
            "changedFields": changed,
            "before": dict(before),
            "after": None if after is None else dict(after),
            "protected": [
                "worktree",
                "index",
                "HEAD",
                "refs",
                "FETCH_HEAD",
                "shallow boundary",
            ],
            "allowedMutation": (
                "verified immutable target blob objects and Git object-pack metadata only"
            ),
        },
    }


def _write_report(
    output: pathlib.Path,
    *,
    repository: pathlib.Path,
    checkout: pathlib.Path,
    payload: Mapping[str, Any],
) -> None:
    rendered = json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    with PreparedReportOutput.prepare(
        output,
        repository=repository,
        checkout=checkout,
        protected_files=(repository / "config/chromium.json",),
    ) as prepared:
        prepared.write(rendered)


def run_hydration(
    args: argparse.Namespace,
    *,
    fetcher: FetchRunner | None = None,
    sleeper: Callable[[float], None] = time.sleep,
) -> tuple[dict[str, Any], int]:
    repository = args.repository.resolve()
    checkout = (
        args.checkout.resolve()
        if args.checkout is not None
        else repository / ".work/chromium/src"
    )
    if args.output is None:
        output = repository / "artifacts/build/chromium-checkout-hydration.json"
    else:
        # Keep the final path component unresolved so the output guard can
        # identify and reject a symlink instead of silently following it.
        output = (
            args.output
            if args.output.is_absolute()
            else pathlib.Path.cwd() / args.output
        )
    if not repository.is_dir():
        raise CheckoutHydrationError("repository root does not exist")
    if not checkout.is_dir():
        raise CheckoutHydrationError("Chromium checkout does not exist")
    target = _require_sha1(args.target, "target")
    _safe_int(args.batch_size, 1, MAX_BATCH_SIZE, "batch-size")
    _safe_int(args.attempts, 1, MAX_ATTEMPTS, "attempts")
    _safe_int(
        args.fetch_timeout,
        1,
        MAX_FETCH_TIMEOUT_SECONDS,
        "fetch-timeout",
    )
    _safe_int(
        args.max_fetch_commands,
        1,
        MAX_FETCH_COMMANDS,
        "max-fetch-commands",
    )
    _safe_int(args.checkpoint_batches, 1, 10_000, "checkpoint-batches")
    _safe_int(args.max_blobs, 1, MAX_BLOBS, "max-blobs")

    # Reserve and remove a temporary inode now so unsafe destinations fail
    # before any Git object is fetched.
    with PreparedReportOutput.prepare(
        output,
        repository=repository,
        checkout=checkout,
        protected_files=(repository / "config/chromium.json",),
    ):
        pass

    environment = _git_environment()
    git = _git_runner(checkout, environment)
    pin = _load_verified_pin(repository, target)
    origin = _verify_checkout_and_origin(checkout, git, str(pin["source"]))
    before = checkout_snapshot(checkout, environment)
    inventory = inventory_target(git, target, max_blobs=args.max_blobs)
    statistics = FetchStatistics()
    remaining = inventory.missing_blob_ids

    initial = _report(
        args=args,
        pin=pin,
        origin=origin,
        inventory=inventory,
        statistics=statistics,
        remaining=remaining,
        phase="inventory_complete" if remaining else "complete_pending_guard",
        before=before,
        after=None,
        exit_code=None,
    )
    _write_report(
        output,
        repository=repository,
        checkout=checkout,
        payload=initial,
    )

    interrupted = False
    runtime_error: CheckoutHydrationError | None = None
    if remaining and not args.dry_run:
        actual_fetcher = fetcher or (
            lambda object_ids: _fetch_object_ids(
                checkout, environment, object_ids, args.fetch_timeout
            )
        )
        initial_missing = set(inventory.missing_blob_ids)

        def check_missing(object_ids: Sequence[str]) -> tuple[str, ...]:
            return _missing_objects(git, object_ids)

        def checkpoint(current: FetchStatistics) -> None:
            if current.completed_top_level_batches % args.checkpoint_batches:
                return
            estimated_remaining = tuple(
                sorted(initial_missing - current.hydrated)
            )
            payload = _report(
                args=args,
                pin=pin,
                origin=origin,
                inventory=inventory,
                statistics=current,
                remaining=estimated_remaining,
                phase="fetching",
                before=before,
                after=None,
                exit_code=None,
            )
            _write_report(
                output,
                repository=repository,
                checkout=checkout,
                payload=payload,
            )

        try:
            statistics = fetch_adaptively(
                inventory.missing_blob_ids,
                batch_size=args.batch_size,
                attempts=args.attempts,
                max_fetch_commands=args.max_fetch_commands,
                fetch=actual_fetcher,
                missing=check_missing,
                retry_backoff_seconds=args.retry_backoff_seconds,
                sleeper=sleeper,
                progress=checkpoint,
            )
        except KeyboardInterrupt:
            statistics.interrupted = True
            interrupted = True
        except CheckoutHydrationError as error:
            runtime_error = error

    try:
        remaining = _missing_objects(git, inventory.blob_ids)
        after = checkout_snapshot(checkout, environment)
    except CheckoutHydrationError as error:
        if runtime_error is None:
            runtime_error = error
        after = checkout_snapshot(checkout, environment)
        remaining = inventory.missing_blob_ids

    changed = changed_guard_fields(before, after)
    if changed:
        exit_code = EXIT_MUTATION
        phase = "mutation_detected"
    elif interrupted:
        exit_code = EXIT_INTERRUPTED
        phase = "interrupted_resumable"
    elif runtime_error is not None:
        exit_code = EXIT_ERROR
        phase = "runtime_error_resumable"
    elif remaining:
        exit_code = EXIT_INCOMPLETE
        phase = "dry_run_incomplete" if args.dry_run else "incomplete_resumable"
    else:
        exit_code = EXIT_OK
        phase = "complete"
    final = _report(
        args=args,
        pin=pin,
        origin=origin,
        inventory=inventory,
        statistics=statistics,
        remaining=remaining,
        phase=phase,
        before=before,
        after=after,
        exit_code=exit_code,
    )
    if runtime_error is not None:
        final["error"] = _clean_detail(str(runtime_error))
    _write_report(
        output,
        repository=repository,
        checkout=checkout,
        payload=final,
    )
    return final, exit_code


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--repository", type=pathlib.Path, default=ROOT)
    parser.add_argument("--checkout", type=pathlib.Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE)
    parser.add_argument("--attempts", type=int, default=DEFAULT_ATTEMPTS)
    parser.add_argument(
        "--fetch-timeout", type=int, default=DEFAULT_FETCH_TIMEOUT_SECONDS
    )
    parser.add_argument(
        "--max-fetch-commands", type=int, default=DEFAULT_MAX_FETCH_COMMANDS
    )
    parser.add_argument(
        "--checkpoint-batches", type=int, default=DEFAULT_CHECKPOINT_BATCHES
    )
    parser.add_argument("--max-blobs", type=int, default=DEFAULT_MAX_BLOBS)
    parser.add_argument("--retry-backoff-seconds", type=float, default=1.0)
    return parser


def main(argv: Sequence[str]) -> int:
    try:
        args = _parser().parse_args(argv)
        payload, exit_code = run_hydration(args)
        if exit_code == EXIT_ERROR and payload.get("error"):
            print(f"error: {payload['error']}", file=sys.stderr)
        return exit_code
    except (
        CheckoutHydrationError,
        OSError,
        ReportOutputError,
        VerificationError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
