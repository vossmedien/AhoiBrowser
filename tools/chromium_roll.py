#!/usr/bin/env python3
"""Discover and preflight Chromium Stable rolls without mutating a checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import stat
import sys
import tempfile
from typing import Any, Sequence

from chromium_roll_discovery import (
    DiscoveryError,
    discover,
)
from chromium_roll_candidate import (
    load_baseline as _load_baseline,
    promote_candidate as _promote_candidate,
    target_binding as _target_binding,
)
from chromium_roll_git import (
    RollError,
    add_overlay as _add_overlay,
    checkout_snapshot as _checkout_snapshot,
    classify_patch as _classify_patch,
    git as _git,
    git_environment as _git_environment,
    git_text as _git_text,
    object_path as _object_path,
    overlay_entries as _overlay_entries,
    patch_paths as _patch_paths,
    resolve_commit as _resolve_commit,
    safe_relative as _safe_relative,
    series_entries as _series_entries,
    tree_paths as _tree_paths,
)
from chromium_roll_hydration import (
    DEFAULT_MAX_RESPONSE_BYTES,
    DEFAULT_MAX_TOTAL_RESPONSE_BYTES,
    GITILES_BASE,
    HydrationError,
    fetch_gitiles_response,
    gitiles_blob_url,
    hydrate_target_blobs,
    patch_stack_report,
    read_fixture_response,
    validate_git_path,
)
from chromium_roll_output import PreparedReportOutput, ReportOutputError
from verify_chromium_pin import (
    VerificationError,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _preflight(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    repository = args.repository.resolve()
    checkout = args.checkout.resolve() if args.checkout else repository / ".work/chromium/src"
    if not repository.is_dir():
        raise RollError("repository root does not exist")
    environment = _git_environment()
    environment["GIT_NO_LAZY_FETCH"] = "1"
    if not checkout.is_dir():
        raise RollError("Chromium checkout does not exist")
    inside = _git_text(checkout, environment, "rev-parse", "--is-inside-work-tree")
    if inside != "true":
        raise RollError("Chromium checkout is not a Git worktree")
    target = _resolve_commit(checkout, environment, args.target)
    target_binding = _target_binding(repository, target)
    config_path = _safe_relative(repository / "config/chromium.json", repository, "config")
    overlay = _safe_relative(repository / "overlay/chromium/src", repository, "overlay")
    patch_root = _safe_relative(repository / "patches/chromium", repository, "patch root")
    series = _safe_relative(patch_root / "series", patch_root, "series")
    baseline = _load_baseline(config_path)
    patches = _series_entries(series, patch_root)
    overlay_entries = _overlay_entries(overlay)
    before = _checkout_snapshot(checkout, environment)
    target_paths = _tree_paths(checkout, environment, target)
    overlay_paths = {relative for _, relative in overlay_entries}
    collisions = sorted(overlay_paths & target_paths)
    overlay_fingerprint = hashlib.sha256()
    for source, relative in overlay_entries:
        overlay_fingerprint.update(relative.encode("utf-8") + b"\0")
        source_stat = source.lstat()
        if stat.S_ISLNK(source_stat.st_mode):
            mode = b"120000"
            content = os.fsencode(os.readlink(source))
        elif source_stat.st_mode & 0o111:
            mode = b"100755"
            content = source.read_bytes()
        else:
            mode = b"100644"
            content = source.read_bytes()
        overlay_fingerprint.update(mode + b"\0" + content + b"\0")

    patch_reports: list[dict[str, Any]] = []
    conflicts: list[str] = []
    with tempfile.TemporaryDirectory(prefix="ahoi-roll-preflight-") as temporary:
        temp_root = pathlib.Path(temporary)
        objects = temp_root / "objects"
        objects.mkdir()
        isolated = dict(environment)
        isolated["GIT_INDEX_FILE"] = str(temp_root / "index")
        isolated["GIT_OBJECT_DIRECTORY"] = str(objects)
        isolated["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = str(
            _object_path(checkout, environment)
        )
        isolated["GIT_WORK_TREE"] = str(checkout)
        _git(checkout, isolated, "read-tree", target)
        _add_overlay(checkout, isolated, overlay_entries)
        for patch in patches:
            payload = patch.read_bytes()
            name = patch.relative_to(patch_root).as_posix()
            touched = _patch_paths(checkout, isolated, payload)
            blocked_by = list(conflicts)
            classification, detail = _classify_patch(
                checkout, isolated, payload
            )
            if classification == "conflict":
                conflicts.append(name)
            patch_reports.append(
                {
                    "path": name,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "bytes": len(payload),
                    "touchedPaths": touched,
                    "overlayCollisions": sorted(overlay_paths & set(touched)),
                    "classification": classification,
                    "authoritative": not blocked_by,
                    "blockedBy": blocked_by,
                    "summary": detail,
                }
            )
        result_tree = _git_text(checkout, isolated, "write-tree", "--missing-ok")

    after = _checkout_snapshot(checkout, environment)
    unchanged = before == after
    if not unchanged:
        raise RollError("Chromium HEAD, index, or worktree changed during preflight")
    counts = {
        classification: sum(
            report["classification"] == classification for report in patch_reports
        )
        for classification in ("applies", "already_upstream", "conflict")
    }
    ready = counts["already_upstream"] == 0 and counts["conflict"] == 0
    report = {
        "schemaVersion": 1,
        "command": "preflight",
        "ready": ready,
        "baseline": {
            "version": baseline["version"],
            "commit": baseline["commit"],
        },
        "target": {
            "requested": args.target,
            "commit": target,
            "binding": target_binding,
        },
        "inputs": {
            "config": "config/chromium.json",
            "overlay": "overlay/chromium/src",
            "series": "patches/chromium/series",
        },
        "overlay": {
            "fileCount": len(overlay_entries),
            "sha256": overlay_fingerprint.hexdigest(),
            "collisionCount": len(collisions),
            "collisions": collisions,
        },
        "patches": patch_reports,
        "summary": {"patchCount": len(patch_reports), **counts},
        "resultTree": result_tree,
        "mutationGuard": {
            "verified": True,
            "unchanged": unchanged,
            "head": before["head"],
            "indexSha256": before["indexSha256"],
            "statusSha256": before["statusSha256"],
            "worktreeSha256": before["worktreeSha256"],
        },
    }
    return report, 0 if ready else 2


def _discover(args: argparse.Namespace) -> dict[str, Any]:
    return discover(args)


def _hydrate(args: argparse.Namespace) -> dict[str, Any]:
    repository = args.repository.resolve()
    checkout = (
        args.checkout.resolve()
        if args.checkout
        else repository / ".work/chromium/src"
    )
    if not repository.is_dir():
        raise RollError("repository root does not exist")
    if not checkout.is_dir():
        raise RollError("Chromium checkout does not exist")
    environment = _git_environment()
    environment["GIT_NO_LAZY_FETCH"] = "1"
    environment["GIT_LITERAL_PATHSPECS"] = "1"
    if (
        _git_text(checkout, environment, "rev-parse", "--is-inside-work-tree")
        != "true"
    ):
        raise RollError("Chromium checkout is not a Git worktree")
    before = _checkout_snapshot(checkout, environment)
    target = _resolve_commit(checkout, environment, args.target)
    target_binding = _target_binding(repository, target)
    patch_root = _safe_relative(
        repository / "patches/chromium", repository, "patch root"
    )
    series = _safe_relative(patch_root / "series", patch_root, "series")
    patches = _series_entries(series, patch_root)
    patch_payloads = [(patch, patch.read_bytes()) for patch in patches]
    touched: set[str] = set()
    for _, payload in patch_payloads:
        touched.update(_patch_paths(checkout, environment, payload))
    include_paths = sorted({validate_git_path(path) for path in args.include_path})
    touched.update(include_paths)

    fixture_root = args.offline_response_directory
    if fixture_root is None:
        transport = "official_gitiles"

        def load_response(target_id, path, object_id, timeout, maximum):
            del object_id
            return fetch_gitiles_response(
                gitiles_blob_url(target_id, path), timeout, maximum
            )

    else:
        if fixture_root.is_symlink():
            raise RollError("offline response directory is a symlink")
        fixture_root = fixture_root.resolve()
        transport = "offline_fixture"

        def load_response(target_id, path, object_id, timeout, maximum):
            del target_id, path, timeout
            return read_fixture_response(fixture_root, object_id, maximum)
    def isolated_git(command, input_bytes=None, check=True):
        return _git(
            checkout,
            environment,
            "-c",
            "maintenance.auto=false",
            "-c",
            "gc.auto=0",
            *command,
            input_bytes=input_bytes,
            check=check,
        ).stdout
    try:
        result = hydrate_target_blobs(
            git=isolated_git,
            target=target,
            touched_paths=sorted(touched),
            load_response=load_response,
            timeout=args.network_timeout,
            total_timeout=args.total_timeout,
            max_response_bytes=args.max_response_bytes,
            max_total_response_bytes=args.max_total_response_bytes,
        )
    finally:
        after = _checkout_snapshot(checkout, environment)
        if before != after:
            raise RollError("Chromium changed during hydration")
    unchanged = True
    return {
        "schemaVersion": 1,
        "command": "hydrate",
        "target": {"commit": target, "binding": target_binding},
        "source": {
            "baseUrl": GITILES_BASE if transport == "official_gitiles" else None,
            "configuredTransport": transport,
            "transportUsed": (
                transport if result["summary"]["requestedBlobCount"] else "none"
            ),
        },
        "inputs": {
            "series": "patches/chromium/series",
            "patchCount": len(patches),
            "patchStack": patch_stack_report(
                [
                    (path.relative_to(patch_root).as_posix(), payload)
                    for path, payload in patch_payloads
                ]
            ),
            "includePaths": include_paths,
        },
        "limits": {
            "networkTimeoutSeconds": args.network_timeout,
            "totalTimeoutSeconds": args.total_timeout,
            "maxResponseBytes": args.max_response_bytes,
            "maxTotalResponseBytes": args.max_total_response_bytes,
        },
        **result,
        "mutationGuard": {
            "verified": True,
            "unchanged": unchanged,
            "head": before["head"],
            "indexSha256": before["indexSha256"],
            "statusSha256": before["statusSha256"],
            "worktreeSha256": before["worktreeSha256"],
            "allowedMutation": "verified target blobs added to the Git object store only",
        },
    }


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    discover = subparsers.add_parser(
        "discover", help="prove the newest fully rolled Mac Stable candidate"
    )
    discover.add_argument("--online", action="store_true")
    discover.add_argument("--network-timeout", type=int, default=20)
    discover.add_argument("--retrieved-at")
    discover.add_argument("--release-json", type=pathlib.Path)
    discover.add_argument("--dash-json", type=pathlib.Path)
    discover.add_argument("--tag-ref-json", type=pathlib.Path)
    discover.add_argument("--branch-ref-json", type=pathlib.Path)
    discover.add_argument("--commit-json", type=pathlib.Path)
    discover.add_argument("--branch-point-json", type=pathlib.Path)
    discover.add_argument("--version-text", type=pathlib.Path)
    discover.add_argument("--output", type=pathlib.Path)

    promote = subparsers.add_parser(
        "promote-candidate",
        help="promote an explicitly reviewed discovery file to the candidate binding",
    )
    promote.add_argument("--repository", type=pathlib.Path, default=ROOT)
    promote.add_argument("--candidate", type=pathlib.Path, required=True)
    promote.add_argument("--accept-sha256", required=True)
    promote.add_argument("--accept-version", required=True)
    promote.add_argument("--accept-commit", required=True)

    hydrate = subparsers.add_parser(
        "hydrate", help="fetch only missing target blobs touched by the patch stack"
    )
    hydrate.add_argument("--repository", type=pathlib.Path, default=ROOT)
    hydrate.add_argument("--checkout", type=pathlib.Path)
    hydrate.add_argument("--target", required=True)
    hydrate.add_argument("--include-path", action="append", default=[])
    hydrate.add_argument("--network-timeout", type=int, default=20)
    hydrate.add_argument("--total-timeout", type=int, default=900)
    hydrate.add_argument(
        "--max-response-bytes", type=int, default=DEFAULT_MAX_RESPONSE_BYTES
    )
    hydrate.add_argument(
        "--max-total-response-bytes",
        type=int,
        default=DEFAULT_MAX_TOTAL_RESPONSE_BYTES,
    )
    hydrate.add_argument("--offline-response-directory", type=pathlib.Path)
    hydrate.add_argument("--output", type=pathlib.Path)

    preflight = subparsers.add_parser(
        "preflight", help="classify overlay and patch compatibility at a local target"
    )
    preflight.add_argument("--repository", type=pathlib.Path, default=ROOT)
    preflight.add_argument("--checkout", type=pathlib.Path)
    preflight.add_argument("--target", required=True)
    preflight.add_argument("--output", type=pathlib.Path)
    return parser


def main(argv: Sequence[str]) -> int:
    try:
        args = _parser().parse_args(argv)
        if args.command == "promote-candidate":
            payload = _promote_candidate(args)
            sys.stdout.write(
                json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False)
                + "\n"
            )
            return 0
        repository = (
            args.repository.resolve() if hasattr(args, "repository") else ROOT
        )
        checkout = None
        if args.command in {"hydrate", "preflight"}:
            checkout = (
                args.checkout.resolve()
                if args.checkout
                else repository / ".work/chromium/src"
            )
        protected = (
            repository / "config/chromium.json",
            repository / "config/upstream-roll-candidate.json",
            repository / "patches/chromium/series",
        )
        with PreparedReportOutput.prepare(
            args.output,
            repository=repository,
            checkout=checkout,
            protected_files=protected,
        ) as report_output:
            if args.command == "discover":
                payload, exit_code = _discover(args), 0
            elif args.command == "hydrate":
                payload, exit_code = _hydrate(args), 0
            else:
                payload, exit_code = _preflight(args)
            rendered = (
                json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False)
                + "\n"
            )
            if report_output.output is None:
                sys.stdout.write(rendered)
            else:
                report_output.write(rendered)
        return exit_code
    except (
        DiscoveryError,
        HydrationError,
        OSError,
        ReportOutputError,
        RollError,
        VerificationError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
