#!/usr/bin/env python3
"""Prehydrate every missing blob for a pinned Chromium checkout target.

The command deliberately does not switch revisions.  It inventories the exact
target tree without lazy fetching and obtains immutable missing blobs either
from the verified ``origin`` promisor or, when explicitly selected, from exact
official Gitiles target paths.  A later ``gclient sync`` can then update the
worktree without issuing thousands of opportunistic blob requests.

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
import pathlib
import re
import stat
import subprocess  # patched as chromium_checkout_hydration.subprocess in tests
import sys
import time
from dataclasses import dataclass
from typing import Any, Callable, Mapping, Sequence

from chromium_checkout_fetch import (
    FetchResult,
    FetchRunner,
    FetchStatistics,
    GitilesResponseLoader,
    MAX_ATTEMPTS,
    MAX_BATCH_SIZE,
    MAX_BLOBS,
    MAX_FETCH_COMMANDS,
    MAX_FETCH_TIMEOUT_SECONDS,
    MAX_GITILES_JOBS,
    _fetch_object_ids,
    _hydrate_gitiles_blobs,
    _missing_objects,
    _require_sha1,
    _safe_int,
    fetch_adaptively,
    fetch_command,
)
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
from chromium_roll_hydration import (
    DEFAULT_MAX_RESPONSE_BYTES,
    DEFAULT_MAX_TOTAL_RESPONSE_BYTES,
    GITILES_BASE,
    NETWORK_ATTEMPTS,
    HydrationError,
    fetch_gitiles_response,
    gitiles_blob_url,
    validate_git_path,
    validate_limits,
)
from chromium_roll_output import PreparedReportOutput, ReportOutputError
from verify_chromium_pin import VerificationError, validate_config


ROOT = pathlib.Path(__file__).resolve().parents[1]
OFFICIAL_ORIGIN = "https://chromium.googlesource.com/chromium/src.git"

DEFAULT_BATCH_SIZE = 16
DEFAULT_ATTEMPTS = 3
DEFAULT_FETCH_TIMEOUT_SECONDS = 300
DEFAULT_MAX_FETCH_COMMANDS = 4096
DEFAULT_CHECKPOINT_BATCHES = 16
DEFAULT_MAX_BLOBS = 2_000_000
DEFAULT_GITILES_JOBS = 4
DEFAULT_NETWORK_TIMEOUT_SECONDS = 20
DEFAULT_TOTAL_TIMEOUT_SECONDS = 900


EXIT_OK = 0
EXIT_ERROR = 1
EXIT_INCOMPLETE = 2
EXIT_MUTATION = 3
EXIT_INTERRUPTED = 130


@dataclass(frozen=True)
class TargetInventory:
    commit: str
    tree: str
    entry_count: int
    submodule_count: int
    blob_ids: tuple[str, ...]
    missing_blob_ids: tuple[str, ...]
    blob_paths: tuple[tuple[str, str], ...]
    tree_inventory_sha256: str


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
    blob_paths: dict[str, str] = {}
    previous_path: bytes | None = None
    entry_count = 0
    submodule_count = 0
    for record in raw.split(b"\0"):
        if not record:
            continue
        digest.update(record + b"\0")
        entry_count += 1
        try:
            metadata, raw_path = record.split(b"\t", 1)
            raw_mode, raw_type, raw_oid = metadata.split(b" ", 2)
            mode = raw_mode.decode("ascii", "strict")
            object_kind = raw_type.decode("ascii", "strict")
            oid = raw_oid.decode("ascii", "strict")
            path = validate_git_path(raw_path.decode("utf-8", "strict"))
        except (UnicodeDecodeError, ValueError) as error:
            raise CheckoutHydrationError("git ls-tree returned malformed data") from error
        _require_sha1(oid, "tree entry object")
        if not raw_path or raw_path == previous_path:
            raise CheckoutHydrationError("target tree contains an invalid duplicate path")
        previous_path = raw_path
        if mode in {"100644", "100755", "120000"} and object_kind == "blob":
            blob_ids.add(oid)
            current_path = blob_paths.get(oid)
            if current_path is None or path < current_path:
                blob_paths[oid] = path
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
        blob_paths=tuple(sorted(blob_paths.items())),
        tree_inventory_sha256=digest.hexdigest(),
    )


def _oid_digest(object_ids: Sequence[str]) -> str:
    digest = hashlib.sha256(b"ahoi-object-id-inventory-v1\0")
    for oid in sorted(object_ids):
        digest.update(oid.encode("ascii") + b"\0")
    return digest.hexdigest()


def _transport_report(statistics: FetchStatistics, args: argparse.Namespace) -> dict[str, Any]:
    failures = [
        {"objectId": oid, "detail": detail}
        for oid, detail in sorted(statistics.singleton_failures.items())[:128]
    ]
    report = {
        "configured": args.transport,
        "maxFetchCommands": args.max_fetch_commands,
        "commandCount": statistics.command_count,
        "successfulCommandCount": statistics.successful_commands,
        "failedCommandCount": statistics.failed_commands,
        "retryCount": statistics.retry_count,
        "adaptiveSplitCount": statistics.adaptive_splits,
        "commandBudgetExhausted": statistics.command_budget_exhausted,
        "responseBudgetExhausted": statistics.response_budget_exhausted,
        "singletonFailureCount": len(statistics.singleton_failures),
        "singletonFailures": failures,
        "singletonFailuresTruncated": len(statistics.singleton_failures) > len(failures),
    }
    if args.transport == "git":
        report.update(
            {
                "batchSize": args.batch_size,
                "attemptsPerBatch": args.attempts,
                "fetchTimeoutSeconds": args.fetch_timeout,
                "httpVersion": "HTTP/1.1",
                "httpMaxRequests": 1,
                "fetchParallel": 1,
            }
        )
    else:
        report.update(
            {
                "baseUrl": GITILES_BASE,
                "jobs": args.jobs,
                "networkAttemptsPerRequest": NETWORK_ATTEMPTS,
                "networkTimeoutSeconds": args.network_timeout,
                "totalTimeoutSeconds": args.total_timeout,
                "maxResponseBytes": args.max_response_bytes,
                "maxTotalResponseBytes": args.max_total_response_bytes,
                "requestCount": statistics.command_count,
                "successfulRequestCount": statistics.successful_commands,
                "failedRequestCount": statistics.failed_commands,
                "responseBytes": statistics.response_bytes,
                "decodedBytes": statistics.decoded_bytes,
            }
        )
    return report


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
    gitiles_loader: GitilesResponseLoader | None = None,
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
    if args.transport not in {"git", "gitiles"}:
        raise CheckoutHydrationError("transport must be git or gitiles")
    if args.transport == "git":
        if args.jobs is not None:
            raise CheckoutHydrationError("jobs is only supported by Gitiles transport")
    else:
        args.jobs = DEFAULT_GITILES_JOBS if args.jobs is None else args.jobs
        _safe_int(args.jobs, 1, MAX_GITILES_JOBS, "jobs")
        try:
            validate_limits(
                args.network_timeout,
                args.total_timeout,
                args.max_response_bytes,
                args.max_total_response_bytes,
            )
        except HydrationError as error:
            raise CheckoutHydrationError(str(error)) from error
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
            if args.transport == "git":
                actual_fetcher = fetcher or (
                    lambda object_ids: _fetch_object_ids(
                        checkout, environment, object_ids, args.fetch_timeout
                    )
                )
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
            else:
                if gitiles_loader is None:

                    def load_response(
                        target_id: str,
                        path: str,
                        object_id: str,
                        timeout: int,
                        maximum: int,
                    ) -> bytes:
                        del object_id
                        return fetch_gitiles_response(
                            gitiles_blob_url(target_id, path), timeout, maximum
                        )
                else:
                    load_response = gitiles_loader
                statistics = _hydrate_gitiles_blobs(
                    git=git,
                    target=target,
                    object_ids=inventory.missing_blob_ids,
                    blob_paths=dict(inventory.blob_paths),
                    jobs=args.jobs,
                    max_requests=args.max_fetch_commands,
                    network_timeout=args.network_timeout,
                    total_timeout=args.total_timeout,
                    max_response_bytes=args.max_response_bytes,
                    max_total_response_bytes=args.max_total_response_bytes,
                    load_response=load_response,
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
    parser.add_argument(
        "--transport", choices=("git", "gitiles"), default="git"
    )
    parser.add_argument("--jobs", type=int)
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
    parser.add_argument(
        "--network-timeout", type=int, default=DEFAULT_NETWORK_TIMEOUT_SECONDS
    )
    parser.add_argument(
        "--total-timeout", type=int, default=DEFAULT_TOTAL_TIMEOUT_SECONDS
    )
    parser.add_argument(
        "--max-response-bytes", type=int, default=DEFAULT_MAX_RESPONSE_BYTES
    )
    parser.add_argument(
        "--max-total-response-bytes",
        type=int,
        default=DEFAULT_MAX_TOTAL_RESPONSE_BYTES,
    )
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
