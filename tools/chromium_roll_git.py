"""Git-safe primitives shared by Chromium roll checkout commands."""

from __future__ import annotations

import hashlib
import os
import pathlib
import shutil
import stat
import subprocess
import tempfile
import zlib
from typing import Mapping, Sequence

from verify_chromium_pin import VerificationError, require_sha1


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


def safe_relative(
    path: pathlib.Path, root: pathlib.Path, label: str
) -> pathlib.Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise RollError(f"{label} resolves outside its root") from error
    return resolved


def git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for key in GIT_ENVIRONMENT_KEYS:
        environment.pop(key, None)
    # Even nominally read-only commands such as `git status` may otherwise
    # refresh and rewrite the real index's stat cache.
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    environment["GIT_TERMINAL_PROMPT"] = "0"
    return environment


def git(
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


def git_text(
    checkout: pathlib.Path, environment: Mapping[str, str], *args: str
) -> str:
    raw = git(checkout, environment, *args).stdout
    try:
        return raw.decode("utf-8").strip()
    except UnicodeDecodeError as error:
        raise RollError("Git returned invalid UTF-8") from error


def validate_revision(revision: str) -> None:
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


def resolve_commit(
    checkout: pathlib.Path, environment: Mapping[str, str], revision: str
) -> str:
    validate_revision(revision)
    result = git(
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


def _index_path(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> pathlib.Path:
    value = git_text(checkout, environment, "rev-parse", "--git-path", "index")
    path = pathlib.Path(value)
    return path if path.is_absolute() else checkout / path


def object_path(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> pathlib.Path:
    value = git_text(checkout, environment, "rev-parse", "--git-path", "objects")
    path = pathlib.Path(value)
    return (path if path.is_absolute() else checkout / path).resolve()


def _file_digest(path: pathlib.Path) -> str:
    if not path.exists():
        return "missing"
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _worktree_digest(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> tuple[str, str]:
    status = git(
        checkout,
        environment,
        "status",
        "--porcelain=v1",
        "-z",
        "--untracked-files=all",
    ).stdout
    delta = git(
        checkout,
        environment,
        "diff",
        "--binary",
        "--full-index",
        "--no-ext-diff",
        "HEAD",
        "--",
    ).stdout
    untracked = git(
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


def checkout_snapshot(
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
        "head": git_text(checkout, environment, "rev-parse", "HEAD"),
        "indexSha256": index_digest,
        "statusSha256": status_digest,
        "worktreeSha256": worktree_digest,
    }


def series_entries(
    series: pathlib.Path, patch_root: pathlib.Path
) -> list[pathlib.Path]:
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
        patch = safe_relative(patch_root / entry, patch_root, "patch")
        if not patch.is_file() or patch.is_symlink():
            raise RollError(f"patch listed in series is missing or unsafe: {entry}")
        patches.append(patch)
    return patches


def overlay_entries(overlay: pathlib.Path) -> list[tuple[pathlib.Path, str]]:
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


def add_overlay(
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
        object_file = object_root / object_id[:2] / object_id[2:]
        if not object_file.exists():
            object_file.parent.mkdir(parents=True, exist_ok=True)
            object_file.write_bytes(zlib.compress(loose))
        index_info.extend(
            mode.encode("ascii")
            + b" "
            + object_id.encode("ascii")
            + b"\t"
            + os.fsencode(relative)
            + b"\0"
        )
    git(
        checkout,
        environment,
        "update-index",
        "-z",
        "--index-info",
        input_bytes=bytes(index_info),
    )


def tree_paths(
    checkout: pathlib.Path, environment: Mapping[str, str], commit: str
) -> set[str]:
    raw = git(
        checkout, environment, "ls-tree", "-r", "-z", "--name-only", commit
    ).stdout
    return {os.fsdecode(value) for value in raw.split(b"\0") if value}


def patch_paths(
    checkout: pathlib.Path, environment: Mapping[str, str], payload: bytes
) -> list[str]:
    result = git(
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


def classify_patch(
    checkout: pathlib.Path,
    environment: Mapping[str, str],
    payload: bytes,
) -> tuple[str, str]:
    base_args = ("apply", "--cached", "--whitespace=error-all")
    forward = git(
        checkout,
        environment,
        *base_args,
        "--check",
        "-",
        input_bytes=payload,
        check=False,
    )
    if forward.returncode == 0:
        git(checkout, environment, *base_args, "-", input_bytes=payload)
        return "applies", "applies cleanly"
    reverse = git(
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
