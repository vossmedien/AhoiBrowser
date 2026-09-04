#!/usr/bin/env python3
"""Compose an Ahoi overlay and ordered patch series without touching a checkout."""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import tempfile
from typing import Optional

from chromium_roll_git import RollError, add_overlay


GIT_CONTEXT_VARIABLES = (
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_DIR",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_WORK_TREE",
)


def isolated_git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for name in GIT_CONTEXT_VARIABLES:
        environment.pop(name, None)
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    environment["GIT_TERMINAL_PROMPT"] = "0"
    return environment


def run(
    *args: str,
    cwd: pathlib.Path,
    env: dict[str, str],
    input_bytes: Optional[bytes] = None,
) -> bytes:
    return subprocess.run(
        args,
        cwd=cwd,
        env=env,
        input=input_bytes,
        check=True,
        capture_output=True,
    ).stdout


def resolve_base_commit(
    checkout: pathlib.Path,
    environment: dict[str, str],
    revision: str,
) -> str:
    """Resolve a caller-supplied revision to an exact commit without option parsing."""

    try:
        return run(
            "git",
            "rev-parse",
            "--verify",
            "--quiet",
            "--end-of-options",
            f"{revision}^{{commit}}",
            cwd=checkout,
            env=environment,
        ).decode("ascii").strip()
    except (subprocess.CalledProcessError, UnicodeDecodeError) as error:
        raise SystemExit(f"invalid base revision: {revision!r}") from error


def git_object_directory(
    checkout: pathlib.Path, environment: dict[str, str]
) -> pathlib.Path:
    """Return the real checkout object directory before enabling isolation."""

    try:
        raw = run(
            "git",
            "rev-parse",
            "--git-path",
            "objects",
            cwd=checkout,
            env=environment,
        ).decode("utf-8").strip()
    except (subprocess.CalledProcessError, UnicodeDecodeError) as error:
        raise SystemExit("could not resolve the checkout object directory") from error
    path = pathlib.Path(raw)
    return (path if path.is_absolute() else checkout / path).resolve()


def overlay_entries(root: pathlib.Path):
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or path.is_file():
            relative = path.relative_to(root)
            if ".git" in relative.parts or ".." in relative.parts:
                raise SystemExit(f"unsafe overlay path: {relative}")
            yield path, relative


def series_entries(series_path: pathlib.Path) -> list[str]:
    entries = []
    for raw in series_path.read_text(encoding="utf-8").splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        relative = pathlib.PurePosixPath(entry)
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"unsafe patch series entry: {entry}")
        entries.append(entry)
    return entries


