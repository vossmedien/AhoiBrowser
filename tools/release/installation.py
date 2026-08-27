#!/usr/bin/env python3
"""Fail-closed, transactional installation of a verified macOS release app."""

from __future__ import annotations

import contextlib
import ctypes
import errno
import fcntl
import os
import pathlib
import re
import secrets
import shutil
import signal
import stat
import tempfile
import threading
from typing import Callable, Iterator, Optional, Sequence

from .chain import create_installed_receipt
from .common import (
    ReleaseError,
    bundle_identity,
    canonical_json,
    load_json,
    require_object,
    require_sha256,
    require_string,
    run,
    sha256_bytes,
    sha256_file,
    tree_sha256,
)
from .signing import verify_signed_app


AT_FDCWD = -2
RENAME_SWAP = 0x00000002
RENAME_EXCL = 0x00000004
LOCK_NAME = ".AhoiBrowser.install.lock"
STAGE_PREFIX = ".AhoiBrowser.stage-"
BACKUP_PREFIX = ".AhoiBrowser.rollback-"
APP_NAME = "AhoiBrowser.app"

CopyBundle = Callable[[pathlib.Path, pathlib.Path], None]
RenameBundle = Callable[[pathlib.Path, pathlib.Path], None]
ProcessInspector = Callable[[Sequence[pathlib.Path]], list[dict]]
BundleVerifier = Callable[..., dict]
ReceiptFactory = Callable[..., dict]
StageVerifier = Callable[[pathlib.Path], None]
InstallFinalizer = Callable[[pathlib.Path, dict], dict]


class InstallerArtifactReservation:
    """Own an exclusive receipt publication path for one install transaction."""

    def __init__(
        self,
        output: pathlib.Path,
        marker_path: pathlib.Path,
        marker_identity: tuple[int, int],
        marker_content: bytes,
    ) -> None:
        self.output = output
        self.marker_path = marker_path
        self.marker_identity = marker_identity
        self.marker_content = marker_content
        self.published_identity: Optional[tuple[int, int]] = None
        self.published_sha256: Optional[str] = None
        self.closed = False

    def _require_marker(self) -> None:
        if self.closed:
            raise ReleaseError("installer artifact reservation is already closed")
        if (
            not _exists(self.marker_path)
            or self.marker_path.is_symlink()
            or not self.marker_path.is_file()
            or _marker(self.marker_path) != self.marker_identity
            or self.marker_path.read_bytes() != self.marker_content
        ):
            raise ReleaseError(
                f"installer artifact reservation changed: {self.marker_path}"
            )

    def _require_published_output(self) -> None:
        if self.published_identity is None or self.published_sha256 is None:
            raise ReleaseError("installer finalizer did not publish its receipt")
        if (
            not _exists(self.output)
            or self.output.is_symlink()
            or not self.output.is_file()
            or _marker(self.output) != self.published_identity
            or sha256_file(self.output) != self.published_sha256
        ):
            raise ReleaseError(
                f"published installer artifact changed unexpectedly: {self.output}"
            )

    def publish_json(self, value: object) -> None:
        """Publish canonical JSON without ever replacing an existing output path."""
        self._require_marker()
        if self.published_identity is not None:
            raise ReleaseError("installer artifact was already published")
        if _exists(self.output):
            raise ReleaseError(f"installer artifact output appeared: {self.output}")

        content = canonical_json(value)
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{self.output.name}.",
            suffix=".pending",
            dir=str(self.output.parent),
        )
        temporary = pathlib.Path(temporary_name)
        temporary_identity = _marker(temporary)
        try:
            with os.fdopen(descriptor, "wb") as handle:
                handle.write(content)
                handle.flush()
                os.fsync(handle.fileno())
            os.chmod(temporary, 0o644)
            self._require_marker()
            try:
                os.link(temporary, self.output, follow_symlinks=False)
            except FileExistsError as error:
                raise ReleaseError(
                    f"installer artifact output appeared during publication: {self.output}"
                ) from error
            except OSError as error:
                raise ReleaseError(
                    f"cannot publish installer artifact exclusively: {self.output}: {error}"
                ) from error
            if _marker(self.output) != temporary_identity:
                raise ReleaseError(
                    "installer artifact output changed immediately after publication: "
                    f"{self.output}"
                )
            self.published_identity = temporary_identity
            self.published_sha256 = sha256_bytes(content)
            self._require_published_output()
        finally:
            if _exists(temporary):
                _remove_owned_regular_file(
                    temporary,
                    expected_marker=temporary_identity,
                    expected_hash=sha256_bytes(content),
                )

    def complete(self) -> None:
        """Retain the owned receipt and release only this transaction's marker."""
        self._require_marker()
        self._require_published_output()
        _remove_owned_regular_file(
            self.marker_path,
            expected_marker=self.marker_identity,
            expected_content=self.marker_content,
        )
        self.closed = True

    def abort(self) -> None:
        """Remove only files still provably owned by this failed transaction."""
        if self.closed:
            return
        if self.published_identity is not None:
            _remove_owned_regular_file(
                self.output,
                expected_marker=self.published_identity,
                expected_hash=self.published_sha256,
            )
        _remove_owned_regular_file(
            self.marker_path,
            expected_marker=self.marker_identity,
            expected_content=self.marker_content,
        )
        self.closed = True


