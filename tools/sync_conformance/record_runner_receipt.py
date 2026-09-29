#!/usr/bin/env python3
"""Bind existing output and tested artifacts to the owner's frozen checkout.

Does not launch tests. --exit-code must be the owner's captured direct process
exit, not the status of tee/grep or a guessed XCTest result. The source-to-binary
relationship still comes from the owner's build/test log, not this hash receipt.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys

from compare_runner_outputs import EvidenceError, json_text, read_json, require, sha256


def receipt(output, fixture, source_tree, binaries, destination, exit_code):
    output_raw, document = read_json(output)
    require(type(document) is dict and document.get("kind") == "ahoi-sync-merge-output",
            "not a runner output")
    fixture_hash = sha256(Path(fixture).read_bytes())
    require(document.get("fixtureSha256") == fixture_hash and
            document.get("fixtureName") == Path(fixture).name,
            "runner output does not match this fixture")

    def git(*args):
        return subprocess.check_output(["git", "-C", str(source_tree), *args], text=True).strip()

    commit = git("rev-parse", "HEAD")
    clean = not git("status", "--porcelain", "--untracked-files=normal")
    require(clean, "source checkout is dirty; preserve output but do not attest clean source")
    binary_hashes = {}
    for binary in binaries:
        path = Path(binary).resolve(strict=True)
        key = os.path.relpath(path, Path(destination).resolve().parent)
        require(key not in binary_hashes, "duplicate binary artifact")
        hasher = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(chunk)
        binary_hashes[key] = hasher.hexdigest()
    require(binary_hashes, "at least one tested binary is required")
    return {"schemaVersion": 1, "kind": "ahoi-sync-merge-run",
            "implementation": document.get("implementation"), "runId": document.get("runId"),
            "fixtureSha256": fixture_hash, "outputSha256": sha256(output_raw),
            "sourceCommit": commit, "sourceTreeClean": clean,
            "completed": True, "exitCode": exit_code, "binaryArtifacts": binary_hashes}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "fixture", "source-tree", "receipt"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", required=True)
    parser.add_argument("--exit-code", type=int, required=True)
    args = parser.parse_args(argv)
    try:
        value = receipt(args.output, args.fixture, args.source_tree, args.binary,
                        args.receipt, args.exit_code)
        with args.receipt.open("x", encoding="utf-8") as stream:
            stream.write(json_text(value) + "\n")
    except (EvidenceError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"receipt not written: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
