import pathlib
import base64
import hashlib
import os
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]

import sys

sys.path.insert(0, str(ROOT / "tools"))

from development_signing import (  # noqa: E402
    CodeSigningIdentity,
    DevelopmentSigningError,
    parse_identities,
    select_identity,
    resolve_from_environment,
    verify_explicit_certificate,
)


APPLE_IDENTITY = "Apple Development: Ahoi Developer (ABCDEFGHIJ)"


class DevelopmentSigningTests(unittest.TestCase):
    def test_security_output_parser_accepts_only_valid_identity_rows(self):
        output = f'''  1) {"1" * 40} "{APPLE_IDENTITY}"
  2) {"2" * 40} "Developer ID Application: Ahoi (ABCDEFGHIJ)"
     2 valid identities found
'''
        self.assertEqual(
            (
                CodeSigningIdentity("1" * 40, APPLE_IDENTITY),
                CodeSigningIdentity(
                    "2" * 40,
                    "Developer ID Application: Ahoi (ABCDEFGHIJ)",
                ),
            ),
            parse_identities(output),
        )

    def test_unique_apple_development_identity_is_selected(self):
        identities = parse_identities(
            f'1) {"A" * 40} "{APPLE_IDENTITY}"\n'
        )
        self.assertEqual(APPLE_IDENTITY, select_identity(identities))

    def test_configured_identity_must_exist_and_be_for_development(self):
        identities = (CodeSigningIdentity("A" * 40, APPLE_IDENTITY),)
        self.assertEqual(
            APPLE_IDENTITY,
            select_identity(identities, configured=APPLE_IDENTITY),
        )
        with self.assertRaisesRegex(DevelopmentSigningError, "not valid"):
            select_identity(
                identities,
                configured="Apple Development: Other (ABCDEFGHIJ)",
            )
        with self.assertRaisesRegex(DevelopmentSigningError, "reserved"):
            select_identity(
                identities,
                configured="Developer ID Application: Ahoi (ABCDEFGHIJ)",
            )

    def test_missing_or_ambiguous_identity_fails_closed(self):
        with self.assertRaisesRegex(DevelopmentSigningError, "no valid"):
            select_identity(())
        with self.assertRaisesRegex(DevelopmentSigningError, "multiple"):
            select_identity(
                (
                    CodeSigningIdentity("A" * 40, APPLE_IDENTITY),
                    CodeSigningIdentity(
                        "B" * 40,
                        "Apple Development: Other (KLMNOPQRST)",
                    ),
                )
            )

    def test_adhoc_signing_requires_an_explicit_opt_in(self):
        with self.assertRaisesRegex(DevelopmentSigningError, "requires"):
            select_identity((), configured="-")
        self.assertEqual("-", select_identity((), allow_adhoc=True))
        self.assertEqual(
            "-",
            select_identity((), configured="-", allow_adhoc=True),
        )

    def test_explicit_keychain_requires_verified_exact_identity(self):
        identity = CodeSigningIdentity("A" * 40, APPLE_IDENTITY)
        with tempfile.NamedTemporaryFile() as keychain, mock.patch.dict(
            os.environ, {"AHOI_DEV_CODESIGN_KEYCHAIN": keychain.name,
                         "AHOI_DEV_CODESIGN_IDENTITY": APPLE_IDENTITY},
            clear=True,
        ), mock.patch("development_signing.read_security_identities",
                      side_effect=[(), (identity,)]) as read, mock.patch(
            "development_signing.verify_explicit_certificate",
        ) as verify:
            self.assertEqual(APPLE_IDENTITY, resolve_from_environment())
            self.assertEqual([
                mock.call(keychain=keychain.name),
                mock.call(keychain=keychain.name, valid_only=False),
            ], read.call_args_list)
            verify.assert_called_once_with(identity, keychain.name)

    def test_invalid_explicit_certificate_cannot_be_selected(self):
        identity = CodeSigningIdentity("A" * 40, APPLE_IDENTITY)
        with tempfile.NamedTemporaryFile() as keychain, mock.patch.dict(
            os.environ, {"AHOI_DEV_CODESIGN_KEYCHAIN": keychain.name,
                         "AHOI_DEV_CODESIGN_IDENTITY": APPLE_IDENTITY},
            clear=True,
        ), mock.patch("development_signing.read_security_identities",
                      side_effect=[(), (identity,)]), mock.patch(
            "development_signing.verify_explicit_certificate",
            side_effect=DevelopmentSigningError("trust rejected"),
        ):
            with self.assertRaisesRegex(DevelopmentSigningError, "trust rejected"):
                resolve_from_environment()

    def test_keychain_alone_does_not_enable_unvalidated_matching_identity(self):
        with tempfile.NamedTemporaryFile() as keychain, mock.patch.dict(
            os.environ, {"AHOI_DEV_CODESIGN_KEYCHAIN": keychain.name},
            clear=True,
        ), mock.patch("development_signing.read_security_identities",
                      return_value=()) as read:
            with self.assertRaisesRegex(DevelopmentSigningError, "no valid"):
                resolve_from_environment()
            read.assert_called_once_with(keychain=keychain.name)

    def test_certificate_binding_and_native_trust_are_both_required(self):
        der = b"synthetic public leaf"
        pem = ("-----BEGIN CERTIFICATE-----\n"
               + base64.b64encode(der).decode()
               + "\n-----END CERTIFICATE-----\n")
        identity = CodeSigningIdentity(
            hashlib.sha1(der).hexdigest().upper(), APPLE_IDENTITY,
        )
        for trust_exit in (0, 1):
            with self.subTest(trust_exit=trust_exit), mock.patch(
                "development_signing.subprocess.run", side_effect=[
                    subprocess.CompletedProcess([], 0, pem, ""),
                    subprocess.CompletedProcess([], trust_exit, "", ""),
                ],
            ) as run:
                if trust_exit:
                    with self.assertRaisesRegex(DevelopmentSigningError, "trust"):
                        verify_explicit_certificate(identity, "/keychain")
                else:
                    verify_explicit_certificate(identity, "/keychain")
                command = run.call_args_list[1].args[0]
                self.assertIn("codeSign", command)
                self.assertFalse(pathlib.Path(command[command.index("-c") + 1]).exists())
        with mock.patch("development_signing.subprocess.run", return_value=
                        subprocess.CompletedProcess([], 0, pem, "")) as run:
            with self.assertRaisesRegex(DevelopmentSigningError, "exact"):
                verify_explicit_certificate(
                    CodeSigningIdentity("B" * 40, APPLE_IDENTITY), "/keychain",
                )
            self.assertEqual(1, run.call_count)


if __name__ == "__main__":
    unittest.main()
