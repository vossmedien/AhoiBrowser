"""Schema, value objects, and atomic cache I/O for Chromium overlay state."""

from __future__ import annotations

import dataclasses
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import stat
import tempfile
from typing import Any


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


@dataclasses.dataclass(frozen=True)
class OverlayRefreshResult:
    chromium_commit: str
    input_fingerprint: str
    checkout_delta_fingerprint: str
    previous_tree: str
    actual_tree: str
    applied_at: str
    checkout_changed: bool
    state_changed: bool


@dataclasses.dataclass(frozen=True)
class OverlayApplyResult:
    chromium_commit: str
    input_fingerprint: str
    checkout_delta_fingerprint: str
    previous_tree: str
    actual_tree: str
    applied_at: str


@dataclasses.dataclass(frozen=True)
class OverlayRestoreResult:
    chromium_commit: str
    input_fingerprint: str
    checkout_delta_fingerprint: str
    previous_tree: str
    actual_tree: str


def require_commit(value: Any, *, label: str) -> str:
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

    expected_commit = require_commit(expected_commit, label="expected Chromium commit")
    try:
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
            raise OverlayStateError(
                f"overlay state must be a regular non-symlink file: {path}"
            )
        raw = path.read_bytes()
    except OSError as error:
        raise OverlayStateError(
            f"overlay state is missing or unreadable: {path}"
        ) from error
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
        raise OverlayStateError(
            "overlay state schema mismatch: " + "; ".join(details)
        )
    if type(state["schemaVersion"]) is not int or state["schemaVersion"] != STATE_SCHEMA_VERSION:
        raise OverlayStateError(
            f"overlay state schemaVersion must be {STATE_SCHEMA_VERSION}"
        )
    state_commit = require_commit(
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
        raise OverlayStateError(
            "overlay state appliedAt is not a valid timestamp"
        ) from error
    if parsed_applied_at.tzinfo is None or parsed_applied_at.utcoffset() is None:
        raise OverlayStateError("overlay state appliedAt must include a UTC offset")
    return state


def new_overlay_state(expected: ExpectedOverlay) -> dict[str, Any]:
    return {
        "schemaVersion": STATE_SCHEMA_VERSION,
        "fingerprint": expected.input_fingerprint,
        "checkoutDeltaFingerprint": expected.delta_fingerprint,
        "chromiumCommit": expected.chromium_commit,
        "appliedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
    }


def write_overlay_state_atomic(path: pathlib.Path, state: dict[str, Any]) -> None:
    """Durably replace a state file through a same-directory temporary file."""

    encoded = (json.dumps(state, indent=2, sort_keys=True) + "\n").encode("utf-8")
    path = path.absolute()
    if path.is_symlink() or not path.is_file():
        raise OverlayStateError(
            f"overlay state must remain an existing regular file: {path}"
        )
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = pathlib.Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def create_overlay_state_atomic(path: pathlib.Path, state: dict[str, Any]) -> None:
    """Publish a new state file without replacing any concurrently created path."""

    encoded = (json.dumps(state, indent=2, sort_keys=True) + "\n").encode("utf-8")
    path = path.absolute()
    if path.exists() or path.is_symlink():
        raise OverlayStateError(f"overlay state already exists: {path}")
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = pathlib.Path(temporary)
    published = False
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary_path, 0o644)
        try:
            # A hard-link publication is atomic and, unlike os.replace(), cannot
            # overwrite a state file another process created after our checks.
            os.link(temporary_path, path, follow_symlinks=False)
        except FileExistsError as error:
            raise OverlayStateError(
                "overlay state appeared during initial application"
            ) from error
        published = True
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        try:
            temporary_path.unlink(missing_ok=True)
        except OSError:
            # Once the destination link exists, a leftover private temporary
            # link must not turn a successful state publication into rollback.
            if not published:
                raise


def remove_exact_overlay_state(
    path: pathlib.Path, expected_state: dict[str, Any], expected_commit: str
) -> None:
    """Remove only the still-current regular state file."""

    if load_overlay_state(path, expected_commit) != expected_state:
        raise OverlayStateError("overlay state changed before removal")
    try:
        path.unlink()
    except OSError as error:
        raise OverlayStateError(f"could not remove overlay state: {path}") from error


def delta_fingerprint(chromium_commit: str, tree: str) -> str:
    """Bind the semantic resulting Git tree to its exact Chromium base commit."""

    chromium_commit = require_commit(
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
