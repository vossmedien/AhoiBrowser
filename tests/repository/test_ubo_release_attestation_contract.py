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
    def candidate_receipts(self):
        source_commit = "1" * 40
        chromium_commit = "2" * 40
        legacy_bundle_hash = "3" * 64
        bundle_tree_hash = "4" * 64
        gn_args_hash = attestation.sha256_file(attestation.DEV_GN_PATH)
        build = {
            "schemaVersion": 2,
            "kind": "ahoi-dev",
            "app": {
                "sourceCommit": source_commit,
                "chromiumCommit": chromium_commit,
                "chromiumVersion": "152.0.7977.65",
                "gnArgsSha256": gn_args_hash,
                "buildProfile": "dev",
                "bundleSha256": legacy_bundle_hash,
                "bundleTreeSha256": bundle_tree_hash,
            },
            "source": {
                "repositoryCommit": source_commit,
                "repositoryDirty": False,
                "overlayFingerprint": attestation.overlay_and_patch_fingerprint(
                    ROOT
                ),
                "checkoutDeltaFingerprint": "5" * 64,
            },
        }
        installation = {
            "schemaVersion": 1,
            "kind": "development-installation-receipt",
            "releaseEvidenceEligible": False,
            "bundle": {
                "sourceCommit": source_commit,
                "chromiumCommit": chromium_commit,
                "chromiumVersion": "152.0.7977.65",
                "gnArgsSha256": gn_args_hash,
                "buildProfile": "dev",
                "bundleTreeSha256": bundle_tree_hash,
                "executableSha256": "6" * 64,
            },
            "installation": {
                "candidateBundleTreeSha256": bundle_tree_hash,
                "target": "/Applications/AhoiBrowser.app",
                "sameVolumeStaging": True,
                "processesQuiescent": True,
                "automaticRollbackOnVerificationFailure": True,
                "postInstallVerification": True,
            },
            "verification": {
                "candidateVerifiedBeforeStaging": True,
                "sameVolumeCopyVerified": True,
                "installedBundleVerifiedAfterActivation": True,
            },
        }
        return build, installation, legacy_bundle_hash, bundle_tree_hash

    def bind_receipts(self, root, build, installation, suffix):
        build_path = root / f"build-{suffix}.json"
        installation_path = root / f"installation-{suffix}.json"
        build_path.write_text(json.dumps(build), encoding="utf-8")
        installation_path.write_text(json.dumps(installation), encoding="utf-8")
        return attestation.bind_candidate(build_path, installation_path)

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

    def test_candidate_binding_uses_exact_tree_hash_and_fails_closed(self):
        build, installation, legacy_hash, tree_hash = self.candidate_receipts()
        with tempfile.TemporaryDirectory(
            dir=ROOT, prefix="ubo-attestation-test-"
        ) as raw:
            root = pathlib.Path(raw)
            candidate = self.bind_receipts(root, build, installation, "valid")
            self.assertNotEqual(legacy_hash, tree_hash)
            self.assertEqual(tree_hash, candidate["bundleTreeSha256"])

            legacy_installation = json.loads(json.dumps(installation))
            legacy_installation["bundle"]["bundleTreeSha256"] = legacy_hash
            legacy_installation["installation"][
                "candidateBundleTreeSha256"
            ] = legacy_hash
            with self.assertRaisesRegex(
                attestation.AttestationError,
                "candidate and atomic installation receipt are not bound",
            ):
                self.bind_receipts(
                    root, build, legacy_installation, "legacy-mismatch"
                )

            missing_tree_hash = json.loads(json.dumps(build))
            del missing_tree_hash["app"]["bundleTreeSha256"]
            with self.assertRaisesRegex(
                attestation.AttestationError,
                "candidate bundle-tree hash",
            ):
                self.bind_receipts(
                    root, missing_tree_hash, installation, "missing-tree-hash"
                )

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