def _exists(path: pathlib.Path) -> bool:
    return os.path.lexists(path)


def _require_canonical_parent(path: pathlib.Path, name: str) -> pathlib.Path:
    if not path.is_absolute():
        raise ReleaseError(f"{name} must be an absolute path")
    parent = path.parent
    if not parent.is_dir() or parent.is_symlink():
        raise ReleaseError(f"{name} parent must be a real directory: {parent}")
    try:
        canonical_parent = parent.resolve(strict=True)
    except OSError as error:
        raise ReleaseError(f"cannot resolve {name} parent: {parent}: {error}") from error
    if canonical_parent != parent:
        raise ReleaseError(f"{name} parent must use its canonical path: {parent}")
    return parent


def _is_within(path: pathlib.Path, parent: pathlib.Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def _require_evidence_file(path: pathlib.Path, name: str) -> None:
    _require_canonical_parent(path, name)
    if path.is_symlink() or not path.is_file():
        raise ReleaseError(f"{name} must be a real file: {path}")
    if path.resolve(strict=True) != path:
        raise ReleaseError(f"{name} must use its canonical path: {path}")


def _require_bundle_path(
    path: pathlib.Path,
    name: str,
    *,
    must_exist: bool,
) -> None:
    _require_canonical_parent(path, name)
    if path.name != APP_NAME:
        raise ReleaseError(f"{name} must be named {APP_NAME}")
    present = _exists(path)
    if must_exist and not present:
        raise ReleaseError(f"{name} is missing: {path}")
    if not present:
        return
    if path.is_symlink() or not path.is_dir():
        raise ReleaseError(f"{name} must be a real app directory: {path}")
    try:
        canonical = path.resolve(strict=True)
    except OSError as error:
        raise ReleaseError(f"cannot resolve {name}: {path}: {error}") from error
    if canonical != path:
        raise ReleaseError(f"{name} must use its canonical path: {path}")


def _marker(path: pathlib.Path) -> tuple[int, int]:
    metadata = path.lstat()
    return metadata.st_dev, metadata.st_ino


def _remove_owned_regular_file(
    path: pathlib.Path,
    *,
    expected_marker: tuple[int, int],
    expected_hash: Optional[str] = None,
    expected_content: Optional[bytes] = None,
) -> None:
    if not _exists(path):
        raise ReleaseError(f"owned installer file disappeared: {path}")
    if path.is_symlink() or not path.is_file() or _marker(path) != expected_marker:
        raise ReleaseError(f"refusing to remove a replaced installer file: {path}")
    if expected_hash is not None and sha256_file(path) != expected_hash:
        raise ReleaseError(f"refusing to remove changed installer file: {path}")
    if expected_content is not None and path.read_bytes() != expected_content:
        raise ReleaseError(f"refusing to remove changed installer marker: {path}")
    path.unlink()


def _token(value: object, fallback: str) -> str:
    text = str(value or fallback)
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", text).strip("-.")
    return (cleaned or fallback)[:48]


def _transaction_path(
    parent: pathlib.Path,
    *,
    prefix: str,
    identity: dict,
    bundle_hash: str,
) -> pathlib.Path:
    version = _token(identity.get("marketingVersion"), "unknown")
    build = _token(identity.get("buildNumber"), "unknown")
    source = _token(identity.get("sourceCommit"), "unknown")[:12]
    return parent / (
        f"{prefix}v{version}-b{build}-s{source}-h{bundle_hash[:12]}.app"
    )


def _require_safe_transaction_path(path: pathlib.Path, parent: pathlib.Path) -> None:
    if path.parent != parent or not path.name.endswith(".app"):
        raise ReleaseError(f"unsafe installer transaction path: {path}")
    if not path.name.startswith((STAGE_PREFIX, BACKUP_PREFIX)):
        raise ReleaseError(f"unsafe installer transaction prefix: {path}")


def _reserve_transaction_path(path: pathlib.Path, parent: pathlib.Path) -> None:
    _require_safe_transaction_path(path, parent)
    try:
        os.mkdir(path, 0o700)
    except FileExistsError as error:
        raise ReleaseError(
            f"installer transaction path already exists; inspect it manually: {path}"
        ) from error
    except OSError as error:
        raise ReleaseError(f"cannot reserve installer transaction path: {path}: {error}") from error


def _remove_transaction_path(
    path: pathlib.Path,
    parent: pathlib.Path,
    *,
    expected_hash: Optional[str] = None,
    expected_marker: Optional[tuple[int, int]] = None,
) -> None:
    if not _exists(path):
        return
    _require_safe_transaction_path(path, parent)
    if path.is_symlink() or not path.is_dir():
        raise ReleaseError(f"refusing to remove unsafe transaction object: {path}")
    if expected_marker is not None and _marker(path) != expected_marker:
        raise ReleaseError(
            f"refusing to remove a replaced transaction directory: {path}"
        )
    if expected_hash is not None and tree_sha256(path) != expected_hash:
        raise ReleaseError(
            f"refusing to remove transaction bundle with unexpected bytes: {path}"
        )
    shutil.rmtree(path)


def _copy_with_ditto(source: pathlib.Path, destination: pathlib.Path) -> None:
    run(["ditto", str(source), str(destination)])


def _renameatx(source: pathlib.Path, destination: pathlib.Path, flags: int) -> None:
    library = ctypes.CDLL(None, use_errno=True)
    try:
        operation = library.renameatx_np
    except AttributeError as error:
        raise ReleaseError(
            "renameatx_np is unavailable; atomic installation is unsupported"
        ) from error
    operation.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    ]
    operation.restype = ctypes.c_int
    result = operation(
        AT_FDCWD,
        os.fsencode(source),
        AT_FDCWD,
        os.fsencode(destination),
        flags,
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise ReleaseError(
            f"atomic rename failed: {source} -> {destination}: "
            f"{os.strerror(error_number)}"
        )


def _exchange(source: pathlib.Path, destination: pathlib.Path) -> None:
    _renameatx(source, destination, RENAME_SWAP)


def _move_exclusive(source: pathlib.Path, destination: pathlib.Path) -> None:
    _renameatx(source, destination, RENAME_EXCL)


def _inspect_bundle_processes(paths: Sequence[pathlib.Path]) -> list[dict]:
    completed = run(["/bin/ps", "-axww", "-o", "pid=,command="])
    needles = [(path, f"{path}/Contents/") for path in paths]
    matches = []
    for raw_line in completed.stdout.decode("utf-8", "replace").splitlines():
        fields = raw_line.strip().split(None, 1)
        if len(fields) != 2 or not fields[0].isdigit():
            continue
        pid = int(fields[0])
        if pid == os.getpid():
            continue
        command = fields[1]
        for bundle, needle in needles:
            if needle in command:
                matches.append(
                    {"pid": pid, "bundle": str(bundle), "command": command}
                )
                break
    return matches


def _require_quiescent(
    paths: Sequence[pathlib.Path],
    inspector: ProcessInspector,
) -> None:
    running = inspector(paths)
    if running:
        details = ", ".join(
            f"PID {item.get('pid', '?')} ({item.get('bundle', 'unknown bundle')})"
            for item in running
        )
        raise ReleaseError(f"quit all AhoiBrowser bundle processes before installation: {details}")


@contextlib.contextmanager
def _installation_lock(parent: pathlib.Path) -> Iterator[None]:
    path = parent / LOCK_NAME
    flags = os.O_RDWR | os.O_CREAT
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as error:
        raise ReleaseError(f"cannot open installer lock {path}: {error}") from error
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
            raise ReleaseError(f"installer lock is not a safe regular file: {path}")
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            if error.errno in (errno.EACCES, errno.EAGAIN):
                raise ReleaseError("another AhoiBrowser installation is in progress") from error
            raise ReleaseError(f"cannot lock AhoiBrowser installation: {error}") from error
        yield
    finally:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)


