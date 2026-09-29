#!/usr/bin/env python3
"""Custom-protocol fixture handler: native source, ownership and removal."""

from __future__ import annotations

import contextlib
import hashlib
import json
import plistlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Mapping
from unittest import mock


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

import custom_protocol  # noqa: E402
import protocol_native_handler as native_handler  # noqa: E402
from protocol_state import locked_state  # noqa: E402


def _owned_app(state, app_path: Path, installation_id: str) -> Mapping[str, object]:
    """Builds an app tree whose marker and receipt match `state` exactly."""
    executable_directory = app_path / "Contents" / "MacOS"
    resources = app_path / "Contents" / "Resources"
    executable_directory.mkdir(parents=True)
    resources.mkdir(parents=True)
    with (app_path / "Contents" / "Info.plist").open("wb") as stream:
        plistlib.dump(dict(native_handler._info()), stream, sort_keys=True)
    executable = executable_directory / native_handler.EXECUTABLE_NAME
    executable.write_bytes(b"compiled-handler")
    marker = native_handler.expected_marker(
        app_path,
        installation_id,
        state,
        hashlib.sha256(b"compiled-handler").hexdigest(),
        "b" * 64,
    )
    marker_path = resources / native_handler.MARKER_NAME
    marker_path.write_text(json.dumps(marker, sort_keys=True) + "\n", encoding="utf-8")
    marker_path.chmod(0o600)
    return {
        "schemaVersion": native_handler.RECEIPT_SCHEMA_VERSION,
        "managedBy": native_handler.MANAGED_BY,
        "installationId": installation_id,
        "explicitConsent": True,
        "stateId": state.identity["stateId"],
        "stateMarkerSha256": hashlib.sha256(state.marker_payload).hexdigest(),
        "appPath": str(app_path),
        "bundleIdentifier": native_handler.BUNDLE_ID,
        "scheme": native_handler.SCHEME,
        "acceptedUrl": native_handler.ACCEPTED_URL,
        "artifactHashes": dict(native_handler.artifact_hashes(app_path)),
    }


def _codesign_runner(command, **_kwargs):
    detail = "Identifier=%s\n" % native_handler.BUNDLE_ID if "-d" in command else ""
    return subprocess.CompletedProcess(command, 0, "", detail)


