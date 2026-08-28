import hashlib
import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import measure_lean_bundles  # noqa: E402


def load_json(relative: str):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class LeanChromiumContractTests(unittest.TestCase):
    def test_full_profiles_are_byte_exact_pre_wave_one_baselines(self):
        matrix = load_json("config/lean-chromium-components.json")
        allowed_delta = matrix["fullBaselineContract"]["allowedLeanDelta"]
        pairs = (
            (
                "ahoi-dev.gn",
                "ahoi-full-dev.gn",
                "49b306d09fa8b6aa17b654d63527fe042eef77abf7f30b3d8727bc67d0c9e48d",
            ),
            (
                "ahoi-release.gn",
                "ahoi-full-release.gn",
                "cf144d4dcfb41383b3e863c363de0d89568a09baa06219b64e5627c516b0bf4c",
            ),
        )
        for lean_name, full_name, baseline_hash in pairs:
            lean = ROOT / "config/build" / lean_name
            full = ROOT / "config/build" / full_name
            with self.subTest(profile=lean_name):
                self.assertEqual(baseline_hash, sha256(full))
                lean_lines = lean.read_text(encoding="utf-8").splitlines()
                self.assertEqual(allowed_delta, lean_lines[-len(allowed_delta) :])
                self.assertEqual(
                    full.read_text(encoding="utf-8").splitlines(),
                    lean_lines[: -len(allowed_delta)],
                )

    def test_wave_one_has_exactly_two_active_exclusions(self):
        matrix = load_json("config/lean-chromium-components.json")
        self.assertEqual(
            ["compose", "pdf-save-to-drive"], matrix["scope"]["activeExclusions"]
        )
        decisions = {
            component["id"]: component["decision"]
            for component in matrix["components"]
        }
        self.assertEqual(
            {
                "lens-desktop",
                "compose",
                "structured-metrics",
                "pdf-save-to-drive",
                "legacy-platform-apps",
                "legacy-hosted-apps",
            },
            set(decisions),
        )
        self.assertEqual(
            {"compose", "pdf-save-to-drive"},
            {
                component_id
                for component_id, decision in decisions.items()
                if decision == "exclude-from-lean-profiles"
            },
        )
        for profile_name in ("ahoi-dev.gn", "ahoi-release.gn"):
            profile = (ROOT / "config/build" / profile_name).read_text(
                encoding="utf-8"
            )
            self.assertEqual(1, profile.count("enable_compose = false"))
            self.assertEqual(1, profile.count("enable_pdf_save_to_drive = false"))
            for protected_arg in (
                "enable_extensions",
                "enable_pdf =",
                "enable_printing",
                "safe_browsing_mode",
            ):
                self.assertNotIn(protected_arg, profile)

    def test_matrix_test_ids_resolve_to_the_registry(self):
        matrix = load_json("config/lean-chromium-components.json")
        registry = load_json("config/test-registry.json")
        known_test_ids = {entry["id"] for entry in registry["tests"]}
        referenced = set(matrix["verificationPolicy"]["testIds"])
        referenced.update(matrix["rollPolicy"]["testIds"])
        for capability in matrix["protectedCapabilities"]:
            referenced.update(capability["testIds"])
        for component in matrix["components"]:
            referenced.update(component["testIds"])
        self.assertTrue(referenced)
        self.assertEqual(set(), referenced - known_test_ids)

    def test_measurement_manifest_binds_profile_hashes_and_matrix_pin(self):
        matrix = load_json("config/lean-chromium-components.json")
        manifest = load_json("config/lean-bundle-measurement.json")
        expected_receipts = {
            "upstream-control": (
                "chromium/src/out/AhoiUpstreamRelease/args.gn",
                "artifacts/build/upstream-build.json",
                "unmodified-upstream-control",
            ),
            "ahoi-full-release": (
                "chromium/src/out/AhoiFullRelease/args.gn",
                "artifacts/build/ahoi-full-release-build.json",
                "ahoi-full-release",
            ),
            "ahoi-release": (
                "chromium/src/out/AhoiRelease/args.gn",
                "artifacts/build/ahoi-release-build.json",
                "ahoi-release",
            ),
        }
        self.assertEqual(matrix["chromium"], manifest["chromium"])
        self.assertEqual(
            set(expected_receipts),
            {profile["id"] for profile in manifest["profiles"]},
        )
        for profile in manifest["profiles"]:
            with self.subTest(profile=profile["id"]):
                self.assertEqual(
                    expected_receipts[profile["id"]],
                    (
                        profile["generatedArgsPath"],
                        profile["receiptPath"],
                        profile["expectedReceiptKind"],
                    ),
                )
                self.assertEqual(
                    profile["expectedArgsSha256"],
                    sha256(ROOT / profile["argsPath"]),
                )
        self.assertEqual(
            {
                "passWhen": "all-comparisons-pass",
                "otherwise": "product-decision-required",
                "testIds": ["LEAN-06"],
            },
            manifest["gate"],
        )
        measure_lean_bundles.validate_manifest(manifest)

        for field, invalid_value in (
            ("passWhen", "any-comparison-passes"),
            ("otherwise", "warn-only"),
        ):
            invalid = json.loads(json.dumps(manifest))
            invalid["gate"][field] = invalid_value
            with self.subTest(field=field), self.assertRaises(SystemExit):
                measure_lean_bundles.validate_manifest(invalid)

    def test_roll_checks_cover_every_m152_feature_reference(self):
        matrix = load_json("config/lean-chromium-components.json")
        components = {
            component["id"]: component for component in matrix["components"]
        }
        expected_paths = {
            "compose": {
                "build/config/android/internal_rules.gni",
                "chrome/browser/BUILD.gn",
                "chrome/browser/compose/BUILD.gn",
                "chrome/browser/resources/BUILD.gn",
                "chrome/browser/ui/BUILD.gn",
                "chrome/browser/ui/android/bricks/internal/BUILD.gn",
                "chrome/browser/ui/autofill/BUILD.gn",
                "chrome/browser/ui/webui/BUILD.gn",
                "chrome/browser/ui/webui/compose/BUILD.gn",
                "chrome/chrome_paks.gni",
                "chrome/common/features.gni",
                "chrome/test/BUILD.gn",
                "chrome/test/data/webui/BUILD.gn",
                "components/BUILD.gn",
                "components/compose/BUILD.gn",
                "components/compose/features.gni",
                "components/segmentation_platform/embedder/default_model/BUILD.gn",
            },
            "pdf-save-to-drive": {
                "chrome/browser/extensions/api/pdf_viewer_private/BUILD.gn",
                "chrome/browser/resources/pdf/BUILD.gn",
                "chrome/browser/save_to_drive/BUILD.gn",
                "chrome/browser/ui/BUILD.gn",
                "chrome/browser/ui/hats/BUILD.gn",
                "chrome/browser/ui/save_to_drive/BUILD.gn",
                "chrome/browser/ui/views/save_to_drive/BUILD.gn",
                "chrome/common/BUILD.gn",
                "chrome/common/features.gni",
                "chrome/test/BUILD.gn",
                "chrome/test/data/pdf/BUILD.gn",
                "components/strings/BUILD.gn",
                "pdf/BUILD.gn",
                "pdf/features.gni",
                "pdf/mojom/BUILD.gn",
            },
        }
        for component_id, expected in expected_paths.items():
            roll_check = components[component_id]["rollCheck"]
            actual = set(roll_check["declarations"])
            actual.update(roll_check["guardPaths"])
            actual.update(roll_check["assertionPaths"])
            if "buildflagPath" in roll_check:
                actual.add(roll_check["buildflagPath"])
            with self.subTest(component=component_id):
                self.assertEqual(expected, actual)

    def test_measurement_fails_closed_on_provenance_and_size_gate(self):
        source = (ROOT / "tools/measure_lean_bundles.py").read_text(
            encoding="utf-8"
        )
        for marker in (
            'receipt.get("schemaVersion") != 2',
            "actual_bundle_sha256 = bundle_hash(bundle)",
            'source.get("repositoryDirty") is not False',
            'source.get("repositoryCommit") != repository_commit',
            'source.get("chromiumDepsSha256") != expected_deps_sha256',
            'raise SystemExit("current Chromium checkout differs from the pin")',
            'raise SystemExit("current depot_tools checkout differs from the pin")',
            'raise SystemExit("current Chromium .gclient differs from the '
            'canonical config")',
            "if toolchain != expected_toolchain",
            "if build_tools != expected_build_tools",
            '("overlayFingerprint", "checkoutDeltaFingerprint")',
            '"sha256": sha256_file(receipt_path)',
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)
        report_write = source.index("atomic_write_json(output_path.resolve(), report)")
        gate_exit = source.index('return 0 if gate_status == "PASS" else 2')
        self.assertLess(report_write, gate_exit)

    def test_bundle_inventory_is_repeatable_and_classifies_entries(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-lean-measure-") as directory:
            bundle = pathlib.Path(directory) / "Fixture.app"
            resources = bundle / "Contents/Resources"
            frameworks = bundle / "Contents/Frameworks"
            resources.mkdir(parents=True)
            frameworks.mkdir(parents=True)
            (resources / "value.txt").write_bytes(b"deterministic")
            (frameworks / "fixture.dylib").write_bytes(
                bytes.fromhex("cffaedfe") + b"fixture"
            )
            (bundle / "Contents/current").symlink_to("Resources/value.txt")

            first = measure_lean_bundles.inventory_bundle(bundle)
            second = measure_lean_bundles.inventory_bundle(bundle)
            self.assertEqual(first, second)
            self.assertEqual(2, first["regularFileCount"])
            self.assertEqual(1, first["symlinkCount"])
            self.assertEqual(1, first["categories"]["resources"]["regularFileCount"])
            self.assertEqual(
                1,
                first["categories"]["frameworks-and-libraries"][
                    "regularFileCount"
                ],
            )
            self.assertEqual(1, first["categories"]["mach-o"]["regularFileCount"])

    def test_full_release_cannot_enter_the_shipping_release_chain(self):
        signer = (ROOT / "tools/release/signing.py").read_text(encoding="utf-8")
        chain = (ROOT / "tools/release/chain.py").read_text(encoding="utf-8")
        installed_verifier = (ROOT / "scripts/verify-installed-app.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn('build_provenance.get("kind") != "ahoi-release"', signer)
        self.assertIn('build.get("kind") != "ahoi-release"', chain)
        self.assertIn('"buildProfile": "release"', chain)
        self.assertIn('[ "${installed_profile}" = "release" ]', installed_verifier)
        self.assertNotIn("full-release", signer)
        self.assertNotIn("full-release", chain)
        self.assertNotIn("full-release", installed_verifier)


if __name__ == "__main__":
    unittest.main()