@contextlib.contextmanager
def _rollback_signals() -> Iterator[None]:
    if threading.current_thread() is not threading.main_thread():
        yield
        return
    caught = (signal.SIGHUP, signal.SIGINT, signal.SIGTERM)
    previous = {number: signal.getsignal(number) for number in caught}

    def interrupt(number: int, _frame: object) -> None:
        raise ReleaseError(f"installation interrupted by signal {number}")

    try:
        for number in caught:
            signal.signal(number, interrupt)
        yield
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)


def _load_and_validate_candidate(
    app: pathlib.Path,
    *,
    signing_receipt_path: pathlib.Path,
    notarization_receipt_path: pathlib.Path,
    expected_bundle: dict,
    policy_path: pathlib.Path,
    verifier: BundleVerifier,
) -> tuple[dict, dict, dict]:
    signing = require_object(load_json(signing_receipt_path), "signing receipt")
    notary = require_object(load_json(notarization_receipt_path), "notarization receipt")
    if signing.get("schemaVersion") != 1 or signing.get("kind") != "signed-package-provenance":
        raise ReleaseError("signing receipt kind/schema is invalid")
    if notary.get("schemaVersion") != 1 or notary.get("kind") != "notarization-receipt":
        raise ReleaseError("notarization receipt kind/schema is invalid")

    signing_policy = require_object(signing.get("signing"), "signing receipt policy")
    team_id = require_string(signing_policy.get("teamIdentifier"), "signing Team ID")
    authority = require_string(signing_policy.get("authority"), "signing authority")
    if (
        signing_policy.get("hardenedRuntime") is not True
        or signing_policy.get("trustedTimestamp") is not True
    ):
        raise ReleaseError("signing receipt lacks runtime/timestamp proof")
    signed = require_object(signing.get("signedBundle"), "signed bundle")
    notary_bundle = require_object(notary.get("bundle"), "notarized bundle")
    notarized = require_object(notary_bundle.get("identity"), "notarized identity")
    if (
        notary_bundle.get("staplerValidated") is not True
        or notary_bundle.get("gatekeeperAccepted") is not True
    ):
        raise ReleaseError("notarization receipt lacks staple/Gatekeeper proof")
    signed_tree = require_sha256(
        signed.get("bundleTreeSha256"), "signed bundle tree SHA-256"
    )
    if signed_tree != require_sha256(
        notary_bundle.get("preStapleTreeSha256"), "notary pre-staple SHA-256"
    ):
        raise ReleaseError("notarization input differs from the signed bundle")
    expected_tree = require_sha256(
        notary_bundle.get("postStapleTreeSha256"), "notary post-staple SHA-256"
    )

    identity = bundle_identity(app)
    if identity.get("name") != expected_bundle.get("name"):
        raise ReleaseError("candidate bundle name differs from release policy")
    if identity.get("identifier") != expected_bundle.get("identifier"):
        raise ReleaseError("candidate bundle identifier differs from release policy")
    if identity.get("buildProfile") != expected_bundle.get("buildProfile"):
        raise ReleaseError("candidate is not stamped with the release build profile")
    if identity.get("bundleTreeSha256") != expected_tree:
        raise ReleaseError("candidate is not the exact notarized/stapled bundle")
    if identity != notarized:
        raise ReleaseError("candidate identity differs from the notarization receipt")
    for field, value in identity.items():
        if field != "bundleTreeSha256" and signed.get(field) != value:
            raise ReleaseError(f"candidate {field} differs from the signed bundle")

    verification = verifier(
        app,
        expected_team=team_id,
        expected_authority=authority,
        policy_path=policy_path,
        require_notarization=True,
    )
    if not isinstance(verification, dict) or verification.get("bundle") != identity:
        raise ReleaseError("candidate live verification returned a different identity")
    return signing, notary, identity


