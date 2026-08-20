#!/usr/bin/env python3
"""Verify a Chromium release pin against captured official-source responses."""

import argparse
import base64
import binascii
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import urllib.parse
from typing import Any, Dict, List, Mapping, Sequence


SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)$")
OFFICIAL_SOURCE = "https://chromium.googlesource.com/chromium/src.git"
VERSION_HISTORY_HOST = "versionhistory.googleapis.com"
VERSION_HISTORY_PATH = (
    "/v1/chrome/platforms/mac_arm64/channels/stable/versions/all/releases"
)


class VerificationError(ValueError):
    """The configured pin is not proven by the supplied official responses."""


def load_json(path: pathlib.Path) -> Any:
    try:
        raw = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as error:
        raise VerificationError(f"invalid UTF-8 in {path}") from error
    if raw.startswith(")]}'"):
        raw = raw.split("\n", 1)[1] if "\n" in raw else ""
    try:
        return json.loads(raw)
    except json.JSONDecodeError as error:
        raise VerificationError(f"invalid JSON in {path}: {error}") from error


def require_sha1(value: Any, name: str) -> str:
    if not isinstance(value, str) or not SHA1_RE.fullmatch(value):
        raise VerificationError(f"{name} must be a lowercase 40-character Git object ID")
    return value


