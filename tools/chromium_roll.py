#!/usr/bin/env python3
"""Discover and preflight Chromium Stable rolls without mutating a checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import zlib
from typing import Any, Mapping, Sequence

from chromium_roll_discovery import DiscoveryError, discover
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
    load_json,
    require_sha1,
    validate_config,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
GIT_ENVIRONMENT_KEYS = (
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_DIR",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_WORK_TREE",
)


class RollError(ValueError):
    """A roll candidate or local preflight could not be proven safely."""


def _safe_relative(path: pathlib.Path, root: pathlib.Path, label: str) -> pathlib.Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise RollError(f"{label} resolves outside its root") from error
    return resolved


def _git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for key in GIT_ENVIRONMENT_KEYS:
        environment.pop(key, None)
    # Even nominally read-only commands such as `git status` may otherwise
    # refresh and rewrite the real index's stat cache.
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    environment["GIT_TERMINAL_PROMPT"] = "0"
    return environment


def _git(
    checkout: pathlib.Path,
    environment: Mapping[str, str],
    *args: str,
    input_bytes: bytes | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(
        ("git", *args),
        cwd=checkout,
        env=dict(environment),
        input=input_bytes,
        capture_output=True,
        check=False,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        suffix = f": {detail}" if detail else ""
        raise RollError(f"git {' '.join(args)} failed{suffix}")
    return result


def _git_text(
    checkout: pathlib.Path, environment: Mapping[str, str], *args: str
) -> str:
    raw = _git(checkout, environment, *args).stdout
    try:
        return raw.decode("utf-8").strip()
    except UnicodeDecodeError as error:
        raise RollError("Git returned invalid UTF-8") from error


def _validate_revision(revision: str) -> None:
    allowed = frozenset(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._/-"
    )
    if (
        not revision
        or len(revision) > 200
        or revision.startswith("-")
        or revision.startswith("/")
        or revision.endswith(("/", "."))
        or "//" in revision
        or any(part in {"", ".", ".."} for part in revision.split("/"))
        or any(character not in allowed for character in revision)
        or any(ord(character) < 32 or ord(character) == 127 for character in revision)
    ):
        raise RollError("target revision is unsafe")


def _resolve_commit(
    checkout: pathlib.Path, environment: Mapping[str, str], revision: str
) -> str:
    _validate_revision(revision)
    result = _git(
        checkout,
        environment,
        "rev-parse",
        "--verify",
        "--quiet",
        "--end-of-options",
        f"{revision}^{{commit}}",
        check=False,
    )
    if result.returncode != 0:
        raise RollError(f"target object is not available locally: {revision}")
    try:
        return require_sha1(result.stdout.decode("ascii").strip(), "target commit")
    except (UnicodeDecodeError, VerificationError) as error:
        raise RollError("target did not resolve to an exact Git commit") from error


def _index_path(checkout: pathlib.Path, environment: Mapping[str, str]) -> pathlib.Path:
    value = _git_text(checkout, environment, "rev-parse", "--git-path", "index")
    path = pathlib.Path(value)
    return path if path.is_absolute() else checkout / path


def _object_path(checkout: pathlib.Path, environment: Mapping[str, str]) -> pathlib.Path:
    value = _git_text(checkout, environment, "rev-parse", "--git-path", "objects")
    path = pathlib.Path(value)
    return (path if path.is_absolute() else checkout / path).resolve()


def _file_digest(path: pathlib.Path) -> str:
    if not path.exists():
        return "missing"
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _worktree_digest(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> tuple[str, str]:
    status = _git(
        checkout,
        environment,
        "status",
        "--porcelain=v1",
        "-z",
        "--untracked-files=all",
    ).stdout
    delta = _git(
        checkout,
        environment,
        "diff",
        "--binary",
        "--full-index",
        "--no-ext-diff",
        "HEAD",
        "--",
    ).stdout
    untracked = _git(
        checkout,
        environment,
        "ls-files",
        "--others",
        "--exclude-standard",
        "-z",
    ).stdout.split(b"\0")
    digest = hashlib.sha256(b"ahoi-worktree-v1\0" + status + b"\0" + delta)
    for raw_path in sorted(path for path in untracked if path):
        relative = pathlib.Path(os.fsdecode(raw_path))
        path = checkout / relative
        digest.update(raw_path + b"\0")
        if path.is_symlink():
            digest.update(b"L" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update(b"F" + path.read_bytes())
        else:
            digest.update(b"O")
    return hashlib.sha256(status).hexdigest(), digest.hexdigest()


def _checkout_snapshot(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> dict[str, str]:
    index = _index_path(checkout, environment)
    index_digest = _file_digest(index)
    if index_digest == "missing":
        raise RollError("Chromium checkout index is missing")
    with tempfile.TemporaryDirectory(prefix="ahoi-roll-snapshot-") as temporary:
        snapshot_index = pathlib.Path(temporary) / "index"
        shutil.copyfile(index, snapshot_index)
        snapshot_environment = dict(environment)
        snapshot_environment["GIT_INDEX_FILE"] = str(snapshot_index)
        status_digest, worktree_digest = _worktree_digest(
            checkout, snapshot_environment
        )
    return {
        "head": _git_text(checkout, environment, "rev-parse", "HEAD"),
        "indexSha256": index_digest,
        "statusSha256": status_digest,
        "worktreeSha256": worktree_digest,
    }


def _series_entries(series: pathlib.Path, patch_root: pathlib.Path) -> list[pathlib.Path]:
    try:
        lines = series.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        raise RollError(f"could not read patch series: {error}") from error
    patches: list[pathlib.Path] = []
    seen: set[str] = set()
    for line_number, raw in enumerate(lines, 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        relative = pathlib.PurePosixPath(entry)
        if relative.is_absolute() or ".." in relative.parts or entry.startswith("-"):
            raise RollError(f"unsafe patch series entry at line {line_number}")
        if entry in seen:
            raise RollError(f"duplicate patch series entry: {entry}")
        seen.add(entry)
        patch = _safe_relative(patch_root / entry, patch_root, "patch")
        if not patch.is_file() or patch.is_symlink():
            raise RollError(f"patch listed in series is missing or unsafe: {entry}")
        patches.append(patch)
    return patches


def _overlay_entries(overlay: pathlib.Path) -> list[tuple[pathlib.Path, str]]:
    if not overlay.is_dir():
        raise RollError("overlay/chromium/src is missing")
    entries: list[tuple[pathlib.Path, str]] = []
    for source in sorted(overlay.rglob("*")):
        if not (source.is_file() or source.is_symlink()):
            continue
        relative = source.relative_to(overlay)
        if ".git" in relative.parts or ".." in relative.parts:
            raise RollError(f"unsafe overlay path: {relative.as_posix()}")
        entries.append((source, relative.as_posix()))
    return entries


def _add_overlay(
    checkout: pathlib.Path,
    environment: Mapping[str, str],
    entries: Sequence[tuple[pathlib.Path, str]],
) -> None:
    object_root_value = environment.get("GIT_OBJECT_DIRECTORY")
    if object_root_value is None:
        raise RollError("isolated Git object directory is missing")
    object_root = pathlib.Path(object_root_value)
    index_info = bytearray()
    for source, relative in entries:
        source_stat = source.lstat()
        if stat.S_ISLNK(source_stat.st_mode):
            mode, content = "120000", os.fsencode(os.readlink(source))
        elif source_stat.st_mode & 0o111:
            mode, content = "100755", source.read_bytes()
        else:
            mode, content = "100644", source.read_bytes()
        loose = b"blob " + str(len(content)).encode("ascii") + b"\0" + content
        object_id = hashlib.sha1(loose, usedforsecurity=False).hexdigest()
        object_path = object_root / object_id[:2] / object_id[2:]
        if not object_path.exists():
            object_path.parent.mkdir(parents=True, exist_ok=True)
            object_path.write_bytes(zlib.compress(loose))
        index_info.extend(
            mode.encode("ascii")
            + b" "
            + object_id.encode("ascii")
            + b"\t"
            + os.fsencode(relative)
            + b"\0"
        )
    _git(
        checkout,
        environment,
        "update-index",
        "-z",
        "--index-info",
        input_bytes=bytes(index_info),
    )


def _tree_paths(
    checkout: pathlib.Path, environment: Mapping[str, str], commit: str
) -> set[str]:
    raw = _git(
        checkout, environment, "ls-tree", "-r", "-z", "--name-only", commit
    ).stdout
    return {os.fsdecode(value) for value in raw.split(b"\0") if value}


def _patch_paths(
    checkout: pathlib.Path, environment: Mapping[str, str], payload: bytes
) -> list[str]:
    result = _git(
        checkout,
        environment,
        "apply",
        "--numstat",
        "-z",
        "-",
        input_bytes=payload,
        check=False,
    )
    if result.returncode != 0:
        raise RollError("could not parse patch paths")
    fields = result.stdout.split(b"\0")
    paths: list[str] = []
    index = 0
    while index < len(fields):
        field = fields[index]
        index += 1
        if not field:
            continue
        parts = field.split(b"\t", 2)
        if len(parts) != 3:
            raise RollError("patch numstat output is malformed")
        if parts[2]:
            paths.append(os.fsdecode(parts[2]))
            continue
        if index + 1 >= len(fields):
            raise RollError("patch rename numstat output is malformed")
        old_path, new_path = fields[index], fields[index + 1]
        index += 2
        paths.extend((os.fsdecode(old_path), os.fsdecode(new_path)))
    for path in paths:
        relative = pathlib.PurePosixPath(path)
        if relative.is_absolute() or ".." in relative.parts:
            raise RollError("patch contains an unsafe path")
    return sorted(set(paths))


def _portable_detail(raw: bytes) -> str:
    detail = raw.decode("utf-8", "replace").strip().replace("\n", " | ")
    return detail[:800] if detail else "patch does not match the candidate tree"


def _classify_patch(
    checkout: pathlib.Path,
    environment: Mapping[str, str],
    payload: bytes,
) -> tuple[str, str]:
    base_args = ("apply", "--cached", "--whitespace=error-all")
    forward = _git(
        checkout,
        environment,
        *base_args,
        "--check",
        "-",
        input_bytes=payload,
        check=False,
    )
    if forward.returncode == 0:
        _git(checkout, environment, *base_args, "-", input_bytes=payload)
        return "applies", "applies cleanly"
    reverse = _git(
        checkout,
        environment,
        *base_args,
        "--reverse",
        "--check",
        "-",
        input_bytes=payload,
        check=False,
    )
    if reverse.returncode == 0:
        return "already_upstream", "reverse check succeeds; patch disposition required"
    return "conflict", _portable_detail(forward.stderr or forward.stdout)


def _load_baseline(path: pathlib.Path) -> dict[str, Any]:
    try:
        baseline = load_json(path)
    except (OSError, VerificationError) as error:
        raise RollError(f"invalid Chromium baseline: {error}") from error
    if not isinstance(baseline, dict):
        raise RollError("Chromium baseline must be a JSON object")
    try:
        validate_config(baseline)
    except VerificationError as error:
        raise RollError(f"invalid Chromium baseline: {error}") from error
    return baseline


def _target_binding(repository: pathlib.Path, target: str) -> dict[str, Any]:
    bindings: list[dict[str, str]] = []
    for relative in (
        "config/chromium.json",
        "config/upstream-roll-candidate.json",
    ):
        path = _safe_relative(repository / relative, repository, "target binding")
        if not path.is_file() or path.is_symlink():
            continue
        config = _load_baseline(path)
        if config["commit"] == target:
            bindings.append(
                {
                    "config": relative,
                    "version": config["version"],
                    "tag": config["tag"],
                    "commit": config["commit"],
                }
            )
    if not bindings:
        raise RollError(
            "target is not bound by config/chromium.json or "
            "config/upstream-roll-candidate.json"
        )
    identities = {(item["version"], item["tag"]) for item in bindings}
    if len(identities) != 1:
        raise RollError("target binding configs disagree on version or tag")
    return {"verified": True, "configs": bindings}


def _preflight(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    repository = args.repository.resolve()
    checkout = args.checkout.resolve() if args.checkout else repository / ".work/chromium/src"
    if not repository.is_dir():
        raise RollError("repository root does not exist")
    environment = _git_environment()
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
        result_tree = _git_text(checkout, isolated, "write-tree")

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
    after = _checkout_snapshot(checkout, environment)
    unchanged = before == after
    if not unchanged:
        raise RollError("Chromium HEAD, index, or worktree changed during hydration")
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