def _verify_staged_bundle(
    app: pathlib.Path,
    *,
    expected_identity: dict,
    signing: dict,
    policy_path: pathlib.Path,
    verifier: BundleVerifier,
) -> None:
    if tree_sha256(app) != expected_identity["bundleTreeSha256"]:
        raise ReleaseError("same-volume staged bundle differs from the candidate")
    policy = signing["signing"]
    verification = verifier(
        app,
        expected_team=policy["teamIdentifier"],
        expected_authority=policy["authority"],
        policy_path=policy_path,
        require_notarization=True,
    )
    if not isinstance(verification, dict) or verification.get("bundle") != expected_identity:
        raise ReleaseError("same-volume staged bundle failed exact identity verification")


def validate_installer_artifact_output(
    output: pathlib.Path,
    *,
    app: pathlib.Path,
    required_install_path: pathlib.Path,
    name: str,
) -> pathlib.Path:
    """Require an immutable installer artifact outside every app transaction path."""
    output_parent = _require_canonical_parent(output, name)
    if _exists(output):
        raise ReleaseError(f"{name} already exists: {output}")
    if (
        output_parent == required_install_path.parent
        or _is_within(output_parent, app)
        or _is_within(output_parent, required_install_path)
    ):
        raise ReleaseError(f"{name} must be outside app transaction paths")
    return output_parent