class CustomProtocolUnitTests(unittest.TestCase):
    def test_native_source_accepts_one_url_without_forwarding_it(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            with locked_state(Path(temporary), create=True) as state:
                source = native_handler.native_source(state).decode("utf-8")
        # The accepted URL is only a comparison constant: the handler records
        # a fixed event on an exact match and never forwards or stores the
        # incoming value.
        self.assertIn(native_handler._octal_literal(native_handler.ACCEPTED_URL), source)
        self.assertIn("[incoming isEqualToString:accepted]", source)
        self.assertIn("exact-custom-protocol-open", source)
        self.assertIn('\\"incomingValueRetained\\":false', source)
        self.assertIn("O_NOFOLLOW", source)
        for forwarding in ("openURL", "NSWorkspace", "system(", "popen(", "execv"):
            self.assertNotIn(forwarding, source)
        self.assertNotIn(str(Path(custom_protocol.__file__).resolve()), source)

    def test_existing_handler_requires_exact_hashes_and_signature(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            root = Path(temporary)
            with locked_state(root, create=True) as state:
                app_path = root / "app" / custom_protocol.APP_NAME
                receipt = _owned_app(state, app_path, "1" * 32)
                self.assertTrue(
                    native_handler.valid_app(
                        app_path, app_path, receipt, state, runner=_codesign_runner
                    )
                )
                executable = (
                    app_path / "Contents" / "MacOS" / native_handler.EXECUTABLE_NAME
                )
                executable.write_bytes(b"tampered-handler")
                self.assertFalse(
                    native_handler.valid_app(
                        app_path, app_path, receipt, state, runner=_codesign_runner
                    )
                )

    def test_receipt_without_an_exact_matching_marker_cannot_own_mutation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            root = Path(temporary)
            with locked_state(root, create=True) as state:
                app_path = root / "app" / custom_protocol.APP_NAME
                receipt = _owned_app(state, app_path, "2" * 32)
                self.assertTrue(
                    native_handler.owns_app(app_path, app_path, receipt, state)
                )
                marker_path = (
                    app_path / "Contents" / "Resources" / native_handler.MARKER_NAME
                )
                marker_path.write_text("{}\n", encoding="utf-8")
                self.assertFalse(
                    native_handler.owns_app(app_path, app_path, receipt, state)
                )
                marker_path.unlink()
                self.assertFalse(
                    native_handler.owns_app(app_path, app_path, receipt, state)
                )

    def test_removal_rechecks_ownership_and_restores_registration(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            with locked_state(directory, create=True) as state:
                app_path = custom_protocol._app_path(state)
            expected_paths = frozenset((str(app_path),))
            receipt_value = ({"installationId": "5" * 32}, b"{}")
            with contextlib.ExitStack() as patches:
                patches.enter_context(mock.patch.object(custom_protocol.sys, "platform", "darwin"))
                for name, value in (
                    ("_registered_handler_paths", expected_paths),
                    ("_expected_registration", expected_paths),
                    ("_state_names",
                     frozenset((custom_protocol.APP_NAME, custom_protocol.RECEIPT_NAME))),
                    ("_read_receipt", receipt_value),
                    ("_entry_stamp", object()),
                    ("_receipt_still_matches", True),
                ):
                    patches.enter_context(
                        mock.patch.object(custom_protocol, name, return_value=value)
                    )
                patches.enter_context(
                    mock.patch.object(
                        custom_protocol.native_handler, "owns_app", side_effect=(True, False)
                    )
                )
                unregister = patches.enter_context(
                    mock.patch.object(custom_protocol, "_unregister_exact")
                )
                restore = patches.enter_context(
                    mock.patch.object(custom_protocol, "_restore_registration")
                )
                remove_tree = patches.enter_context(
                    mock.patch.object(custom_protocol, "_remove_state_tree")
                )
                with self.assertRaisesRegex(
                    custom_protocol.ProtocolHandlerError, "ownership changed"
                ):
                    custom_protocol.remove(
                        directory, confirmation=custom_protocol.REMOVE_CONFIRMATION
                    )
            unregister.assert_called_once_with(app_path, runner=subprocess.run)
            restore.assert_called_once_with(app_path, True, runner=subprocess.run)
            remove_tree.assert_not_called()

    def test_launchservices_paths_are_exact_and_foreign_claims_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            expected = directory / custom_protocol.APP_NAME
            foreign = directory / "other-state" / custom_protocol.APP_NAME
            dump = (
                "------------------------------\n"
                "bundle id: %s\nbundle path: %s\nbindings: %s:\n"
                "------------------------------\n"
                "identifier: foreign.bundle\npath: %s\nschemes: %s\n"
                % (
                    custom_protocol.BUNDLE_ID,
                    expected,
                    custom_protocol.SCHEME,
                    foreign,
                    custom_protocol.SCHEME,
                )
            )
            paths = custom_protocol._parse_registered_handler_paths(dump)
            self.assertEqual({str(expected), str(foreign)}, set(paths))
            with self.assertRaisesRegex(
                custom_protocol.ProtocolHandlerError, "foreign"
            ):
                custom_protocol._assert_registration_scope(paths, expected)
            custom_protocol._assert_registration_scope(
                frozenset((str(expected),)), expected
            )

    def test_launchservices_claim_without_path_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            custom_protocol.ProtocolHandlerError, "without an exact path"
        ):
            custom_protocol._parse_registered_handler_paths(
                "bundle id: %s\nbindings: %s:\n"
                % (custom_protocol.BUNDLE_ID, custom_protocol.SCHEME)
            )

    def test_launchservices_noncanonical_path_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            custom_protocol.ProtocolHandlerError, "non-canonical"
        ):
            custom_protocol._parse_registered_handler_paths(
                "bundle id: %s\npath: /private/tmp/state/../other/%s\n"
                "bindings: %s:\n"
                % (
                    custom_protocol.BUNDLE_ID,
                    custom_protocol.APP_NAME,
                    custom_protocol.SCHEME,
                )
            )

    def test_status_reports_foreign_registration_without_disclosing_its_path(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            foreign = directory / "private-other-state" / custom_protocol.APP_NAME
            with mock.patch.object(
                custom_protocol.sys, "platform", "darwin"
            ), mock.patch.object(
                custom_protocol,
                "_registered_handler_paths",
                return_value=frozenset((str(foreign),)),
            ):
                result = custom_protocol.status(directory)
            self.assertFalse(result["installed"])
            self.assertTrue(result["foreignRegistrationPresent"])
            self.assertEqual(1, result["registeredPathCount"])
            self.assertNotIn(str(foreign), json.dumps(result, sort_keys=True))



if __name__ == "__main__":
    unittest.main()
