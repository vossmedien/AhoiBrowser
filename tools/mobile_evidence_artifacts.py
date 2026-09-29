"""Independent local artifact inspection for Mobile release evidence."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import plistlib
import subprocess
from typing import Any, Optional


class EvidenceInspectionError(RuntimeError):
    """An evidence artifact could not be independently inspected."""


def run_output(*args: str, cwd: Optional[pathlib.Path] = None) -> str:
    completed = subprocess.run(
        args,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def repository_state(root: pathlib.Path) -> tuple[str, bool]:
    try:
        commit = run_output("git", "rev-parse", "HEAD", cwd=root)
        status = run_output(
            "git", "status", "--porcelain", "--untracked-files=normal", cwd=root
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise EvidenceInspectionError(f"cannot inspect repository state: {error}")
    return commit, bool(status)


def sha256_path(path: pathlib.Path) -> str:
    """Hash a file or directory without following symlinks."""
    digest = hashlib.sha256()
    if path.is_symlink():
        raise EvidenceInspectionError(f"artifact root must not be a symlink: {path}")
    if path.is_file():
        digest.update(b"file\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    if not path.is_dir():
        raise EvidenceInspectionError(f"artifact does not exist: {path}")
    digest.update(b"tree-v1\0")
    for child in sorted(path.rglob("*"), key=lambda item: item.as_posix()):
        relative = child.relative_to(path).as_posix().encode("utf-8")
        if child.is_symlink():
            digest.update(b"link\0" + relative + b"\0")
            digest.update(os.readlink(child).encode("utf-8") + b"\0")
        elif child.is_dir():
            digest.update(b"dir\0" + relative + b"\0")
        elif child.is_file():
            digest.update(b"file\0" + relative + b"\0")
            with child.open("rb") as handle:
                for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(chunk)
        else:
            raise EvidenceInspectionError(f"unsupported artifact entry: {child}")
    return digest.hexdigest()


def read_plist(path: pathlib.Path) -> dict[str, Any]:
    try:
        with path.open("rb") as handle:
            value = plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException) as error:
        raise EvidenceInspectionError(f"cannot read plist {path}: {error}")
    if not isinstance(value, dict):
        raise EvidenceInspectionError(f"plist root must be an object: {path}")
    return value


def inspect_archive(path: pathlib.Path) -> dict[str, str]:
    if path.suffix != ".xcarchive" or not path.is_dir():
        raise EvidenceInspectionError("archive must be an existing .xcarchive")
    apps = sorted((path / "Products/Applications").glob("*.app"))
    if len(apps) != 1:
        raise EvidenceInspectionError("archive must contain exactly one iOS app")
    app = apps[0]
    info = read_plist(app / "Info.plist")
    archive_info = read_plist(path / "Info.plist")
    properties = archive_info.get("ApplicationProperties", {})
    if not isinstance(properties, dict):
        properties = {}
    team_id = str(properties.get("Team", ""))
    if not team_id:
        try:
            description = subprocess.run(
                ["codesign", "-d", "--verbose=4", str(app)], check=True,
                capture_output=True, text=True,
            ).stderr
        except (FileNotFoundError, subprocess.CalledProcessError) as error:
            raise EvidenceInspectionError(f"cannot inspect archive signature: {error}")
        for line in description.splitlines():
            if line.startswith("TeamIdentifier="):
                team_id = line.partition("=")[2].strip()
                break
    return {
        "bundleId": str(info.get("CFBundleIdentifier", "")),
        "teamId": team_id,
        "marketingVersion": str(info.get("CFBundleShortVersionString", "")),
        "buildNumber": str(info.get("CFBundleVersion", "")),
        "sourceCommit": str(info.get("AhoiSourceCommit", "")),
        "configuration": str(info.get("AhoiBuildMode", "")),
    }


def inspect_xcresult(path: pathlib.Path) -> dict[str, Any]:
    if path.suffix != ".xcresult" or not path.is_dir():
        raise EvidenceInspectionError("test result must be an existing .xcresult")
    try:
        raw = run_output(
            "xcrun", "xcresulttool", "get", "test-results", "summary",
            "--path", str(path), "--compact",
        )
        summary = json.loads(raw)
    except (
        FileNotFoundError,
        subprocess.CalledProcessError,
        UnicodeDecodeError,
        json.JSONDecodeError,
    ) as error:
        raise EvidenceInspectionError(f"cannot inspect xcresult {path}: {error}")
    if not isinstance(summary, dict):
        raise EvidenceInspectionError("xcresult summary root must be an object")
    return summary
