#!/usr/bin/env python3
"""Transactional lifecycle for the fixture-only macOS URL handler.

This module is intentionally the small public bridge between ``manage.py`` and
the three security-focused implementation modules.  It never forwards an
incoming URL, never broadens ownership beyond the dedicated protocol state,
and treats unreadable or foreign state as a reason to stop.
"""

from __future__ import annotations

import json
import os
import secrets
import stat
import subprocess
import sys
from pathlib import Path
from typing import Callable, FrozenSet, Iterable, Mapping, Optional, Tuple

import protocol_launch_services as launch_services
import protocol_native_handler as native_handler
from protocol_launch_services import LaunchServicesError
from protocol_native_handler import NativeHandlerError
from protocol_state import (
    FileStamp,
    ProtocolState,
    ProtocolStateError,
    STATE_DIRECTORY_NAME,
    locked_state,
    read_at,
    read_json_at,
    rename_at,
    stamp,
    unlink_at,
    write_new_at,
)


SCHEME = native_handler.SCHEME
ACCEPTED_URL = native_handler.ACCEPTED_URL
BUNDLE_ID = native_handler.BUNDLE_ID
APP_NAME = native_handler.APP_NAME
EXECUTABLE_NAME = native_handler.EXECUTABLE_NAME
MARKER_NAME = native_handler.MARKER_NAME
RECEIPT_NAME = native_handler.RECEIPT_NAME
EVENTS_NAME = native_handler.EVENTS_NAME
MARKER_SCHEMA_VERSION = native_handler.MARKER_SCHEMA_VERSION
RECEIPT_SCHEMA_VERSION = native_handler.RECEIPT_SCHEMA_VERSION
MANAGED_BY = native_handler.MANAGED_BY
HASH_KEYS = native_handler.HASH_KEYS

INSTALL_CONFIRMATION = "install-the-isolated-ahoi-e2e-protocol-handler"
REMOVE_CONFIRMATION = "remove-the-isolated-ahoi-e2e-protocol-handler"
STATUS_SCHEMA_VERSION = 1
_STAGING_PREFIX = ".protocol-handler-staging-"
_RECEIPT_REMOVAL_PREFIX = ".protocol-handler-receipt-removal-"
_TRANSACTION_PREFIXES = (
    _STAGING_PREFIX,
    _RECEIPT_REMOVAL_PREFIX,
    ".native-source-",
    ".pending-",
)


class ProtocolHandlerError(RuntimeError):
    """The custom-protocol lifecycle cannot proceed without widening scope."""


Runner = Callable[..., subprocess.CompletedProcess[str]]
ReceiptValue = Tuple[Mapping[str, object], bytes, FileStamp]


def _require_macos() -> None:
    if sys.platform != "darwin":
        raise ProtocolHandlerError(
            "the fixture custom-protocol handler is supported only on macOS"
        )


def _require_confirmation(value: str, expected: str) -> None:
    if value != expected:
        raise ProtocolHandlerError("the exact custom-protocol consent phrase is required")


def _app_path(state: ProtocolState) -> Path:
    return state.path / APP_NAME