def reserve_installer_artifact_output(
    output: pathlib.Path,
    *,
    app: pathlib.Path,
    required_install_path: pathlib.Path,
    name: str,
) -> InstallerArtifactReservation:
    """Reserve a sibling marker while keeping the final output no-replace."""
    parent = validate_installer_artifact_output(
        output,
        app=app,
        required_install_path=required_install_path,
        name=name,
    )
    marker_path = parent / f".{output.name}.install-reservation"
    marker_content = (
        "AhoiBrowser installer artifact reservation\n"
        f"{secrets.token_hex(32)}\n"
    ).encode("ascii")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(marker_path, flags, 0o600)
    except FileExistsError as error:
        raise ReleaseError(
            f"installer artifact path is already reserved: {marker_path}"
        ) from error
    except OSError as error:
        raise ReleaseError(
            f"cannot reserve installer artifact path: {marker_path}: {error}"
        ) from error
    marker_identity: Optional[tuple[int, int]] = None
    marker_content_written = False
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
            os.close(descriptor)
            raise ReleaseError(
                f"installer artifact reservation is not a safe file: {marker_path}"
            )
        marker_identity = (metadata.st_dev, metadata.st_ino)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(marker_content)
            handle.flush()
            os.fsync(handle.fileno())
        marker_content_written = True
        reservation = InstallerArtifactReservation(
            output,
            marker_path,
            marker_identity,
            marker_content,
        )
        reservation._require_marker()
        if _exists(output):
            reservation.abort()
            raise ReleaseError(f"installer artifact output appeared: {output}")
        return reservation
    except BaseException as failure:
        if _exists(marker_path) and marker_identity is not None:
            try:
                _remove_owned_regular_file(
                    marker_path,
                    expected_marker=marker_identity,
                    expected_content=(
                        marker_content if marker_content_written else None
                    ),
                )
            except BaseException as cleanup_failure:
                raise ReleaseError(
                    "installer artifact reservation failed and owned-marker cleanup "
                    f"could not be proven: {marker_path}: {cleanup_failure}"
                ) from cleanup_failure
        raise failure


