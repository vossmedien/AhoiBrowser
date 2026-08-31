import hashlib
import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import ubo_release_attestation as attestation  # noqa: E402
import ubo_attestation_crx3 as crx3  # noqa: E402


class UboReleaseAttestationContractTests(unittest.TestCase):
    def test_reviewed_identity_matches_repository_pin(self):
        pins = attestation.load_pins()
        self.assertEqual("1.74.0", pins.version)
        self.assertEqual(
            "6dd2d95e50d134a477a4e183343c0b26e9147123",
            pins.release_commit,
        )
        self.assertEqual(
            "b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e",
            pins.package_sha256,
        )
        self.assertEqual(
            "5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf",
            pins.public_key_sha256,
        )
        self.assertEqual("fkgkibajhfbepljeaefdnfnegdcjomkh", pins.extension_id)

    def test_chromium_id_derivation_uses_first_128_key_hash_bits(self):
        public_key = b"synthetic public key"
        expected = "".join(
            chr(ord("a") + int(character, 16))
            for character in hashlib.sha256(public_key).hexdigest()[:32]
        )
        self.assertEqual(expected, crx3.derive_extension_id(public_key))

    def test_redirect_contract_accepts_only_one_exact_302_asset_location(self):
        final_url = (
            "https://release-assets.githubusercontent.com"
            f"{attestation.EXPECTED_ASSET_PATH}?public-transient-signature=fixture"
        )
        self.assertEqual(
            final_url,
            attestation.validate_response_chain(
                attestation.EXPECTED_PACKAGE_URL, 302, [final_url]
            ),
        )
        rejected = (
            (301, [final_url]),
            (302, []),
            (302, [final_url, final_url]),
            (302, [final_url.replace(attestation.EXPECTED_ASSET_HOST, "example.com")]),
            (302, [final_url.split("?", 1)[0]]),
            (302, [final_url.replace("https://", "https://user:fixture@")]),
        )
        for status, locations in rejected:
            with self.subTest(status=status, locations=locations):
                with self.assertRaises(attestation.AttestationError):
                    attestation.validate_response_chain(
                        attestation.EXPECTED_PACKAGE_URL, status, locations
                    )

    def test_protobuf_parser_rejects_unknown_wire_types_and_noncanonical_varints(self):
        with self.assertRaises(attestation.AttestationError):
            crx3._protobuf_bytes_fields(b"\x08\x01", "fixture")
        with self.assertRaises(attestation.AttestationError):
            crx3._protobuf_bytes_fields(b"\x8a\x00\x00", "fixture")
        self.assertEqual(
            {1: [b"x"]},
            crx3._protobuf_bytes_fields(b"\x0a\x01x", "fixture"),
        )

    def test_atomic_publication_refuses_to_overwrite_a_receipt(self):
        with tempfile.TemporaryDirectory(
            dir=ROOT, prefix="ubo-attestation-test-"
        ) as raw:
            output = pathlib.Path(raw) / "receipt.json"
            attestation.publish_atomic(output, {"schemaVersion": 1})
            self.assertEqual(
                {"schemaVersion": 1},
                json.loads(output.read_text(encoding="utf-8")),
            )
            with self.assertRaises(attestation.AttestationError):
                attestation.publish_atomic(output, {"schemaVersion": 2})

    def test_tool_has_no_configurable_url_or_persistent_crx_output(self):
        option_strings = {
            option
            for action in attestation.parser()._actions
            for option in action.option_strings
        }
        self.assertNotIn("--url", option_strings)
        self.assertNotIn("--crx-output", option_strings)
        self.assertIn("--build-provenance", option_strings)
        self.assertIn("--install-receipt", option_strings)


if __name__ == "__main__":
    unittest.main()