def _receipt_payload(receipt: Mapping[str, object]) -> bytes:
    return (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _read_receipt(state: ProtocolState) -> Optional[ReceiptValue]:
    return read_json_at(state, RECEIPT_NAME)


def _entry_stamp(state: ProtocolState, name: str) -> Optional[FileStamp]:
    if Path(name).name != name:
        raise ProtocolStateError("protocol state artifact name is unsafe")
    try:
        metadata = os.stat(
            name,
            dir_fd=state.directory_descriptor,
            follow_symlinks=False,
        )
    except FileNotFoundError:
        return None
    except OSError as error:
        raise ProtocolStateError("protocol state artifact cannot be inspected") from error
    return stamp(metadata)


def _state_names(state: ProtocolState) -> FrozenSet[str]:
    try:
        return frozenset(os.listdir(state.directory_descriptor))
    except OSError as error:
        raise ProtocolStateError("protocol state directory cannot be inspected") from error


def _transaction_names(names: Iterable[str]) -> FrozenSet[str]:
    return frozenset(
        name
        for name in names
        if any(name.startswith(prefix) for prefix in _TRANSACTION_PREFIXES)
    )


def _registered_handler_paths(
    *, runner: Runner = subprocess.run
) -> FrozenSet[str]:
    try:
        return launch_services.registered_handler_paths(runner=runner)
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _parse_registered_handler_paths(dump: str) -> FrozenSet[str]:
    try:
        return launch_services.parse_registered_handler_paths(dump)
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _assert_registration_scope(paths: FrozenSet[str], app_path: Path) -> None:
    try:
        launch_services.assert_registration_scope(paths, app_path)
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _register_exact(app_path: Path, *, runner: Runner = subprocess.run) -> None:
    try:
        launch_services.register_exact(app_path, runner=runner)
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _unregister_exact(app_path: Path, *, runner: Runner = subprocess.run) -> None:
    try:
        launch_services.unregister_exact(app_path, runner=runner)
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _restore_registration(
    app_path: Path,
    was_registered: bool,
    *,
    runner: Runner = subprocess.run,
) -> None:
    try:
        launch_services.restore_registration(
            app_path, was_registered, runner=runner
        )
    except LaunchServicesError as error:
        raise ProtocolHandlerError(str(error)) from error


def _expected_registration(app_path: Path) -> FrozenSet[str]:
    return frozenset((launch_services.expected_path(app_path),))


def _same_identity(left: FileStamp, right: FileStamp) -> bool:
    return (
        left.device,
        left.inode,
        left.mode,
        left.uid,
    ) == (
        right.device,
        right.inode,
        right.mode,
        right.uid,
    )


def _remove_entry_at(
    parent_descriptor: int,
    name: str,
    *,
    expected_device: int,
    expected: Optional[FileStamp] = None,
) -> None:
    """Remove an owner-only tree without following links or crossing devices."""

    if Path(name).name != name or name in {"", ".", ".."}:
        raise ProtocolStateError("protocol removal target name is unsafe")
    try:
        before_raw = os.stat(
            name,
            dir_fd=parent_descriptor,
            follow_symlinks=False,
        )
    except OSError as error:
        raise ProtocolStateError("protocol removal target is unavailable") from error
    before = stamp(before_raw)
    if before.uid != os.getuid() or before.device != expected_device:
        raise ProtocolStateError("protocol removal target ownership is unsafe")
    if expected is not None and not _same_identity(before, expected):
        raise ProtocolStateError("protocol removal target identity changed")

    if stat.S_ISREG(before.mode):
        if before.links != 1:
            raise ProtocolStateError("protocol removal file has foreign hard links")
        try:
            os.unlink(name, dir_fd=parent_descriptor)
        except OSError as error:
            raise ProtocolStateError("protocol removal file could not be removed") from error
        return

    if not stat.S_ISDIR(before.mode):
        raise ProtocolStateError("protocol removal refuses links or special files")
    flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_DIRECTORY
    flags |= int(getattr(os, "O_CLOEXEC", 0))
    try:
        descriptor = os.open(name, flags, dir_fd=parent_descriptor)
    except OSError as error:
        raise ProtocolStateError("protocol removal directory cannot be opened") from error
    try:
        opened = stamp(os.fstat(descriptor))
        if not _same_identity(opened, before):
            raise ProtocolStateError("protocol removal directory identity changed")
        try:
            children = tuple(os.listdir(descriptor))
        except OSError as error:
            raise ProtocolStateError(
                "protocol removal directory cannot be enumerated"
            ) from error
        for child in children:
            _remove_entry_at(
                descriptor,
                child,
                expected_device=expected_device,
            )
        after = stamp(os.fstat(descriptor))
        if not _same_identity(after, opened):
            raise ProtocolStateError("protocol removal directory changed")
        os.fsync(descriptor)
    except OSError as error:
        raise ProtocolStateError("protocol removal directory could not be synced") from error
    finally:
        os.close(descriptor)
    try:
        os.rmdir(name, dir_fd=parent_descriptor)
        os.fsync(parent_descriptor)
    except OSError as error:
        raise ProtocolStateError("protocol removal directory could not be removed") from error


def _remove_state_tree(
    state: ProtocolState,
    name: str,
    *,
    expected: Optional[FileStamp] = None,
) -> None:
    state_metadata = os.fstat(state.directory_descriptor)
    _remove_entry_at(
        state.directory_descriptor,
        name,
        expected_device=state_metadata.st_dev,
        expected=expected,
    )


def _receipt_still_matches(state: ProtocolState, expected: ReceiptValue) -> bool:
    current = _read_receipt(state)
    return bool(
        current is not None
        and current[1] == expected[1]
        and current[2] == expected[2]
        and current[0] == expected[0]
    )


def _installation_result(receipt: Mapping[str, object]) -> Mapping[str, object]:
    result = dict(receipt)
    result["installed"] = True
    result["registered"] = True
    return result


def _raise_lifecycle_error(error: BaseException, operation: str) -> None:
    if isinstance(error, ProtocolHandlerError):
        raise error
    if isinstance(error, (ProtocolStateError, NativeHandlerError, LaunchServicesError)):
        raise ProtocolHandlerError(str(error)) from error
    raise ProtocolHandlerError(
        "custom-protocol %s failed during a bounded filesystem operation" % operation
    ) from error


def install(
    state_directory: Path,
    *,
    confirmation: str,
    runner: Runner = subprocess.run,
) -> Mapping[str, object]:
    """Install or validate the one-URL handler under an explicit state lock."""

    _require_macos()
    _require_confirmation(confirmation, INSTALL_CONFIRMATION)
    try:
        with locked_state(Path(state_directory), create=True) as state:
            if state is None:
                raise ProtocolStateError("dedicated protocol state is unavailable")
            app_path = _app_path(state)
            paths = _registered_handler_paths(runner=runner)
            _assert_registration_scope(paths, app_path)
            names = _state_names(state)
            transactions = _transaction_names(names)
            if transactions:
                raise ProtocolHandlerError(
                    "a prior custom-protocol transaction requires manual cleanup"
                )
            app_present = APP_NAME in names
            receipt_present = RECEIPT_NAME in names
            receipt_value = _read_receipt(state) if receipt_present else None

            if app_present or receipt_present:
                if not app_present or receipt_value is None:
                    raise ProtocolHandlerError(
                        "the custom-protocol installation is incomplete and needs repair"
                    )
                receipt = receipt_value[0]
                if not native_handler.valid_app(
                    app_path, app_path, receipt, state, runner=runner
                ):
                    raise ProtocolHandlerError(
                        "the existing custom-protocol handler failed integrity validation"
                    )
                expected_paths = _expected_registration(app_path)
                if paths and paths != expected_paths:
                    raise ProtocolHandlerError(
                        "the existing custom-protocol registration is not exact"
                    )
                repaired_registration = not paths
                try:
                    if repaired_registration:
                        _register_exact(app_path, runner=runner)
                    if _registered_handler_paths(runner=runner) != expected_paths:
                        raise ProtocolHandlerError(
                            "the custom-protocol registration did not verify"
                        )
                except BaseException:
                    if repaired_registration:
                        _restore_registration(app_path, False, runner=runner)
                    raise
                return _installation_result(receipt)

            if paths:
                raise ProtocolHandlerError(
                    "LaunchServices retains an unowned fixture registration"
                )

            staging_name = _STAGING_PREFIX + secrets.token_hex(16) + ".app"
            receipt: Optional[Mapping[str, object]] = None
            app_installed = False
            receipt_written = False
            registration_attempted = False
            try:
                receipt = native_handler.build_app(
                    state,
                    staging_name,
                    app_path,
                    secrets.token_hex(16),
                    runner=runner,
                )
                rename_at(state, staging_name, APP_NAME)
                app_installed = True
                if not native_handler.valid_app(
                    app_path, app_path, receipt, state, runner=runner
                ):
                    raise ProtocolHandlerError(
                        "the installed custom-protocol bundle did not revalidate"
                    )
                write_new_at(state, RECEIPT_NAME, _receipt_payload(receipt))
                receipt_written = True
                registration_attempted = True
                _register_exact(app_path, runner=runner)
                expected_paths = _expected_registration(app_path)
                committed = _read_receipt(state)
                if (
                    committed is None
                    or committed[0] != receipt
                    or _registered_handler_paths(runner=runner) != expected_paths
                    or not native_handler.valid_app(
                        app_path, app_path, committed[0], state, runner=runner
                    )
                ):
                    raise ProtocolHandlerError(
                        "the committed custom-protocol installation did not verify"
                    )
                return _installation_result(committed[0])
            except BaseException as error:
                rollback_errors = []
                if registration_attempted:
                    try:
                        _restore_registration(app_path, False, runner=runner)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                if app_installed and not rollback_errors:
                    try:
                        if receipt is None or not native_handler.owns_app(
                            app_path, app_path, receipt, state
                        ):
                            raise ProtocolStateError(
                                "installed app ownership changed during rollback"
                            )
                        app_stamp = _entry_stamp(state, APP_NAME)
                        if app_stamp is None:
                            raise ProtocolStateError(
                                "installed app disappeared during rollback"
                            )
                        _remove_state_tree(state, APP_NAME, expected=app_stamp)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                if receipt_written and not rollback_errors:
                    try:
                        current = _read_receipt(state)
                        if current is None or receipt is None or current[0] != receipt:
                            raise ProtocolStateError(
                                "install receipt ownership changed during rollback"
                            )
                        unlink_at(state, RECEIPT_NAME)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                elif not app_installed and not rollback_errors:
                    try:
                        if _entry_stamp(state, staging_name) is not None:
                            _remove_state_tree(state, staging_name)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                if rollback_errors:
                    raise ProtocolHandlerError(
                        "custom-protocol install failed and rollback requires manual cleanup"
                    ) from error
                _raise_lifecycle_error(error, "install")
    except BaseException as error:
        _raise_lifecycle_error(error, "install")
    raise AssertionError("unreachable")


def remove(
    state_directory: Path,
    *,
    confirmation: str,
    runner: Runner = subprocess.run,
) -> bool:
    """Unregister and remove only a receipt-and-marker-owned fixture app."""

    _require_macos()
    _require_confirmation(confirmation, REMOVE_CONFIRMATION)
    root = Path(os.path.abspath(state_directory))
    if not os.path.lexists(root):
        if _registered_handler_paths(runner=runner):
            raise ProtocolHandlerError(
                "a custom-protocol registration exists without owned state"
            )
        return False
    try:
        with locked_state(root, create=False) as state:
            if state is None:
                paths = _registered_handler_paths(runner=runner)
                if paths:
                    raise ProtocolHandlerError(
                        "a custom-protocol registration exists without owned state"
                    )
                return False
            app_path = _app_path(state)
            paths = _registered_handler_paths(runner=runner)
            _assert_registration_scope(paths, app_path)
            names = _state_names(state)
            if _transaction_names(names):
                raise ProtocolHandlerError(
                    "a prior custom-protocol transaction requires manual cleanup"
                )
            app_present = APP_NAME in names
            receipt_present = RECEIPT_NAME in names
            if not app_present and not receipt_present:
                if paths:
                    raise ProtocolHandlerError(
                        "LaunchServices retains an unowned fixture registration"
                    )
                return False
            if not app_present or not receipt_present:
                raise ProtocolHandlerError(
                    "the custom-protocol installation is incomplete and needs repair"
                )
            receipt_value = _read_receipt(state)
            if receipt_value is None or not native_handler.owns_app(
                app_path, app_path, receipt_value[0], state
            ):
                raise ProtocolHandlerError(
                    "receipt and marker do not authorize custom-protocol removal"
                )
            app_stamp = _entry_stamp(state, APP_NAME)
            if app_stamp is None:
                raise ProtocolHandlerError(
                    "the owned custom-protocol app disappeared before removal"
                )
            was_registered = paths == _expected_registration(app_path)
            if paths and not was_registered:
                raise ProtocolHandlerError(
                    "the custom-protocol registration is not exact"
                )
            if was_registered:
                _unregister_exact(app_path, runner=runner)

            backup_name = _RECEIPT_REMOVAL_PREFIX + secrets.token_hex(16) + ".json"
            receipt_moved = False
            try:
                if not _receipt_still_matches(state, receipt_value) or not native_handler.owns_app(
                    app_path, app_path, receipt_value[0], state
                ):
                    raise ProtocolHandlerError(
                        "custom-protocol ownership changed during removal"
                    )
                current_stamp = _entry_stamp(state, APP_NAME)
                if current_stamp is None or not _same_identity(current_stamp, app_stamp):
                    raise ProtocolHandlerError(
                        "custom-protocol app identity changed during removal"
                    )
                rename_at(state, RECEIPT_NAME, backup_name)
                receipt_moved = True
                backup = read_at(state, backup_name)
                if backup is None or backup[0] != receipt_value[1]:
                    raise ProtocolHandlerError(
                        "custom-protocol receipt backup did not verify"
                    )
                _remove_state_tree(state, APP_NAME, expected=current_stamp)
                unlink_at(state, backup_name)
                receipt_moved = False
                return True
            except BaseException as error:
                rollback_errors = []
                if receipt_moved:
                    try:
                        if _entry_stamp(state, RECEIPT_NAME) is not None:
                            raise ProtocolStateError(
                                "receipt path was occupied during removal rollback"
                            )
                        rename_at(state, backup_name, RECEIPT_NAME)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                if was_registered:
                    try:
                        _restore_registration(app_path, True, runner=runner)
                    except BaseException as rollback_error:
                        rollback_errors.append(rollback_error)
                if rollback_errors:
                    raise ProtocolHandlerError(
                        "custom-protocol removal failed and rollback requires manual cleanup"
                    ) from error
                _raise_lifecycle_error(error, "removal")
    except BaseException as error:
        _raise_lifecycle_error(error, "removal")
    raise AssertionError("unreachable")


def _empty_status() -> dict[str, object]:
    return {
        "schemaVersion": STATUS_SCHEMA_VERSION,
        "supported": sys.platform == "darwin",
        "installed": False,
        "registered": False,
        "registryReadable": False,
        "registeredPathCount": None,
        "foreignRegistrationPresent": False,
        "statePresent": False,
        "stateReadable": True,
        "appPresent": False,
        "receiptPresent": False,
        "receiptReadable": True,
        "explicitConsentRecorded": False,
        "appIntegrityValid": False,
        "transactionArtifactPresent": False,
        "needsRepair": False,
        "operationBlocked": sys.platform != "darwin",
        "bundleIdentifier": BUNDLE_ID,
        "scheme": SCHEME,
        "acceptedUrl": ACCEPTED_URL,
    }


def status(
    state_directory: Path,
    *,
    runner: Runner = subprocess.run,
) -> Mapping[str, object]:
    """Return a privacy-safe status; never disclose registered handler paths."""

    result = _empty_status()
    paths: FrozenSet[str] = frozenset()
    if sys.platform == "darwin":
        try:
            paths = _registered_handler_paths(runner=runner)
            result["registryReadable"] = True
            result["registeredPathCount"] = len(paths)
        except ProtocolHandlerError:
            result["registryReadable"] = False
            result["operationBlocked"] = True

    root = Path(os.path.abspath(state_directory))
    dedicated = root / STATE_DIRECTORY_NAME
    try:
        os.lstat(dedicated)
        result["statePresent"] = True
    except FileNotFoundError:
        if paths:
            result["foreignRegistrationPresent"] = True
            result["operationBlocked"] = True
        return result
    except OSError:
        result["statePresent"] = True
        result["stateReadable"] = False
        result["needsRepair"] = True
        result["operationBlocked"] = True
        return result

    try:
        with locked_state(root, create=False) as state:
            if state is None:
                result["stateReadable"] = False
                result["needsRepair"] = True
                result["operationBlocked"] = True
                return result
            names = _state_names(state)
            app_present = APP_NAME in names
            receipt_present = RECEIPT_NAME in names
            transactions = _transaction_names(names)
            result["appPresent"] = app_present
            result["receiptPresent"] = receipt_present
            result["transactionArtifactPresent"] = bool(transactions)
            app_path = _app_path(state)
            expected_paths = _expected_registration(app_path)
            registered = bool(paths) and paths == expected_paths
            foreign = any(path not in expected_paths for path in paths)
            result["registered"] = registered
            result["foreignRegistrationPresent"] = foreign

            receipt: Optional[Mapping[str, object]] = None
            if receipt_present:
                try:
                    value = _read_receipt(state)
                    receipt = None if value is None else value[0]
                except ProtocolStateError:
                    result["receiptReadable"] = False
            if receipt is not None:
                result["explicitConsentRecorded"] = native_handler.valid_receipt(
                    receipt, app_path, state
                )
            if app_present and receipt is not None and sys.platform == "darwin":
                result["appIntegrityValid"] = native_handler.valid_app(
                    app_path, app_path, receipt, state, runner=runner
                )

            result["installed"] = bool(
                result["registryReadable"]
                and registered
                and result["explicitConsentRecorded"]
                and result["appIntegrityValid"]
                and not transactions
                and not foreign
            )
            result["needsRepair"] = bool(
                transactions
                or app_present != receipt_present
                or (receipt_present and receipt is None)
                or (
                    app_present
                    and receipt is not None
                    and sys.platform == "darwin"
                    and not result["appIntegrityValid"]
                )
                or (registered and not result["installed"])
            )
            result["operationBlocked"] = bool(
                result["operationBlocked"]
                or foreign
                or result["needsRepair"]
                or (sys.platform == "darwin" and not result["registryReadable"])
            )
            return result
    except (ProtocolStateError, OSError):
        result["stateReadable"] = False
        result["needsRepair"] = True
        result["operationBlocked"] = True
        if paths:
            result["foreignRegistrationPresent"] = True
        return result


def is_installed(state_directory: Path, *, runner: Runner = subprocess.run) -> bool:
    """Fail closed for cleanup when any owned or ambiguous artifact remains."""

    value = status(state_directory, runner=runner)
    return bool(
        value["installed"]
        or value["appPresent"]
        or value["receiptPresent"]
        or value["transactionArtifactPresent"]
        or value["needsRepair"]
        or value["foreignRegistrationPresent"]
        or (
            sys.platform == "darwin"
            and not value["registryReadable"]
        )
    )