def install_verified_app_transaction(
    app: pathlib.Path,
    *,
    expected_bundle: dict,
    required_install_path: pathlib.Path,
    verify_staged_bundle: StageVerifier,
    finalize_installation: InstallFinalizer,
    candidate_identity: Optional[dict] = None,
    verify_candidate: Optional[StageVerifier] = None,
    artifact_reservation: Optional[InstallerArtifactReservation] = None,
    copy_bundle: CopyBundle = _copy_with_ditto,
    exchange_bundle: RenameBundle = _exchange,
    move_exclusive: RenameBundle = _move_exclusive,
    process_inspector: ProcessInspector = _inspect_bundle_processes,
) -> dict:
    """Install one already verified bundle through the shared atomic transaction."""
    _require_bundle_path(app, "installation candidate", must_exist=True)
    _require_bundle_path(required_install_path, "installation target", must_exist=False)
    if app == required_install_path:
        raise ReleaseError("candidate must be separate from the installation target")

    live_candidate_identity = bundle_identity(app)
    if candidate_identity is None:
        candidate_identity = live_candidate_identity
    elif live_candidate_identity != candidate_identity:
        raise ReleaseError("candidate changed after its authoritative verification")
    if (
        candidate_identity.get("name") != expected_bundle.get("name")
        or candidate_identity.get("identifier") != expected_bundle.get("identifier")
        or candidate_identity.get("buildProfile")
        != expected_bundle.get("buildProfile")
    ):
        raise ReleaseError("candidate identity/build profile differs from install policy")
    candidate_hash = require_sha256(
        candidate_identity.get("bundleTreeSha256"), "candidate bundle tree SHA-256"
    )
    if verify_candidate is not None:
        verify_candidate(app)
        if bundle_identity(app) != candidate_identity:
            raise ReleaseError("candidate changed during its pre-install verification")

    parent = required_install_path.parent

    with _installation_lock(parent):
        _require_bundle_path(app, "installation candidate", must_exist=True)
        _require_bundle_path(required_install_path, "installation target", must_exist=False)
        if bundle_identity(app) != candidate_identity:
            raise ReleaseError("candidate changed before installer staging")
        _require_quiescent((app, required_install_path), process_inspector)
        previous_identity = None
        previous_marker = None
        if _exists(required_install_path):
            previous_identity = bundle_identity(required_install_path)
            if (
                previous_identity.get("name") != expected_bundle.get("name")
                or previous_identity.get("identifier") != expected_bundle.get("identifier")
            ):
                raise ReleaseError("existing target is not the expected AhoiBrowser bundle")
            previous_marker = _marker(required_install_path)
            stage = _transaction_path(
                parent,
                prefix=BACKUP_PREFIX,
                identity=previous_identity,
                bundle_hash=previous_identity["bundleTreeSha256"],
            )
        else:
            stage = _transaction_path(
                parent,
                prefix=STAGE_PREFIX,
                identity=candidate_identity,
                bundle_hash=candidate_hash,
            )

        _reserve_transaction_path(stage, parent)
        activation_attempted = False
        stage_marker = _marker(stage)
        stage_verified = False
        try:
            copy_bundle(app, stage)
            if _marker(stage) != stage_marker:
                raise ReleaseError("staging directory identity changed during copy")
            if bundle_identity(stage) != candidate_identity:
                raise ReleaseError("same-volume staged bundle differs from the candidate")
            verify_staged_bundle(stage)
            if bundle_identity(stage) != candidate_identity:
                raise ReleaseError("staged bundle changed during verification")
            stage_verified = True
            if stage.stat().st_dev != parent.stat().st_dev:
                raise ReleaseError("installer staging is not on the target filesystem")
            _require_quiescent((app, required_install_path), process_inspector)
            if bundle_identity(app) != candidate_identity:
                raise ReleaseError("candidate changed during installer staging")
            if previous_identity is not None:
                if (
                    _marker(required_install_path) != previous_marker
                    or tree_sha256(required_install_path)
                    != previous_identity["bundleTreeSha256"]
                ):
                    raise ReleaseError("installed target changed during preflight")
            elif _exists(required_install_path):
                raise ReleaseError("installation target appeared during preflight")

            method = (
                "renameatx_np(RENAME_SWAP)"
                if previous_identity is not None
                else "renameatx_np(RENAME_EXCL)"
            )
            with _rollback_signals():
                activation_attempted = True
                if previous_identity is not None:
                    exchange_bundle(required_install_path, stage)
                    if (
                        _marker(required_install_path) != stage_marker
                        or _marker(stage) != previous_marker
                    ):
                        raise ReleaseError(
                            "atomic exchange produced an unexpected path identity"
                        )
                    if bundle_identity(stage) != previous_identity:
                        raise ReleaseError(
                            "version-bound rollback bundle changed during exchange"
                        )
                else:
                    move_exclusive(stage, required_install_path)
                    if _marker(required_install_path) != stage_marker or _exists(stage):
                        raise ReleaseError(
                            "atomic initial installation produced an unexpected state"
                        )

                if bundle_identity(required_install_path) != candidate_identity:
                    raise ReleaseError("activated bundle differs from the verified candidate")

                installation = {
                    "target": str(required_install_path),
                    "method": method,
                    "sameVolumeStaging": True,
                    "processesQuiescent": True,
                    "automaticRollbackOnVerificationFailure": True,
                    "postInstallVerification": True,
                    "candidateBundleTreeSha256": candidate_hash,
                    "previousBundle": None,
                }
                if previous_identity is not None:
                    installation["previousBundle"] = {
                        "backupPath": str(stage),
                        "bundleTreeSha256": previous_identity["bundleTreeSha256"],
                        "bundle": previous_identity,
                    }
                receipt = finalize_installation(
                    required_install_path, installation
                )
                if not isinstance(receipt, dict):
                    raise ReleaseError("installer finalizer returned an invalid receipt")
                if bundle_identity(required_install_path) != candidate_identity:
                    raise ReleaseError("installed bundle changed during final verification")
                if artifact_reservation is not None:
                    artifact_reservation.complete()
            return receipt
        except BaseException as failure:
            rollback_error = None
            if activation_attempted:
                try:
                    if previous_identity is not None:
                        target_marker = _marker(required_install_path)
                        backup_marker = _marker(stage)
                        activated = (
                            target_marker == stage_marker
                            and backup_marker == previous_marker
                        )
                        not_activated = (
                            target_marker == previous_marker
                            and backup_marker == stage_marker
                        )
                        if activated:
                            exchange_bundle(required_install_path, stage)
                            if (
                                _marker(required_install_path) != previous_marker
                                or bundle_identity(required_install_path)
                                != previous_identity
                                or _marker(stage) != stage_marker
                            ):
                                raise ReleaseError(
                                    "automatic rollback did not restore the prior bundle"
                                )
                        elif not not_activated:
                            raise ReleaseError(
                                "installer state is ambiguous; preserve target and "
                                "rollback path"
                            )
                    else:
                        activated = (
                            _exists(required_install_path)
                            and not _exists(stage)
                            and _marker(required_install_path) == stage_marker
                        )
                        not_activated = (
                            not _exists(required_install_path)
                            and _exists(stage)
                            and _marker(stage) == stage_marker
                        )
                        if activated:
                            move_exclusive(required_install_path, stage)
                            if (
                                _exists(required_install_path)
                                or _marker(stage) != stage_marker
                            ):
                                raise ReleaseError(
                                    "automatic rollback did not restore target absence"
                                )
                        elif not not_activated:
                            raise ReleaseError(
                                "installer state is ambiguous; preserve target and "
                                "staging path"
                            )
                except BaseException as rollback_failure:
                    rollback_error = rollback_failure
            if rollback_error is not None:
                raise ReleaseError(
                    "CRITICAL: installation failed and automatic rollback could not be "
                    f"proven; inspect {required_install_path} and {stage}: {rollback_error}"
                ) from rollback_error

            cleanup_error = None
            try:
                if artifact_reservation is not None:
                    artifact_reservation.abort()
                if _exists(stage):
                    _remove_transaction_path(
                        stage,
                        parent,
                        expected_hash=candidate_hash if stage_verified else None,
                        expected_marker=stage_marker,
                    )
            except BaseException as cleanup_failure:
                cleanup_error = cleanup_failure
            if cleanup_error is not None:
                raise ReleaseError(
                    "installation failed with the prior state restored, but cleanup was "
                    "intentionally refused; inspect "
                    f"the installer artifact and {stage}: "
                    f"{cleanup_error}"
                ) from cleanup_error
            if activation_attempted:
                raise ReleaseError(
                    "post-install verification failed; the prior installation state was restored: "
                    f"{failure}"
                ) from failure
            if isinstance(failure, ReleaseError):
                raise
            raise ReleaseError(f"installation staging failed: {failure}") from failure


