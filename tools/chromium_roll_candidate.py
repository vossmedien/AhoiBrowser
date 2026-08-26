"""Reviewed-candidate policy and bindings for Chromium Stable rolls."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import tempfile
from typing import Any, Mapping

from chromium_roll_discovery import DiscoveryError, normalize_timestamp, version_tuple
from chromium_roll_git import RollError, safe_relative
from verify_chromium_pin import (
    VerificationError,
    load_json,
    require_sha1,
    validate_config,
)


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RollError(f"candidate contains duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise RollError(f"candidate contains invalid JSON value: {value}")


def load_baseline(path: pathlib.Path) -> dict[str, Any]:
    try:
        baseline = load_json(path)
    except (OSError, VerificationError) as error:
        raise RollError(f"invalid Chromium baseline: {error}") from error
    if not isinstance(baseline, dict):
        raise RollError("Chromium baseline must be a JSON object")
    try:
        validate_config(baseline)
    except VerificationError as error:
        raise RollError(f"invalid Chromium baseline: {error}") from error
    return baseline


def _load_reviewed_candidate(
    path: pathlib.Path, baseline: Mapping[str, Any]
) -> tuple[dict[str, Any], bytes]:
    """Load a bounded discovery result with the production pin's exact schema."""

    if path.is_symlink() or not path.is_file():
        raise RollError("candidate input must be a regular non-symlink file")
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise RollError(f"could not read candidate input: {error}") from error
    if len(raw) > 64 * 1024:
        raise RollError("candidate input exceeds the 64 KiB limit")
    try:
        candidate = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RollError("candidate input is not valid UTF-8 JSON") from error
    if not isinstance(candidate, dict):
        raise RollError("candidate input must be a JSON object")
    if set(candidate) != set(baseline):
        missing = sorted(set(baseline) - set(candidate))
        extra = sorted(set(candidate) - set(baseline))
        details = []
        if missing:
            details.append("missing=" + ",".join(missing))
        if extra:
            details.append("extra=" + ",".join(extra))
        raise RollError(
            "candidate schema differs from production pin: " + "; ".join(details)
        )
    if type(candidate.get("schemaVersion")) is not int or candidate["schemaVersion"] != 1:
        raise RollError("candidate schemaVersion must be 1")
    retrieved_at = candidate.get("retrievedAt")
    if not isinstance(retrieved_at, str):
        raise RollError("candidate retrievedAt must be an ISO-8601 UTC timestamp")
    try:
        validate_config(candidate)
        normalize_timestamp(retrieved_at)
    except (DiscoveryError, VerificationError) as error:
        raise RollError(f"invalid reviewed candidate: {error}") from error
    if version_tuple(candidate["version"]) <= version_tuple(baseline["version"]):
        raise RollError("reviewed candidate must be newer than the production pin")
    if candidate["commit"] == baseline["commit"]:
        raise RollError("reviewed candidate must not reuse the production commit")
    return candidate, raw


def _publish_candidate_binding(
    repository: pathlib.Path, candidate: Mapping[str, Any]
) -> tuple[pathlib.Path, str]:
    """Atomically replace only the canonical non-production candidate binding."""

    config_root = repository / "config"
    if config_root.is_symlink() or not config_root.is_dir():
        raise RollError("repository config directory must be a regular directory")
    destination = config_root / "upstream-roll-candidate.json"
    if destination.is_symlink() or (
        destination.exists() and not destination.is_file()
    ):
        raise RollError("candidate binding destination is not a regular file")
    encoded = (
        json.dumps(candidate, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".tmp", dir=config_root
    )
    temporary_path = pathlib.Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, destination)
        directory_fd = os.open(config_root, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        temporary_path.unlink(missing_ok=True)
    return destination, hashlib.sha256(encoded).hexdigest()


def promote_candidate(args: argparse.Namespace) -> dict[str, Any]:
    """Promote an explicitly reviewed discovery file to the candidate binding."""

    repository = args.repository.resolve()
    if not repository.is_dir():
        raise RollError("repository root does not exist")
    production_path = safe_relative(
        repository / "config/chromium.json", repository, "production config"
    )
    baseline = load_baseline(production_path)
    candidate_path = args.candidate.absolute()
    destination = repository / "config/upstream-roll-candidate.json"
    if candidate_path.resolve(strict=False) == destination.resolve(strict=False):
        raise RollError(
            "promotion requires a separate reviewed discovery file as input"
        )
    candidate, source_bytes = _load_reviewed_candidate(candidate_path, baseline)
    source_sha256 = hashlib.sha256(source_bytes).hexdigest()
    if (
        len(args.accept_sha256) != 64
        or any(character not in "0123456789abcdef" for character in args.accept_sha256)
    ):
        raise RollError("accepted candidate SHA-256 must be lowercase hexadecimal")
    if args.accept_sha256 != source_sha256:
        raise RollError("accepted SHA-256 does not match the reviewed candidate file")
    if args.accept_version != candidate["version"]:
        raise RollError("accepted version does not match the reviewed candidate")
    try:
        accepted_commit = require_sha1(args.accept_commit, "accepted commit")
    except VerificationError as error:
        raise RollError(str(error)) from error
    if accepted_commit != candidate["commit"]:
        raise RollError("accepted commit does not match the reviewed candidate")
    destination, binding_sha256 = _publish_candidate_binding(repository, candidate)
    return {
        "schemaVersion": 1,
        "command": "promote-candidate",
        "productionPinChanged": False,
        "candidate": {
            "version": candidate["version"],
            "commit": candidate["commit"],
            "sourceSha256": source_sha256,
            "bindingSha256": binding_sha256,
            "binding": destination.relative_to(repository).as_posix(),
        },
    }


def target_binding(repository: pathlib.Path, target: str) -> dict[str, Any]:
    bindings: list[dict[str, str]] = []
    for relative in (
        "config/chromium.json",
        "config/upstream-roll-candidate.json",
    ):
        path = safe_relative(repository / relative, repository, "target binding")
        if not path.is_file() or path.is_symlink():
            continue
        config = load_baseline(path)
        if config["commit"] == target:
            bindings.append(
                {
                    "config": relative,
                    "version": config["version"],
                    "tag": config["tag"],
                    "commit": config["commit"],
                }
            )
    if not bindings:
        raise RollError(
            "target is not bound by config/chromium.json or "
            "config/upstream-roll-candidate.json"
        )
    identities = {(item["version"], item["tag"]) for item in bindings}
    if len(identities) != 1:
        raise RollError("target binding configs disagree on version or tag")
    return {"verified": True, "configs": bindings}
