#!/usr/bin/env python3
"""Verify that a Chromium worktree is exactly the deterministic Ahoi overlay."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
from typing import Any

from compose_overlay import compose_overlay, isolated_git_environment
from overlay_fingerprint import fingerprint as overlay_inputs_fingerprint


STATE_SCHEMA_VERSION = 2
STATE_FIELDS = frozenset(
    {
        "schemaVersion",
        "fingerprint",
        "checkoutDeltaFingerprint",
        "chromiumCommit",
        "appliedAt",
    }
)
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}\Z")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
TIMESTAMP_PATTERN = re.compile(
    r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?"
    r"(?:Z|[+-]\d{2}:\d{2})\Z"
)


class OverlayStateError(ValueError):
    """Raised when overlay inputs, checkout state, or cached state are invalid."""


@dataclasses.dataclass(frozen=True)
class ExpectedOverlay:
    chromium_commit: str
    input_fingerprint: str
    tree: str
    delta_fingerprint: str


@dataclasses.dataclass(frozen=True)
class VerifiedOverlayState:
    chromium_commit: str
    input_fingerprint: str
    checkout_delta_fingerprint: str
    expected_tree: str
    actual_tree: str
    applied_at: str


def _run_git(
    *args: str,
    checkout: pathlib.Path,
    env: dict[str, str] | None = None,
) -> str:
    effective_environment = isolated_git_environment() if env is None else env
    try:
        result = subprocess.run(
            ("git", *args),
            cwd=checkout,
            env=effective_environment,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise OverlayStateError(f"git {' '.join(args)} failed{suffix}") from error
    return result.stdout.strip()


def _require_commit(value: Any, *, label: str) -> str:
    if not isinstance(value, str) or COMMIT_PATTERN.fullmatch(value) is None:
        raise OverlayStateError(f"{label} must be a lowercase 40-character Git commit")
    return value


def _require_sha256(value: Any, *, label: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise OverlayStateError(f"{label} must be a lowercase SHA-256 digest")
    return value


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise OverlayStateError(f"overlay state contains duplicate key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str):
    raise OverlayStateError(f"overlay state contains invalid JSON value: {value}")


def load_overlay_state(path: pathlib.Path, expected_commit: str) -> dict[str, Any]:
    """Load the cache with an exact, versioned schema and commit binding."""

    expected_commit = _require_commit(expected_commit, label="expected Chromium commit")
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise OverlayStateError(f"overlay state is missing or unreadable: {path}") from error
    if len(raw) > 64 * 1024:
        raise OverlayStateError("overlay state exceeds the 64 KiB size limit")
    try:
        state = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise OverlayStateError("overlay state is not valid UTF-8 JSON") from error
    if not isinstance(state, dict):
        raise OverlayStateError("overlay state must be a JSON object")
    actual_fields = frozenset(state)
    if actual_fields != STATE_FIELDS:
        missing = sorted(STATE_FIELDS - actual_fields)
        extra = sorted(actual_fields - STATE_FIELDS)
        details = []
        if missing:
            details.append("missing=" + ",".join(missing))
        if extra:
            details.append("extra=" + ",".join(extra))
        raise OverlayStateError("overlay state schema mismatch: " + "; ".join(details))
    if type(state["schemaVersion"]) is not int or state["schemaVersion"] != STATE_SCHEMA_VERSION:
        raise OverlayStateError(
            f"overlay state schemaVersion must be {STATE_SCHEMA_VERSION}"
        )
    state_commit = _require_commit(
        state["chromiumCommit"], label="overlay state chromiumCommit"
    )
    if state_commit != expected_commit:
        raise OverlayStateError("overlay state Chromium commit does not match the pin")
    _require_sha256(state["fingerprint"], label="overlay state fingerprint")
    _require_sha256(
        state["checkoutDeltaFingerprint"],
        label="overlay state checkoutDeltaFingerprint",
    )
    applied_at = state["appliedAt"]
    if not isinstance(applied_at, str) or TIMESTAMP_PATTERN.fullmatch(applied_at) is None:
        raise OverlayStateError("overlay state appliedAt must be an ISO-8601 timestamp")
    try:
        parsed_applied_at = dt.datetime.fromisoformat(applied_at.replace("Z", "+00:00"))
    except ValueError as error:
        raise OverlayStateError("overlay state appliedAt is not a valid timestamp") from error
    if parsed_applied_at.tzinfo is None or parsed_applied_at.utcoffset() is None:
        raise OverlayStateError("overlay state appliedAt must include a UTC offset")
    return state


def delta_fingerprint(chromium_commit: str, tree: str) -> str:
    """Bind the semantic resulting Git tree to its exact Chromium base commit."""

    chromium_commit = _require_commit(
        chromium_commit, label="expected Chromium commit"
    )
    if re.fullmatch(r"[0-9a-f]{40}", tree) is None:
        raise OverlayStateError("overlay tree must be a lowercase Git SHA-1 object ID")
    digest = hashlib.sha256()
    digest.update(b"ahoi-checkout-delta-v2\0")
    digest.update(chromium_commit.encode("ascii"))
    digest.update(b"\0")
    digest.update(tree.encode("ascii"))
    digest.update(b"\0")
    return digest.hexdigest()


def derive_expected_overlay(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    expected_commit: str,
) -> ExpectedOverlay:
    """Recompose the expected tree solely from the pin and current source inputs."""

    repository = repository.resolve()
    checkout = checkout.resolve()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    if not (checkout / ".git").exists():
        raise OverlayStateError(f"not a Git checkout: {checkout}")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD does not match the configured pin")

    fingerprint_before = overlay_inputs_fingerprint(repository)
    try:
        _, expected_tree = compose_overlay(
            checkout,
            repository / "overlay/chromium/src",
            repository / "patches/chromium/series",
            repository / "patches/chromium",
            base_revision=expected_commit,
        )
    except (OSError, subprocess.CalledProcessError, SystemExit) as error:
        raise OverlayStateError(f"could not compose deterministic overlay: {error}") from error
    fingerprint_after = overlay_inputs_fingerprint(repository)
    if fingerprint_before != fingerprint_after:
        raise OverlayStateError("overlay inputs changed while they were being composed")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD changed during overlay composition")
    return ExpectedOverlay(
        chromium_commit=expected_commit,
        input_fingerprint=fingerprint_after,
        tree=expected_tree,
        delta_fingerprint=delta_fingerprint(expected_commit, expected_tree),
    )


def current_checkout_tree(checkout: pathlib.Path, expected_commit: str) -> str:
    """Stage the complete non-ignored worktree into an isolated index and return its tree."""

    checkout = checkout.resolve()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    if not (checkout / ".git").exists():
        raise OverlayStateError(f"not a Git checkout: {checkout}")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD does not match the configured pin")
    environment = isolated_git_environment()
    head_tree = _run_git(
        "rev-parse",
        f"{expected_commit}^{{tree}}",
        checkout=checkout,
        env=environment,
    )
    index_tree = _run_git("write-tree", checkout=checkout, env=environment)
    if index_tree != head_tree:
        raise OverlayStateError(
            "Chromium index must match the pinned HEAD before overlay verification"
        )
    index_value = _run_git(
        "rev-parse", "--git-path", "index", checkout=checkout, env=environment
    )
    index_path = pathlib.Path(index_value)
    if not index_path.is_absolute():
        index_path = checkout / index_path
    if not index_path.is_file():
        raise OverlayStateError(f"Chromium Git index is missing: {index_path}")
    with tempfile.TemporaryDirectory(prefix="ahoi-checkout-index-") as temp_root:
        temporary_index = pathlib.Path(temp_root) / "index"
        shutil.copyfile(index_path, temporary_index)
        environment["GIT_INDEX_FILE"] = str(temporary_index)
        _run_git(
            "-c",
            "core.fileMode=true",
            "-c",
            "core.symlinks=true",
            "add",
            "--all",
            "--",
            checkout=checkout,
            env=environment,
        )
        tree = _run_git("write-tree", checkout=checkout, env=environment)
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD changed while deriving its tree")
    return tree


def verify_overlay_state(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
) -> VerifiedOverlayState:
    """Verify source truth first, then validate the state as a disposable cache."""

    state = load_overlay_state(state_path, expected_commit)
    expected = derive_expected_overlay(repository, checkout, expected_commit)
    actual_tree = current_checkout_tree(checkout, expected_commit)
    if actual_tree != expected.tree:
        raise OverlayStateError(
            "Chromium checkout does not match the deterministic Ahoi overlay "
            "derived from the pinned commit and current inputs"
        )
    if state["fingerprint"] != expected.input_fingerprint:
        raise OverlayStateError("overlay state fingerprint does not match current inputs")
    if state["checkoutDeltaFingerprint"] != expected.delta_fingerprint:
        raise OverlayStateError(
            "overlay state checkoutDeltaFingerprint does not match the recomputed delta"
        )
    return VerifiedOverlayState(
        chromium_commit=expected.chromium_commit,
        input_fingerprint=expected.input_fingerprint,
        checkout_delta_fingerprint=expected.delta_fingerprint,
        expected_tree=expected.tree,
        actual_tree=actual_tree,
        applied_at=state["appliedAt"],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    expected_parser = subparsers.add_parser(
        "expected-fingerprint", help="print the freshly composed delta fingerprint"
    )
    verify_parser = subparsers.add_parser(
        "verify", help="verify checkout truth and its cached state"
    )
    for command_parser in (expected_parser, verify_parser):
        command_parser.add_argument("--repository", type=pathlib.Path, required=True)
        command_parser.add_argument("--checkout", type=pathlib.Path, required=True)
        command_parser.add_argument("--expected-commit", required=True)
    verify_parser.add_argument("--state", type=pathlib.Path, required=True)
    args = parser.parse_args()

    try:
        if args.command == "expected-fingerprint":
            expected = derive_expected_overlay(
                args.repository, args.checkout, args.expected_commit
            )
            print(expected.delta_fingerprint)
        else:
            verified = verify_overlay_state(
                args.repository,
                args.checkout,
                args.state,
                args.expected_commit,
            )
            print(verified.checkout_delta_fingerprint)
    except (OSError, OverlayStateError, SystemExit) as error:
        print(f"error: overlay verification failed: {error}", file=os.sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
