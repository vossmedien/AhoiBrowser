#!/usr/bin/env python3
"""Create and verify an exact, clean-source Mobile simulator candidate receipt."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import plistlib
import re
import stat
import subprocess
import sys
from typing import Any, Optional, Sequence

from mobile_evidence_artifacts import (
    EvidenceInspectionError,
    read_plist,
    repository_state,
    run_output,
    sha256_path,
)


SCHEMA_VERSION = 1
RECEIPT_KIND = "simulator-candidate-binding"
COMMIT_RE = re.compile(r"[0-9a-f]{40}")


class CandidateReceiptError(RuntimeError):
    """The candidate cannot be bound to a trustworthy local receipt."""


def absolute_path(path: pathlib.Path) -> pathlib.Path:
    """Make a path absolute without resolving a possibly hostile symlink."""
    return pathlib.Path(os.path.abspath(os.fspath(path)))


def reject_symlink_components(path: pathlib.Path, label: str) -> pathlib.Path:
    absolute = absolute_path(path)
    current = absolute
    while True:
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            pass
        except OSError as error:
            raise CandidateReceiptError(f"cannot inspect {label}: {error}") from error
        else:
            if stat.S_ISLNK(mode):
                raise CandidateReceiptError(f"{label} must not contain symlink components")
        if current.parent == current:
            break
        current = current.parent
    return absolute


def require_file(path: pathlib.Path, label: str) -> pathlib.Path:
    absolute = reject_symlink_components(path, label)
    if not absolute.is_file():
        raise CandidateReceiptError(f"{label} must be an existing regular file")
    return absolute


def require_directory(path: pathlib.Path, label: str) -> pathlib.Path:
    absolute = reject_symlink_components(path, label)
    if not absolute.is_dir():
        raise CandidateReceiptError(f"{label} must be an existing directory")
    return absolute


def reject_nested_symlinks(path: pathlib.Path, label: str) -> None:
    if not path.is_dir():
        return
    try:
        for child in path.rglob("*"):
            if child.is_symlink():
                raise CandidateReceiptError(
                    f"{label} must not contain symlinks: {child.relative_to(path)}"
                )
    except OSError as error:
        raise CandidateReceiptError(f"cannot inspect {label}: {error}") from error


def require_repository_root(path: pathlib.Path) -> pathlib.Path:
    root = require_directory(path, "repository root")
    try:
        reported = absolute_path(
            pathlib.Path(run_output("git", "rev-parse", "--show-toplevel", cwd=root))
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise CandidateReceiptError(f"cannot resolve repository root: {error}") from error
    if reported != root:
        raise CandidateReceiptError("--repo-root must name the Git worktree root exactly")
    return root


def required_plist_string(info: dict[str, Any], key: str) -> str:
    value = info.get(key)
    if not isinstance(value, str) or not value.strip():
        raise CandidateReceiptError(f"Info.plist {key} must be a non-empty string")
    return value.strip()


def inspect_signature(app: pathlib.Path, bundle_id: str) -> dict[str, Any]:
    try:
        description = subprocess.run(
            ["codesign", "-d", "--verbose=4", str(app)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise CandidateReceiptError(f"cannot run codesign: {error}") from error

    combined = "\n".join((description.stdout, description.stderr))
    if description.returncode != 0:
        if "not signed at all" not in combined.lower():
            raise CandidateReceiptError("cannot inspect the candidate code signature")
        return {
            "kind": "unsigned",
            "valid": False,
            "identifier": None,
            "teamId": None,
            "distributionEligible": False,
        }

    fields: dict[str, str] = {}
    for line in combined.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            fields[key.strip()] = value.strip()
    identifier = fields.get("Identifier") or None
    if identifier is None:
        raise CandidateReceiptError("signed candidate has no code-signing identifier")
    if identifier != bundle_id:
        raise CandidateReceiptError(
            "code-signing identifier does not match CFBundleIdentifier"
        )
    raw_team_id = fields.get("TeamIdentifier", "")
    team_id = raw_team_id if raw_team_id and raw_team_id != "not set" else None

    try:
        verification = subprocess.run(
            ["codesign", "--verify", "--deep", "--strict", str(app)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise CandidateReceiptError(f"cannot verify code signature: {error}") from error
    if verification.returncode != 0:
        raise CandidateReceiptError("candidate code signature does not verify")
    signature_kind = "team" if team_id else "adHoc"
    return {
        "kind": signature_kind,
        "valid": True,
        "identifier": identifier,
        "teamId": team_id,
        # An iphonesimulator app is never App Store/TestFlight proof, even if a
        # local signature happens to expose a development-team identifier.
        "distributionEligible": False,
    }


def inspect_toolchain() -> dict[str, str]:
    try:
        xcode_version = run_output("xcodebuild", "-version")
        swift_version = run_output("xcrun", "swift", "--version")
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise CandidateReceiptError(f"cannot inspect Apple toolchain: {error}") from error
    if not xcode_version or not swift_version:
        raise CandidateReceiptError("Apple toolchain versions must not be empty")
    return {
        "xcodeVersion": xcode_version,
        "swiftVersion": swift_version,
    }


def optional_artifact_hashes(
    xcode_project: Optional[pathlib.Path],
    xctestrun: Optional[pathlib.Path],
) -> dict[str, str]:
    hashes: dict[str, str] = {}
    if xcode_project is not None:
        project = require_directory(xcode_project, "Xcode project")
        if project.suffix != ".xcodeproj":
            raise CandidateReceiptError("Xcode project must end in .xcodeproj")
        reject_nested_symlinks(project, "Xcode project")
        hashes["xcodeProjectSha256"] = sha256_path(project)
    if xctestrun is not None:
        test_run = require_file(xctestrun, "xctestrun")
        if test_run.suffix != ".xctestrun":
            raise CandidateReceiptError("xctestrun must end in .xctestrun")
        hashes["xctestrunSha256"] = sha256_path(test_run)
    return hashes


def build_receipt(
    repo_root: pathlib.Path,
    app_path: pathlib.Path,
    *,
    xcode_project: Optional[pathlib.Path] = None,
    xctestrun: Optional[pathlib.Path] = None,
) -> dict[str, Any]:
    root = require_repository_root(repo_root)
    app = require_directory(app_path, "app bundle")
    if app.suffix != ".app":
        raise CandidateReceiptError("app bundle must end in .app")
    reject_nested_symlinks(app, "app bundle")

    try:
        head, source_dirty = repository_state(root)
    except EvidenceInspectionError as error:
        raise CandidateReceiptError(str(error)) from error
    if source_dirty:
        raise CandidateReceiptError(
            "repository source is dirty; release receipts require sourceDirty=false"
        )
    if COMMIT_RE.fullmatch(head) is None:
        raise CandidateReceiptError("Git HEAD must be a lowercase 40-character commit SHA")

    info_path = require_file(app / "Info.plist", "app Info.plist")
    try:
        info = read_plist(info_path)
    except EvidenceInspectionError as error:
        raise CandidateReceiptError(str(error)) from error
    bundle_id = required_plist_string(info, "CFBundleIdentifier")
    executable_name = required_plist_string(info, "CFBundleExecutable")
    if pathlib.PurePath(executable_name).name != executable_name:
        raise CandidateReceiptError("CFBundleExecutable must be a single file name")
    binary = require_file(app / executable_name, "app executable")
    embedded_commit = required_plist_string(info, "AhoiSourceCommit")
    if embedded_commit != head:
        raise CandidateReceiptError(
            "Info.plist AhoiSourceCommit must equal the exact clean Git HEAD"
        )
    platform_name = required_plist_string(info, "DTPlatformName")
    if platform_name != "iphonesimulator":
        raise CandidateReceiptError("candidate must be an iphonesimulator app")

    inspected_signature = inspect_signature(app, bundle_id)
    required_signature_keys = {
        "kind", "valid", "identifier", "teamId", "distributionEligible"
    }
    if inspected_signature.keys() != required_signature_keys:
        raise CandidateReceiptError("code-signing inspection returned an invalid schema")
    signing_kind = inspected_signature.get("kind")
    signing_valid = inspected_signature.get("valid")
    team_id = inspected_signature.get("teamId")
    if team_id is not None and (
        not isinstance(team_id, str)
        or re.fullmatch(r"[A-Z0-9]{10}", team_id) is None
    ):
        raise CandidateReceiptError("code-signing TeamIdentifier is malformed")
    if inspected_signature.get("distributionEligible") is not False:
        raise CandidateReceiptError(
            "simulator candidate must never claim distribution eligibility"
        )
    if signing_kind not in {"unsigned", "adHoc", "team"}:
        raise CandidateReceiptError("candidate code-signing kind is invalid")
    if type(signing_valid) is not bool:
        raise CandidateReceiptError("candidate signature validity must be Boolean")
    if signing_kind == "unsigned":
        if signing_valid or team_id is not None or inspected_signature.get("identifier"):
            raise CandidateReceiptError("unsigned signature evidence is inconsistent")
    else:
        if not signing_valid or inspected_signature.get("identifier") != bundle_id:
            raise CandidateReceiptError("signed candidate identity is inconsistent")
        if (signing_kind == "team") != (team_id is not None):
            raise CandidateReceiptError("candidate TeamIdentifier evidence is inconsistent")
    signature = {
        key: value
        for key, value in inspected_signature.items()
        if key != "teamId"
    }
    try:
        hashes = {
            "appTreeSha256": sha256_path(app),
            "binarySha256": sha256_path(binary),
            "infoPlistSha256": sha256_path(info_path),
            **optional_artifact_hashes(xcode_project, xctestrun),
        }
    except EvidenceInspectionError as error:
        raise CandidateReceiptError(str(error)) from error

    return {
        "schemaVersion": SCHEMA_VERSION,
        "kind": RECEIPT_KIND,
        "sourceCommit": head,
        "sourceDirty": False,
        "bundleId": bundle_id,
        "teamId": team_id,
        "marketingVersion": required_plist_string(
            info, "CFBundleShortVersionString"
        ),
        "buildNumber": required_plist_string(info, "CFBundleVersion"),
        "buildMode": required_plist_string(info, "AhoiBuildMode"),
        "embeddedSourceCommit": embedded_commit,
        "platform": platform_name,
        "signing": signature,
        "hashes": hashes,
        "toolchain": inspect_toolchain(),
    }


def read_receipt(path: pathlib.Path) -> dict[str, Any]:
    receipt_path = require_file(path, "receipt")
    try:
        value = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CandidateReceiptError(f"cannot read receipt JSON: {error}") from error
    if not isinstance(value, dict):
        raise CandidateReceiptError("receipt JSON root must be an object")
    return value


def first_difference(expected: Any, actual: Any, label: str = "receipt") -> str:
    if type(expected) is not type(actual):
        return f"{label} has the wrong value type"
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return f"{label} fields do not match the current candidate"
        for key in expected:
            difference = first_difference(expected[key], actual[key], f"{label}.{key}")
            if difference:
                return difference
        return ""
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{label} list length does not match the current candidate"
        for index, item in enumerate(expected):
            difference = first_difference(item, actual[index], f"{label}[{index}]")
            if difference:
                return difference
        return ""
    return "" if expected == actual else f"{label} does not match the current candidate"


def verify_receipt(
    receipt_path: pathlib.Path,
    repo_root: pathlib.Path,
    app_path: pathlib.Path,
    *,
    xcode_project: Optional[pathlib.Path] = None,
    xctestrun: Optional[pathlib.Path] = None,
) -> dict[str, Any]:
    recorded = read_receipt(receipt_path)
    current = build_receipt(
        repo_root,
        app_path,
        xcode_project=xcode_project,
        xctestrun=xctestrun,
    )
    difference = first_difference(recorded, current)
    if difference:
        raise CandidateReceiptError(difference)
    return recorded


def write_receipt(
    output_path: pathlib.Path,
    receipt: dict[str, Any],
    *,
    overwrite: bool = False,
) -> None:
    output = reject_symlink_components(output_path, "output")
    parent = require_directory(output.parent, "output directory")
    if output.exists() and not output.is_file():
        raise CandidateReceiptError("output must be a regular JSON file")
    if output.exists() and not overwrite:
        raise CandidateReceiptError("output already exists; pass --overwrite to replace it")
    if output.suffix != ".json":
        raise CandidateReceiptError("output must end in .json")
    payload = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    flags = os.O_WRONLY | os.O_CREAT
    flags |= os.O_TRUNC if overwrite else os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    directory_flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        directory_flags |= os.O_DIRECTORY
    if hasattr(os, "O_NOFOLLOW"):
        directory_flags |= os.O_NOFOLLOW
    try:
        directory_fd = os.open(parent, directory_flags)
        try:
            output_fd = os.open(output.name, flags, 0o600, dir_fd=directory_fd)
            with os.fdopen(output_fd, "w", encoding="utf-8") as handle:
                handle.write(payload)
        finally:
            os.close(directory_fd)
    except OSError as error:
        raise CandidateReceiptError(f"cannot write output receipt: {error}") from error


def add_candidate_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo-root", type=pathlib.Path, required=True)
    parser.add_argument("--app", type=pathlib.Path, required=True)
    parser.add_argument("--xcode-project", type=pathlib.Path)
    parser.add_argument("--xctestrun", type=pathlib.Path)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Bind a clean Git HEAD and an exact AhoiBrowser Mobile simulator .app "
            "to independently reproducible local evidence."
        )
    )
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="create a candidate receipt")
    add_candidate_arguments(create)
    create.add_argument("--output", type=pathlib.Path, required=True)
    create.add_argument("--overwrite", action="store_true")
    verify = commands.add_parser("verify", help="verify an existing candidate receipt")
    add_candidate_arguments(verify)
    verify.add_argument("--receipt", type=pathlib.Path, required=True)
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "create":
            receipt = build_receipt(
                args.repo_root,
                args.app,
                xcode_project=args.xcode_project,
                xctestrun=args.xctestrun,
            )
            write_receipt(args.output, receipt, overwrite=args.overwrite)
            print(json.dumps(receipt, sort_keys=True))
        else:
            receipt = verify_receipt(
                args.receipt,
                args.repo_root,
                args.app,
                xcode_project=args.xcode_project,
                xctestrun=args.xctestrun,
            )
            print(json.dumps(receipt, sort_keys=True))
    except (
        CandidateReceiptError,
        EvidenceInspectionError,
        plistlib.InvalidFileException,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
