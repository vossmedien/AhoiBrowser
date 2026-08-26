"""Read-only Git helpers and mutation guards for Chromium checkout hydration."""

from __future__ import annotations

import hashlib
import os
import pathlib
import shutil
import stat
import subprocess
import tempfile
from typing import Any, Callable, Mapping, Sequence


GIT_ENVIRONMENT_KEYS = (
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_DIR",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_WORK_TREE",
)


class CheckoutHydrationError(ValueError):
    """The checkout cannot be hydrated while retaining the safety contract."""


GitRunner = Callable[
    [Sequence[str], bytes | None, bool], subprocess.CompletedProcess[bytes]
]


def clean_detail(raw: bytes | str, maximum: int = 500) -> str:
    rendered = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else raw
    compact = " ".join(rendered.strip().split())
    return compact[: maximum - 3] + "..." if len(compact) > maximum else compact


def git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for key in tuple(environment):
        if key in GIT_ENVIRONMENT_KEYS or key.startswith("GIT_CONFIG_"):
            environment.pop(key, None)
    environment.update(
        {
            "GIT_NO_LAZY_FETCH": "1",
            "GIT_OPTIONAL_LOCKS": "0",
            "GIT_TERMINAL_PROMPT": "0",
            "LC_ALL": "C",
        }
    )
    return environment


def git_runner(checkout: pathlib.Path, environment: Mapping[str, str]) -> GitRunner:
    def run(
        arguments: Sequence[str],
        input_bytes: bytes | None = None,
        check: bool = True,
    ) -> subprocess.CompletedProcess[bytes]:
        result = subprocess.run(
            ("git", *arguments),
            cwd=checkout,
            env=dict(environment),
            input=input_bytes,
            capture_output=True,
            check=False,
        )
        if check and result.returncode != 0:
            detail = clean_detail(result.stderr or result.stdout)
            suffix = f": {detail}" if detail else ""
            raise CheckoutHydrationError(
                f"git {' '.join(arguments[:3])} failed{suffix}"
            )
        return result

    return run


def git_text(git: GitRunner, *arguments: str) -> str:
    raw = git(arguments, None, True).stdout
    try:
        return raw.decode("utf-8", "strict").strip()
    except UnicodeDecodeError as error:
        raise CheckoutHydrationError("Git returned invalid UTF-8") from error


def _file_sha256(path: pathlib.Path, label: str) -> str:
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        return "missing"
    if not stat.S_ISREG(metadata.st_mode) or path.is_symlink():
        raise CheckoutHydrationError(f"{label} is not a regular file")
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise CheckoutHydrationError(f"could not read {label}: {error}") from error
    return digest.hexdigest()


def _git_path(checkout: pathlib.Path, git: GitRunner, name: str) -> pathlib.Path:
    raw = git_text(git, "rev-parse", "--git-path", name)
    path = pathlib.Path(raw)
    return path if path.is_absolute() else checkout / path


def _hash_worktree_path(digest: Any, checkout: pathlib.Path, raw_path: bytes) -> None:
    digest.update(raw_path + b"\0")
    path = checkout / pathlib.Path(os.fsdecode(raw_path))
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        digest.update(b"missing\0")
        return
    if stat.S_ISLNK(metadata.st_mode):
        digest.update(b"symlink\0" + os.fsencode(os.readlink(path)) + b"\0")
        return
    if stat.S_ISREG(metadata.st_mode):
        mode = b"executable" if metadata.st_mode & 0o111 else b"regular"
        digest.update(mode + b"\0")
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
        digest.update(b"\0")
        return
    if stat.S_ISDIR(metadata.st_mode):
        digest.update(b"directory\0")
        return
    digest.update(f"special:{stat.S_IFMT(metadata.st_mode):o}".encode("ascii") + b"\0")


def _worktree_snapshot(
    checkout: pathlib.Path, environment: Mapping[str, str], index: pathlib.Path
) -> tuple[str, str]:
    with tempfile.TemporaryDirectory(prefix="ahoi-checkout-guard-") as temporary:
        copied_index = pathlib.Path(temporary) / "index"
        shutil.copyfile(index, copied_index)
        isolated_environment = dict(environment)
        isolated_environment["GIT_INDEX_FILE"] = str(copied_index)
        git = git_runner(checkout, isolated_environment)
        status = git(
            ("status", "--porcelain=v1", "-z", "--untracked-files=all"),
            None,
            True,
        ).stdout
        changed = git(
            ("diff", "--name-only", "-z", "--no-ext-diff", "--"),
            None,
            True,
        ).stdout.split(b"\0")
        untracked = git(
            ("ls-files", "--others", "--exclude-standard", "-z"),
            None,
            True,
        ).stdout.split(b"\0")
        digest = hashlib.sha256(b"ahoi-checkout-worktree-v1\0" + status + b"\0")
        for raw_path in sorted({path for path in (*changed, *untracked) if path}):
            _hash_worktree_path(digest, checkout, raw_path)
    return hashlib.sha256(status).hexdigest(), digest.hexdigest()


def checkout_snapshot(
    checkout: pathlib.Path, environment: Mapping[str, str]
) -> dict[str, Any]:
    git = git_runner(checkout, environment)
    index = _git_path(checkout, git, "index")
    index_sha256 = _file_sha256(index, "Chromium index")
    if index_sha256 == "missing":
        raise CheckoutHydrationError("Chromium index is missing")
    status_sha256, worktree_sha256 = _worktree_snapshot(checkout, environment, index)
    refs = git(
        ("for-each-ref", "--format=%(refname)%00%(objectname)%00%(symref)%00"),
        None,
        True,
    ).stdout
    return {
        "head": git_text(git, "rev-parse", "--verify", "HEAD"),
        "headFileSha256": _file_sha256(_git_path(checkout, git, "HEAD"), "HEAD"),
        "indexSha256": index_sha256,
        "refsSha256": hashlib.sha256(refs).hexdigest(),
        "refCount": refs.count(b"\n"),
        "fetchHeadSha256": _file_sha256(
            _git_path(checkout, git, "FETCH_HEAD"), "FETCH_HEAD"
        ),
        "shallowSha256": _file_sha256(
            _git_path(checkout, git, "shallow"), "shallow boundary"
        ),
        "statusSha256": status_sha256,
        "worktreeSha256": worktree_sha256,
    }


def changed_guard_fields(before: Mapping[str, Any], after: Mapping[str, Any]) -> list[str]:
    return sorted(
        key for key in set(before) | set(after) if before.get(key) != after.get(key)
    )
