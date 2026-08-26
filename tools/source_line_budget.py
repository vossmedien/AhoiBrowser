#!/usr/bin/env python3
"""Enforce the physical-line budget for Ahoi-owned source files."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from typing import Iterable, Optional


MAX_SOURCE_LINES = 800
SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".css",
    ".gn",
    ".gni",
    ".h",
    ".hpp",
    ".html",
    ".js",
    ".jsx",
    ".m",
    ".mm",
    ".py",
    ".sh",
    ".swift",
    ".ts",
    ".tsx",
    ".zsh",
}
EXCLUDED_TRACKED_PREFIXES = (
    ".work/",
    "patches/",
    "overlay/chromium/src/ahoi/",
    "overlay/chromium/src/third_party/",
)


class SourceLineBudgetError(RuntimeError):
    """Raised when the source inventory cannot be built safely."""


@dataclass(frozen=True, order=True)
class SourceLineCount:
    path: str
    lines: int


def is_source(path: pathlib.PurePath) -> bool:
    return path.suffix.lower() in SOURCE_SUFFIXES


def count_lines(path: pathlib.Path) -> int:
    return len(path.read_bytes().splitlines())


def git_tracked_files(root: pathlib.Path) -> tuple[pathlib.PurePosixPath, ...]:
    completed = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if completed.returncode:
        raise SourceLineBudgetError("cannot enumerate tracked repository files")
    return tuple(
        pathlib.PurePosixPath(raw.decode("utf-8"))
        for raw in completed.stdout.split(b"\0")
        if raw
    )


def tracked_source_counts(root: pathlib.Path) -> tuple[SourceLineCount, ...]:
    counts = []
    for relative in git_tracked_files(root):
        serialized = relative.as_posix()
        if serialized.startswith(EXCLUDED_TRACKED_PREFIXES):
            continue
        if not is_source(relative):
            continue
        path = root / relative
        if not path.is_file():
            raise SourceLineBudgetError(f"tracked source is missing: {relative}")
        counts.append(SourceLineCount(serialized, count_lines(path)))
    return tuple(counts)


def patch_series_entries(series: pathlib.Path) -> tuple[str, ...]:
    entries = []
    for raw in series.read_text(encoding="utf-8").splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        relative = pathlib.PurePosixPath(entry)
        if relative.is_absolute() or ".." in relative.parts:
            raise SourceLineBudgetError(f"unsafe patch series entry: {entry}")
        entries.append(entry)
    return tuple(entries)


def materialize_committed_ahoi_source(
    root: pathlib.Path, destination: pathlib.Path
) -> pathlib.Path:
    overlay = root / "overlay/chromium/src/ahoi"
    patch_root = root / "patches/chromium"
    series = patch_root / "series"
    if not overlay.is_dir() or not series.is_file():
        raise SourceLineBudgetError("Ahoi Chromium overlay or patch series is missing")
    target = destination / "ahoi"
    shutil.copytree(overlay, target)
    for entry in patch_series_entries(series):
        patch = patch_root / entry
        if not patch.is_file():
            raise SourceLineBudgetError(f"patch is missing: {entry}")
        completed = subprocess.run(
            [
                "git",
                "apply",
                "--include=ahoi/**",
                "--whitespace=error-all",
                str(patch),
            ],
            cwd=destination,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode:
            detail = (completed.stderr or completed.stdout).strip()
            raise SourceLineBudgetError(
                f"cannot materialize Ahoi source from {entry}: {detail}"
            )
    return target


def tree_source_counts(
    source_root: pathlib.Path, *, logical_prefix: str
) -> tuple[SourceLineCount, ...]:
    if not source_root.is_dir():
        raise SourceLineBudgetError(f"source root is missing: {source_root}")
    counts = []
    for path in sorted(source_root.rglob("*")):
        if path.is_file() and not path.is_symlink() and is_source(path):
            relative = path.relative_to(source_root).as_posix()
            counts.append(
                SourceLineCount(
                    f"{logical_prefix}/{relative}",
                    count_lines(path),
                )
            )
    return tuple(counts)


def collect_source_counts(
    root: pathlib.Path,
    *,
    chromium_src: Optional[pathlib.Path] = None,
) -> tuple[SourceLineCount, ...]:
    root = root.resolve()
    counts = list(tracked_source_counts(root))
    with tempfile.TemporaryDirectory(prefix="ahoi-source-budget-") as raw_temp:
        committed = materialize_committed_ahoi_source(root, pathlib.Path(raw_temp))
        counts.extend(
            tree_source_counts(committed, logical_prefix="chromium-committed/ahoi")
        )
    if chromium_src is not None:
        live_ahoi = chromium_src.resolve() / "ahoi"
        if live_ahoi.is_dir():
            counts.extend(
                tree_source_counts(live_ahoi, logical_prefix="chromium-live/ahoi")
            )
    return tuple(counts)


def violations(
    counts: Iterable[SourceLineCount], *, maximum: int = MAX_SOURCE_LINES
) -> tuple[SourceLineCount, ...]:
    return tuple(sorted((item for item in counts if item.lines > maximum)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--chromium-src", type=pathlib.Path)
    parser.add_argument("--max-lines", type=int, default=MAX_SOURCE_LINES)
    args = parser.parse_args()
    if args.max_lines < 1:
        parser.error("--max-lines must be positive")
    chromium_src = args.chromium_src
    if chromium_src is None:
        default_checkout = args.root / ".work/chromium/src"
        chromium_src = default_checkout if default_checkout.is_dir() else None
    try:
        counts = collect_source_counts(args.root, chromium_src=chromium_src)
        over_budget = violations(counts, maximum=args.max_lines)
    except (OSError, UnicodeError, SourceLineBudgetError) as error:
        print(f"error: {error}")
        return 2
    if over_budget:
        print(f"Ahoi source files exceed {args.max_lines} physical lines:")
        for item in over_budget:
            print(f"  {item.lines:4d}  {item.path}")
        return 1
    print(
        f"Ahoi source line budget passed: {len(counts)} files, "
        f"maximum {args.max_lines} lines"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
