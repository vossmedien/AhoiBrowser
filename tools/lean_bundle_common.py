"""Shared helpers of the Lean Chromium bundle measurement: hashing, git,
JSON, field validation and repository paths."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import subprocess
from typing import Any

from chromium_output import configured_output_root, profile_output_directory


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "config/lean-bundle-measurement.json"
MACH_O_MAGICS = {
    bytes.fromhex(value)
    for value in (
        "feedface",
        "cefaedfe",
        "feedfacf",
        "cffaedfe",
        "cafebabe",
        "bebafeca",
        "cafebabf",
        "bfbafeca",
    )
}
CATEGORY_IDS = ("resources", "frameworks-and-libraries", "mach-o")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
EXPECTED_RECEIPT_BINDINGS = {
    "upstream-control": (
        "artifacts/build/upstream-build.json",
        "unmodified-upstream-control",
    ),
    "ahoi-full-release": (
        "artifacts/build/ahoi-full-release-build.json",
        "ahoi-full-release",
    ),
    "ahoi-release": (
        "artifacts/build/ahoi-release-build.json",
        "ahoi-release",
    ),
}
BUILD_TOOL_KEYS = ("gn", "ninja", "siso", "clang", "lld")
EXPECTED_OUTPUT_PROFILES = {
    "upstream-control": "AhoiUpstreamRelease",
    "ahoi-full-release": "AhoiFullRelease",
    "ahoi-release": "AhoiRelease",
}


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def git_output(*args: str, cwd: pathlib.Path) -> str:
    try:
        return subprocess.run(
            args,
            cwd=cwd,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"cannot inspect repository identity: {error}") from error


def require_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SystemExit(f"{field} must be an object")
    return value


def require_nonempty_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise SystemExit(f"{field} must be a non-empty string")
    return value


def require_sha256(value: Any, field: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise SystemExit(f"{field} must be a lowercase SHA-256")
    return value


def logical_repo_path(relative: pathlib.PurePosixPath) -> str:
    return f"<repo>/{relative.as_posix()}"


def provenance_logical_path(path: pathlib.Path, work_root: pathlib.Path) -> str:
    resolved = path.resolve()
    for root, label in (
        (ROOT.resolve(), "<repo>"),
        (work_root.resolve(), "<work-root>"),
        (configured_output_root(work_root / "chromium/src"), "<chromium-out>"),
    ):
        try:
            relative = resolved.relative_to(root)
        except ValueError:
            continue
        return label if str(relative) == "." else f"{label}/{relative.as_posix()}"
    raise SystemExit(f"path is outside the provenance roots: {resolved}")


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit(f"cannot read JSON {path}: {error}") from error
    if not isinstance(payload, dict):
        raise SystemExit(f"JSON root must be an object: {path}")
    return payload


def configured_work_root() -> pathlib.Path:
    raw = os.environ.get("AHOI_WORK_ROOT")
    work_root = pathlib.Path(raw) if raw else ROOT / ".work"
    if not work_root.is_absolute():
        raise SystemExit("AHOI_WORK_ROOT must be absolute")
    return work_root.resolve()


def relative_path(value: Any, field: str) -> pathlib.PurePosixPath:
    if not isinstance(value, str) or not value:
        raise SystemExit(f"{field} must be a non-empty relative POSIX path")
    parsed = pathlib.PurePosixPath(value)
    if parsed.is_absolute() or ".." in parsed.parts or "." in parsed.parts:
        raise SystemExit(f"{field} must not be absolute or contain dot components")
    return parsed


def resolve_beneath(
    root: pathlib.Path, value: Any, field: str, *, kind: str
) -> pathlib.Path:
    rel_path = relative_path(value, field)
    candidate = root.joinpath(*rel_path.parts)
    if candidate.is_symlink():
        raise SystemExit(f"{field} must not be a symlink: {value}")
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise SystemExit(f"{field} escapes its configured root: {value}") from error
    if kind == "file" and not resolved.is_file():
        raise SystemExit(f"{field} is not a regular file: {value}")
    if kind == "directory" and not resolved.is_dir():
        raise SystemExit(f"{field} is not a directory: {value}")
    return resolved


def resolve_output_artifact(
    work_root: pathlib.Path, value: Any, field: str, *, kind: str
) -> pathlib.Path:
    relative = relative_path(value, field)
    if len(relative.parts) < 5 or relative.parts[:3] != ("chromium", "src", "out"):
        raise SystemExit(f"{field} must name an artifact under Chromium out")
    output = profile_output_directory(work_root / "chromium/src", relative.parts[3])
    return resolve_beneath(
        output, pathlib.PurePosixPath(*relative.parts[4:]).as_posix(),
        f"{field} at {output}", kind=kind
    )
