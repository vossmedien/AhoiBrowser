#!/usr/bin/env python3
"""Hash the exact semantic inputs consumed by the Chromium overlay composer."""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib


def add(digest, *values: bytes) -> None:
    for value in values:
        digest.update(value)
        digest.update(b"\0")


def fingerprint(root: pathlib.Path) -> str:
    root = root.resolve()
    patch_root = root / "patches/chromium"
    overlay_root = root / "overlay/chromium/src"
    digest = hashlib.sha256()

    for path in sorted(patch_root.rglob("*")):
        if not (path.is_file() or path.is_symlink()):
            continue
        relative = path.relative_to(root).as_posix().encode("utf-8")
        if path.is_symlink():
            raise SystemExit(f"patch inputs must not be symlinks: {relative!r}")
        add(digest, b"patch", relative, path.read_bytes())

    if overlay_root.is_dir():
        for path in sorted(overlay_root.rglob("*")):
            if not (path.is_file() or path.is_symlink()):
                continue
            relative = path.relative_to(root).as_posix().encode("utf-8")
            source_stat = path.lstat()
            if path.is_symlink():
                mode = b"120000"
                content = os.readlink(path).encode("utf-8", "surrogateescape")
            elif source_stat.st_mode & 0o111:
                mode = b"100755"
                content = path.read_bytes()
            else:
                mode = b"100644"
                content = path.read_bytes()
            add(digest, b"overlay", relative, mode, content)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(fingerprint(args.repository))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