def install_release_app(
    app: pathlib.Path,
    *,
    signing_receipt_path: pathlib.Path,
    notarization_receipt_path: pathlib.Path,
    policy_path: pathlib.Path,
    output: pathlib.Path,
    expected_bundle: dict,
    required_install_path: pathlib.Path = pathlib.Path("/Applications/AhoiBrowser.app"),
    copy_bundle: CopyBundle = _copy_with_ditto,
    exchange_bundle: RenameBundle = _exchange,
    move_exclusive: RenameBundle = _move_exclusive,
    process_inspector: ProcessInspector = _inspect_bundle_processes,
    bundle_verifier: BundleVerifier = verify_signed_app,
    receipt_factory: ReceiptFactory = create_installed_receipt,
) -> dict:
    """Install an exact notarized candidate and retain atomic rollback evidence."""
    _require_bundle_path(app, "release candidate", must_exist=True)
    _require_bundle_path(required_install_path, "installation target", must_exist=False)
    if app == required_install_path:
        raise ReleaseError("release candidate must be separate from the installation target")
    _require_evidence_file(signing_receipt_path, "signing receipt")
    _require_evidence_file(notarization_receipt_path, "notarization receipt")
    output_parent = validate_installer_artifact_output(
        output,
        app=app,
        required_install_path=required_install_path,
        name="installed receipt",
    )
    if not (
        signing_receipt_path.parent
        == notarization_receipt_path.parent
        == output_parent
    ):
        raise ReleaseError("installation receipts/output must share one evidence directory")
    artifact_reservation = reserve_installer_artifact_output(
        output,
        app=app,
        required_install_path=required_install_path,
        name="installed receipt",
    )
    try:
        signing, _notary, candidate_identity = _load_and_validate_candidate(
            app,
            signing_receipt_path=signing_receipt_path,
            notarization_receipt_path=notarization_receipt_path,
            expected_bundle=expected_bundle,
            policy_path=policy_path,
            verifier=bundle_verifier,
        )

        def verify_stage(stage: pathlib.Path) -> None:
            _verify_staged_bundle(
                stage,
                expected_identity=candidate_identity,
                signing=signing,
                policy_path=policy_path,
                verifier=bundle_verifier,
            )

        def finalize(target: pathlib.Path, installation: dict) -> dict:
            with tempfile.TemporaryDirectory(
                prefix=".AhoiBrowser.install-receipt-",
                dir=str(output_parent),
            ) as temporary_directory:
                temporary_output = pathlib.Path(temporary_directory) / "receipt.json"
                receipt = receipt_factory(
                    target,
                    signing_receipt_path=signing_receipt_path,
                    notarization_receipt_path=notarization_receipt_path,
                    policy_path=policy_path,
                    output=temporary_output,
                    required_install_path=required_install_path,
                    installation=installation,
                )
                if not isinstance(receipt, dict) or load_json(temporary_output) != receipt:
                    raise ReleaseError(
                        "installed receipt factory returned different persisted evidence"
                    )
                artifact_reservation.publish_json(receipt)
                return receipt

        return install_verified_app_transaction(
            app,
            expected_bundle=expected_bundle,
            required_install_path=required_install_path,
            candidate_identity=candidate_identity,
            verify_staged_bundle=verify_stage,
            finalize_installation=finalize,
            artifact_reservation=artifact_reservation,
            copy_bundle=copy_bundle,
            exchange_bundle=exchange_bundle,
            move_exclusive=move_exclusive,
            process_inspector=process_inspector,
        )
    except BaseException:
        if not artifact_reservation.closed:
            artifact_reservation.abort()
        raise