def compose_overlay(
    checkout: pathlib.Path,
    overlay: pathlib.Path,
    series: pathlib.Path,
    patch_root: pathlib.Path,
    *,
    base_revision: str = "HEAD",
    diff_base_revision: str | None = None,
) -> tuple[bytes, str]:
    """Return a binary delta and the resulting canonical overlay tree.

    Composition always starts from ``base_revision``. By default the returned
    delta also starts there. ``diff_base_revision`` may name a different tree
    when a caller needs a minimal transition from an already applied,
    independently verified overlay tree to the newly composed result.
    """

    checkout = checkout.resolve()
    overlay = overlay.resolve()
    patch_root = patch_root.resolve()
    series = series.resolve()
    if not (checkout / ".git").exists():
        raise SystemExit(f"not a Git checkout: {checkout}")
    if not overlay.is_dir():
        raise SystemExit(f"overlay directory is missing: {overlay}")
    if not series.is_file():
        raise SystemExit(f"patch series is missing: {series}")

    diff_configuration: list[str] = []
    diff_attributes = patch_root / "diff.gitattributes"
    if diff_attributes.exists():
        if diff_attributes.is_symlink() or not diff_attributes.is_file():
            raise SystemExit(
                f"unsafe Chromium diff attributes input: {diff_attributes}"
            )
        diff_configuration = [
            "-c",
            f"core.attributesFile={diff_attributes}",
            "-c",
            "diff.ahoi-binary.binary=true",
        ]

    with tempfile.TemporaryDirectory(prefix="ahoi-overlay-index-") as temp_root:
        environment = isolated_git_environment()
        base_commit = resolve_base_commit(checkout, environment, base_revision)
        real_objects = git_object_directory(checkout, environment)
        temporary_root = pathlib.Path(temp_root)
        temporary_objects = temporary_root / "objects"
        temporary_objects.mkdir()
        # Git expects GIT_INDEX_FILE to name a path that does not yet exist.
        environment["GIT_INDEX_FILE"] = str(temporary_root / "index")
        environment["GIT_OBJECT_DIRECTORY"] = str(temporary_objects)
        environment["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = str(real_objects)
        run("git", "read-tree", base_commit, cwd=checkout, env=environment)
        base_tree = run(
            "git",
            "rev-parse",
            "--verify",
            "--quiet",
            "--end-of-options",
            f"{base_commit}^{{tree}}",
            cwd=checkout,
            env=environment,
        ).decode("ascii").strip()
        diff_base_tree = base_tree
        if diff_base_revision is not None:
            try:
                diff_base_tree = run(
                    "git",
                    "rev-parse",
                    "--verify",
                    "--quiet",
                    "--end-of-options",
                    f"{diff_base_revision}^{{tree}}",
                    cwd=checkout,
                    env=environment,
                ).decode("ascii").strip()
            except (subprocess.CalledProcessError, UnicodeDecodeError) as error:
                raise SystemExit(
                    f"invalid diff base revision: {diff_base_revision!r}"
                ) from error
        try:
            # Reuse roll preflight's NUL-delimited batch and isolated object
            # writer. Rewriting Chromium's entire index once per overlay file
            # made every verification needlessly quadratic in index I/O.
            add_overlay(
                checkout,
                environment,
                [
                    (source, relative.as_posix())
                    for source, relative in overlay_entries(overlay)
                ],
            )
        except RollError as error:
            raise SystemExit(f"could not compose overlay entries: {error}") from error
        for entry in series_entries(series):
            patch = (patch_root / entry).resolve()
            try:
                patch.relative_to(patch_root)
            except ValueError as error:
                raise SystemExit(
                    f"patch resolves outside patch root: {entry}"
                ) from error
            if not patch.is_file():
                raise SystemExit(f"patch listed in series does not exist: {entry}")
            run(
                "git",
                "apply",
                "--cached",
                "--whitespace=error-all",
                str(patch),
                cwd=checkout,
                env=environment,
            )
        combined = run(
            "git",
            *diff_configuration,
            "diff",
            "--cached",
            "--binary",
            "--full-index",
            "--no-ext-diff",
            "--no-renames",
            "--no-color",
            "--diff-algorithm=myers",
            "--src-prefix=a/",
            "--dst-prefix=b/",
            diff_base_tree,
            "--",
            cwd=checkout,
            env=environment,
        )
        tree = run(
            "git",
            "write-tree",
            cwd=checkout,
            env=environment,
        ).decode("ascii").strip()
    if tree == base_tree:
        raise SystemExit("overlay and patch series produced no checkout delta")
    if not combined and diff_base_revision is None:
        raise SystemExit("overlay and patch series produced no checkout delta")
    return combined, tree


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=pathlib.Path, required=True)
    parser.add_argument("--overlay", type=pathlib.Path, required=True)
    parser.add_argument("--series", type=pathlib.Path, required=True)
    parser.add_argument("--patch-root", type=pathlib.Path, required=True)
    parser.add_argument(
        "--base-revision",
        default="HEAD",
        metavar="REVISION",
        help="compose against this commit or ref (default: HEAD)",
    )
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    combined, _ = compose_overlay(
        args.checkout,
        args.overlay,
        args.series,
        args.patch_root,
        base_revision=args.base_revision,
    )
    args.output.write_bytes(combined)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
