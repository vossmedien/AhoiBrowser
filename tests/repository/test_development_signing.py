import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]

import sys

sys.path.insert(0, str(ROOT / "tools"))

from development_signing import (  # noqa: E402
    CodeSigningIdentity,
    DevelopmentSigningError,
    parse_identities,
    select_identity,
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


if __name__ == "__main__":
    unittest.main()