def require_positive_int(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise VerificationError(f"{name} must be a positive integer")
    return value


def validate_config(config: Mapping[str, Any]) -> None:
    version = config.get("version")
    match = VERSION_RE.fullmatch(version) if isinstance(version, str) else None
    if match is None:
        raise VerificationError("version must use the four-part Chromium version format")

    milestone = require_positive_int(config.get("milestone"), "milestone")
    branch_head = require_positive_int(config.get("branchHead"), "branchHead")
    require_positive_int(config.get("branchHeadPosition"), "branchHeadPosition")
    require_positive_int(config.get("branchPosition"), "branchPosition")
    require_sha1(config.get("commit"), "commit")
    require_sha1(config.get("branchPoint"), "branchPoint")

    if milestone != int(match.group(1)):
        raise VerificationError("milestone does not match the version major component")
    if branch_head != int(match.group(3)):
        raise VerificationError("branchHead does not match the version build component")

    expected_tag = f"refs/tags/{version}"
    if config.get("tag") != expected_tag:
        raise VerificationError(f"tag must be exactly {expected_tag}")
    if config.get("source") != OFFICIAL_SOURCE:
        raise VerificationError(
            f"source must be the official Chromium repository: {OFFICIAL_SOURCE}"
        )
    if config.get("channel") != "Stable" or config.get("platform") != "Mac":
        raise VerificationError("this production pin must target Chromium Stable for Mac")
    if config.get("verifiedVersionFile") != "chrome/VERSION":
        raise VerificationError("verifiedVersionFile must be chrome/VERSION")
    if config.get("pinnable") is not True or config.get("rolloutFraction") != 1.0:
        raise VerificationError(
            "the configured production release must be fully rolled and pinnable"
        )

    release_url = config.get("releaseApi")
    if not isinstance(release_url, str):
        raise VerificationError("releaseApi must be an HTTPS URL")
    parsed = urllib.parse.urlparse(release_url)
    if (
        parsed.scheme != "https"
        or parsed.hostname != VERSION_HISTORY_HOST
        or parsed.username is not None
        or parsed.password is not None
        or parsed.port is not None
        or parsed.path != VERSION_HISTORY_PATH
        or parsed.fragment
    ):
        raise VerificationError("releaseApi must target the official Mac ARM64 Stable endpoint")
    query = urllib.parse.parse_qs(parsed.query, strict_parsing=True)
    if query.get("filter") != ["endtime=none"]:
        raise VerificationError("releaseApi must request only active releases")


def parse_ls_remote(text: str) -> Dict[str, str]:
    refs: Dict[str, str] = {}
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 2 or not SHA1_RE.fullmatch(fields[0]):
            raise VerificationError(f"malformed ls-remote response at line {line_number}")
        object_id, ref = fields
        if ref in refs:
            raise VerificationError(f"duplicate ref in ls-remote response: {ref}")
        refs[ref] = object_id
    return refs


def verify_remote_refs(config: Mapping[str, Any], text: str) -> None:
    refs = parse_ls_remote(text)
    tag = str(config["tag"])
    peeled_tag = f"{tag}^{{}}"
    branch = f"refs/branch-heads/{config['branchHead']}"
    allowed = {tag, peeled_tag, branch}
    unexpected = sorted(set(refs) - allowed)
    if unexpected:
        raise VerificationError(f"unexpected refs in ls-remote response: {', '.join(unexpected)}")
    if tag not in refs:
        raise VerificationError(f"official repository does not contain {tag}")
    if branch not in refs:
        raise VerificationError(f"official repository does not contain {branch}")

    # Lightweight tags point directly at the commit. Annotated tags point at a
    # tag object and ls-remote publishes the recursively peeled commit as ^{}.
    target = refs.get(peeled_tag, refs[tag])
    if target != config["commit"]:
        raise VerificationError(
            f"tag target mismatch: {tag} resolves to {target}, expected {config['commit']}"
        )


def resolve_remote_refs(
    config: Mapping[str, Any], output_path: pathlib.Path, timeout: int
) -> None:
    if timeout <= 0:
        raise VerificationError("network timeout must be positive")
    tag = str(config["tag"])
    command = [
        "git",
        "-c",
        "http.lowSpeedLimit=1",
        "-c",
        "http.lowSpeedTime=30",
        "ls-remote",
        str(config["source"]),
        tag,
        f"{tag}^{{}}",
        f"refs/branch-heads/{config['branchHead']}",
    ]
    environment = dict(os.environ)
    environment["GIT_TERMINAL_PROMPT"] = "0"
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise VerificationError(
            f"official Git ref lookup timed out after {timeout} seconds"
        ) from error
    if process.returncode != 0:
        detail = stderr.decode("utf-8", "replace").strip()
        raise VerificationError(
            f"official Git ref lookup failed with exit {process.returncode}: {detail}"
        )
    try:
        rendered = stdout.decode("utf-8")
    except UnicodeDecodeError as error:
        raise VerificationError("official Git ref lookup returned invalid UTF-8") from error
    verify_remote_refs(config, rendered)
    output_path.write_text(rendered, encoding="utf-8")


def resolve_gitiles_refs(
    config: Mapping[str, Any],
    tag_ref_path: pathlib.Path,
    branch_ref_path: pathlib.Path,
    output_path: pathlib.Path,
) -> None:
    tag_payload = load_json(tag_ref_path)
    branch_payload = load_json(branch_ref_path)
    if not isinstance(tag_payload, dict) or not isinstance(branch_payload, dict):
        raise VerificationError("Gitiles ref responses must be JSON objects")
    tag_commit = require_sha1(tag_payload.get("commit"), "Gitiles tag target")
    branch_commit = require_sha1(
        branch_payload.get("commit"), "Gitiles branch-head target"
    )
    rendered = "".join(
        (
            f"{tag_commit}\t{config['tag']}\n",
            f"{branch_commit}\trefs/branch-heads/{config['branchHead']}\n",
        )
    )
    verify_remote_refs(config, rendered)
    output_path.write_text(rendered, encoding="utf-8")


def exact_footer_values(message: Any, footer: str) -> List[str]:
    if not isinstance(message, str):
        raise VerificationError("Gitiles commit response has no textual message")
    prefix = f"{footer}: "
    return [line[len(prefix) :] for line in message.splitlines() if line.startswith(prefix)]


def require_exact_single_footer(message: Any, footer: str, expected: str) -> None:
    values = exact_footer_values(message, footer)
    if values != [expected]:
        rendered = ", ".join(values) if values else "missing"
        raise VerificationError(
            f"{footer} mismatch: expected exactly {expected}, got {rendered}"
        )


def verify_commit_metadata(
    config: Mapping[str, Any],
    commit_payload: Mapping[str, Any],
    branch_point_payload: Mapping[str, Any],
) -> None:
    commit = require_sha1(commit_payload.get("commit"), "Gitiles release commit")
    if commit != config["commit"]:
        raise VerificationError(
            f"Gitiles release commit mismatch: got {commit}, expected {config['commit']}"
        )
    require_exact_single_footer(
        commit_payload.get("message"),
        "Cr-Commit-Position",
        f"refs/branch-heads/{config['branchHead']}@{{#{config['branchHeadPosition']}}}",
    )
    branched_from = (
        f"{config['branchPoint']}-refs/heads/main@{{#{config['branchPosition']}}}"
    )
    branched_from_values = exact_footer_values(
        commit_payload.get("message"), "Cr-Branched-From"
    )
    if branched_from_values.count(branched_from) != 1:
        raise VerificationError(
            "Cr-Branched-From does not bind the release commit to the configured "
            f"branch point and position: {branched_from}"
        )

    branch_point = require_sha1(
        branch_point_payload.get("commit"), "Gitiles branch-point commit"
    )
    if branch_point != config["branchPoint"]:
        raise VerificationError(
            f"Gitiles branch-point mismatch: got {branch_point}, expected {config['branchPoint']}"
        )
    require_exact_single_footer(
        branch_point_payload.get("message"),
        "Cr-Commit-Position",
        f"refs/heads/main@{{#{config['branchPosition']}}}",
    )


def verify_version_file(config: Mapping[str, Any], encoded: bytes) -> None:
    try:
        compact = b"".join(encoded.split())
        decoded = base64.b64decode(compact, validate=True).decode("utf-8")
    except (binascii.Error, UnicodeDecodeError) as error:
        raise VerificationError(
            "official chrome/VERSION response is not valid base64 UTF-8"
        ) from error
    values: Dict[str, str] = {}
    for line in decoded.splitlines():
        if not line:
            continue
        if line.count("=") != 1:
            raise VerificationError("official chrome/VERSION contains a malformed line")
        key, value = line.split("=", 1)
        if key in values:
            raise VerificationError(f"official chrome/VERSION contains duplicate key {key}")
        values[key] = value
    required = ("MAJOR", "MINOR", "BUILD", "PATCH")
    if any(key not in values or not values[key].isdigit() for key in required):
        raise VerificationError("official chrome/VERSION is missing a numeric version component")
    actual = ".".join(values[key] for key in required)
    if actual != config["version"]:
        raise VerificationError(
            f"remote version mismatch: expected {config['version']}, got {actual}"
        )


def verify_release(config: Mapping[str, Any], payload: Mapping[str, Any]) -> None:
    releases = payload.get("releases")
    if not isinstance(releases, list):
        raise VerificationError("VersionHistory response has no releases array")
    matches = [
        release
        for release in releases
        if isinstance(release, dict) and release.get("version") == config["version"]
    ]
    if len(matches) != 1:
        raise VerificationError(
            f"expected one active release record for {config['version']}, found {len(matches)}"
        )
    release = matches[0]
    if (
        release.get("fraction") != config["rolloutFraction"]
        or release.get("pinnable") is not config["pinnable"]
    ):
        raise VerificationError("pinned version is not fully rolled and pinnable")


def verify(args: argparse.Namespace) -> None:
    config = load_json(args.config)
    if not isinstance(config, dict):
        raise VerificationError("Chromium pin config must be a JSON object")
    validate_config(config)
    if args.validate_config_only:
        return
    if args.resolve_remote_refs is not None:
        resolve_remote_refs(config, args.resolve_remote_refs, args.network_timeout)
        return
    if args.resolve_gitiles_refs is not None:
        if args.tag_ref_json is None or args.branch_ref_json is None:
            raise VerificationError(
                "Gitiles ref resolution requires tag and branch response files"
            )
        resolve_gitiles_refs(
            config,
            args.tag_ref_json,
            args.branch_ref_json,
            args.resolve_gitiles_refs,
        )
        return
    required_paths = (
        args.remote_refs,
        args.commit_json,
        args.branch_point_json,
        args.version_text,
        args.release_json,
    )
    if any(path is None for path in required_paths):
        raise VerificationError("all official response files are required for full verification")
    verify_remote_refs(config, args.remote_refs.read_text(encoding="utf-8"))

    commit_payload = load_json(args.commit_json)
    branch_point_payload = load_json(args.branch_point_json)
    release_payload = load_json(args.release_json)
    if not all(
        isinstance(item, dict)
        for item in (commit_payload, branch_point_payload, release_payload)
    ):
        raise VerificationError("official JSON responses must be JSON objects")
    verify_commit_metadata(config, commit_payload, branch_point_payload)
    verify_version_file(config, args.version_text.read_bytes())
    verify_release(config, release_payload)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=pathlib.Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--validate-config-only", action="store_true")
    mode.add_argument("--resolve-remote-refs", type=pathlib.Path)
    mode.add_argument("--resolve-gitiles-refs", type=pathlib.Path)
    parser.add_argument("--network-timeout", type=int, default=90)
    parser.add_argument("--tag-ref-json", type=pathlib.Path)
    parser.add_argument("--branch-ref-json", type=pathlib.Path)
    parser.add_argument("--remote-refs", type=pathlib.Path)
    parser.add_argument("--commit-json", type=pathlib.Path)
    parser.add_argument("--branch-point-json", type=pathlib.Path)
    parser.add_argument("--version-text", type=pathlib.Path)
    parser.add_argument("--release-json", type=pathlib.Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    try:
        verify(parse_args(argv))
    except (OSError, VerificationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
