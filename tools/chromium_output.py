"""Bind Chromium outputs to the default out tree or an explicit physical root."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import stat


PROFILES = {"AhoiDev", "AhoiFullDev", "AhoiRelease", "AhoiFullRelease",
            "AhoiUpstreamRelease"}


def absolute_path(raw: str) -> Path:
    if any(ord(character) < 32 or ord(character) == 127 for character in raw):
        raise SystemExit("output directory contains control characters")
    if any(component in {".", ".."} for component in raw.split(os.sep)):
        raise SystemExit("output directory must not contain dot components")
    path = Path(raw)
    if not path.is_absolute():
        raise SystemExit("output directory must be absolute")
    return path


def check_components(path: Path, start: Path) -> None:
    cursor = start
    for component in path.relative_to(start).parts:
        cursor = cursor / component
        try:
            metadata = cursor.lstat()
        except FileNotFoundError:
            break
        if stat.S_ISLNK(metadata.st_mode):
            raise SystemExit(f"output directory component is a symlink: {cursor}")
        if not stat.S_ISDIR(metadata.st_mode):
            raise SystemExit(f"output directory component is not a directory: {cursor}")


def configured_output_root(source: Path) -> Path:
    raw = os.environ.get("AHOI_CHROMIUM_OUT_ROOT")
    if raw is None:
        source = source.resolve()
        root = source / "out"
        check_components(root, source)
        return root
    root = absolute_path(raw)
    if root == Path(root.anchor):
        raise SystemExit("AHOI_CHROMIUM_OUT_ROOT must not be a filesystem root")
    check_components(root, Path(root.anchor))
    if not root.is_dir():
        raise SystemExit("AHOI_CHROMIUM_OUT_ROOT must be an existing directory")
    if root.resolve(strict=True) != root:
        raise SystemExit("AHOI_CHROMIUM_OUT_ROOT must be a physical directory")
    return root


def validate_output_directory(source: Path, raw: str) -> Path:
    candidate = absolute_path(raw)
    root = configured_output_root(source)
    lexical_root = (source / "out" if os.environ.get("AHOI_CHROMIUM_OUT_ROOT")
                    is None else root)
    try:
        relative = candidate.relative_to(lexical_root)
    except ValueError:
        try:
            relative = candidate.relative_to(root)
        except ValueError as error:
            raise SystemExit(f"output directory must be below {root}") from error
    if not relative.parts:
        raise SystemExit("output directory must name a child below Chromium out root")
    output = root / relative
    check_components(output, root)
    resolved = output.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise SystemExit(f"resolved output directory escapes {root}") from error
    return resolved


def profile_output_directory(source: Path, profile: str) -> Path:
    if profile not in PROFILES:
        raise SystemExit(f"unsupported Chromium output profile: {profile}")
    root = configured_output_root(source)
    return validate_output_directory(source, str(root / profile))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--root", action="store_true")
    action.add_argument("--profile", choices=sorted(PROFILES))
    action.add_argument("--out-dir")
    args = parser.parse_args()
    if args.root:
        result = configured_output_root(args.source)
    elif args.profile:
        result = profile_output_directory(args.source, args.profile)
    else:
        args.source.resolve(strict=True)
        result = validate_output_directory(args.source, args.out_dir)
    print(result)


if __name__ == "__main__":
    main()
