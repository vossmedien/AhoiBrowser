import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import build_provenance  # noqa: E402,F401
import chromium_dependencies  # noqa: E402


class ChromiumDependencyContractTests(unittest.TestCase):
    def manifests(self):
        source_commit = "1" * 40
        nested_commit = "2" * 40
        expected = {
            "src": {"url": "https://example.test/src.git", "revision": None},
            "src/nested": {
                "url": "https://example.test/nested.git",
                "revision": nested_commit,
            },
        }
        actual = {
            "src": {
                "url": "https://example.test/src.git",
                "revision": source_commit,
            },
            "src/nested": {
                "url": "https://example.test/nested.git",
                "revision": nested_commit,
            },
        }
        return source_commit, expected, actual

    def test_expected_and_actual_git_closure_matches(self):
        source_commit, expected, actual = self.manifests()
        chromium_dependencies.compare_expected_actual(
            expected, actual, source_commit
        )

    def test_wrong_nested_revision_is_rejected(self):
        source_commit, expected, actual = self.manifests()
        actual["src/nested"]["revision"] = "3" * 40
        with self.assertRaisesRegex(SystemExit, "dependency closure mismatch"):
            chromium_dependencies.compare_expected_actual(
                expected, actual, source_commit
            )

    def test_missing_nested_dependency_is_rejected(self):
        source_commit, expected, actual = self.manifests()
        actual.pop("src/nested")
        with self.assertRaisesRegex(SystemExit, "dependency is missing"):
            chromium_dependencies.compare_expected_actual(
                expected, actual, source_commit
            )

    def test_unexpected_actual_dependency_is_rejected(self):
        source_commit, expected, actual = self.manifests()
        actual["src/injected"] = {
            "url": "https://attacker.test/injected.git",
            "revision": "3" * 40,
        }
        with self.assertRaisesRegex(SystemExit, "unexpected actual"):
            chromium_dependencies.compare_expected_actual(
                expected, actual, source_commit
            )

    def test_abbreviated_git_pin_is_rejected(self):
        source_commit, expected, actual = self.manifests()
        expected["src/nested"]["revision"] = "2" * 12
        with self.assertRaisesRegex(SystemExit, "unpinned Git dependency"):
            chromium_dependencies.compare_expected_actual(
                expected, actual, source_commit
            )

    def test_gcs_objects_are_not_misclassified_as_cipd(self):
        source_commit, expected, actual = self.manifests()
        gcs_name = "src/gcs_dep:deadbeef"
        gcs_entry = {
            "url": "gs://chromium-fixture/deadbeef",
            "revision": None,
        }
        expected[gcs_name] = dict(gcs_entry)
        actual[gcs_name] = dict(gcs_entry)

        chromium_dependencies.compare_expected_actual(
            expected, actual, source_commit
        )
        self.assertEqual(set(), chromium_dependencies.resolved_cipd_pins(expected))
        self.assertEqual(set(), chromium_dependencies.actual_cipd_pins(actual))

    def test_gcs_url_or_object_drift_is_rejected(self):
        source_commit, expected, actual = self.manifests()
        name = "src/gcs_dep:deadbeef"
        expected[name] = {
            "url": "gs://chromium-fixture/deadbeef",
            "revision": None,
        }
        actual[name] = {
            "url": "gs://attacker-fixture/deadbeef",
            "revision": None,
        }
        with self.assertRaisesRegex(SystemExit, "GCS dependency closure mismatch"):
            chromium_dependencies.compare_expected_actual(
                expected, actual, source_commit
            )

    def test_full_cipd_version_is_recovered_from_plain_revinfo(self):
        expected = {
            "src": {
                "url": "https://example.test/src.git",
                "revision": None,
            },
            "src/tool:infra/tools/example/mac-${arch}": {
                "url": (
                    "https://chrome-infra-packages.appspot.com/"
                    "infra/tools/example/mac-${arch}"
                ),
                "revision": "version:2",
            },
            "src/data:object": {
                "url": "gs://chromium-fixture/object",
                "revision": None,
            },
        }
        plain = "\n".join(
            (
                "src: https://example.test/src.git",
                (
                    "src/tool:infra/tools/example/mac-${arch}: "
                    "https://chrome-infra-packages.appspot.com/"
                    "infra/tools/example/mac-${arch}@version:2@5.8-release"
                ),
                "src/data:object: gs://chromium-fixture/object",
            )
        )

        recovered = chromium_dependencies.restore_expected_cipd_versions(
            expected, plain
        )

        self.assertEqual(
            "version:2@5.8-release",
            recovered["src/tool:infra/tools/example/mac-${arch}"]["revision"],
        )

    def test_missing_plain_cipd_pin_is_rejected(self):
        expected = {
            "src/tool:infra/tools/example/mac-arm64": {
                "url": (
                    "https://chrome-infra-packages.appspot.com/"
                    "infra/tools/example/mac-arm64"
                ),
                "revision": "version:2",
            }
        }
        with self.assertRaisesRegex(SystemExit, "CIPD dependencies are missing"):
            chromium_dependencies.restore_expected_cipd_versions(expected, "")

    def test_plain_cipd_version_drift_is_rejected(self):
        expected = {
            "src/tool:infra/tools/example/mac-arm64": {
                "url": (
                    "https://chrome-infra-packages.appspot.com/"
                    "infra/tools/example/mac-arm64"
                ),
                "revision": "git_revision:deadbeef",
            }
        }
        plain = (
            "src/tool:infra/tools/example/mac-arm64: "
            "https://chrome-infra-packages.appspot.com/"
            "infra/tools/example/mac-arm64@git_revision:attacker"
        )
        with self.assertRaisesRegex(SystemExit, "CIPD version mismatch"):
            chromium_dependencies.restore_expected_cipd_versions(expected, plain)

    def test_cipd_ensure_file_pins_verified_platform_and_full_version(self):
        expected = {
            "src/tool:infra/tools/example/mac-${arch}": {
                "url": (
                    "https://chrome-infra-packages.appspot.com/"
                    "infra/tools/example/mac-${arch}"
                ),
                "revision": "version:2@5.8-release",
            }
        }

        ensure_file = chromium_dependencies.cipd_ensure_file(expected)

        self.assertIn("$VerifiedPlatform mac-arm64", ensure_file)
        self.assertIn(
            "infra/tools/example/mac-${arch} version:2@5.8-release",
            ensure_file,
        )

    def test_actual_cipd_service_swap_is_rejected(self):
        actual = {
            "src/tool:infra/tools/example/mac-arm64": {
                "url": (
                    "https://attacker.test/p/infra/tools/example/mac-arm64/+/"
                    "immutable-instance"
                ),
                "revision": None,
            }
        }
        with self.assertRaisesRegex(SystemExit, "instance URL is malformed"):
            chromium_dependencies.actual_cipd_pins(actual)

    def test_build_tool_identity_rejects_version_or_binary_swap(self):
        build_provenance.verify_build_tool_identity(
            "GN", "expected", "a" * 64, "expected", "a" * 64
        )
        with self.assertRaisesRegex(SystemExit, "version mismatch"):
            build_provenance.verify_build_tool_identity(
                "GN", "forged", "a" * 64, "expected", "a" * 64
            )
        with self.assertRaisesRegex(SystemExit, "trusted pin"):
            build_provenance.verify_build_tool_identity(
                "GN", "expected", "b" * 64, "expected", "a" * 64
            )

    def test_build_profile_binding_rejects_cli_profile_drift(self):
        root = build_provenance.ROOT
        chromium = build_provenance.CHROMIUM_SRC
        dev_args = (root / "config/build/ahoi-dev.gn").resolve()
        dev_out = (chromium / "out/AhoiDev").resolve()
        dev_app = (dev_out / "AhoiBrowser.app").resolve()
        plist = {
            "AhoiBuildProfile": "dev",
            "AhoiGNArgsSHA256": build_provenance.sha256(dev_args),
        }
        build_provenance.verify_profile_binding(
            "dev", dev_app, dev_out, dev_args, plist
        )
        with self.assertRaisesRegex(SystemExit, "requires"):
            build_provenance.verify_profile_binding(
                "release", dev_app, dev_out, dev_args, plist
            )
        wrong_stamp = dict(plist, AhoiBuildProfile="release")
        with self.assertRaisesRegex(SystemExit, "does not match"):
            build_provenance.verify_profile_binding(
                "dev", dev_app, dev_out, dev_args, wrong_stamp
            )

    def test_xcode_binding_uses_the_same_m152_toolchain_for_all_profiles(self):
        toolchain = build_provenance.load_json(
            build_provenance.ROOT / "config/toolchain.json"
        )
        dev = build_provenance.expected_xcode_for_kind("dev", toolchain)
        release = build_provenance.expected_xcode_for_kind("release", toolchain)
        upstream = build_provenance.expected_xcode_for_kind("upstream", toolchain)
        self.assertEqual("compatible-development", dev["mode"])
        self.assertEqual("26.6", dev["version"])
        self.assertEqual("23F81a", dev["iOSSDKBuild"])
        self.assertEqual("pinned-reference", release["mode"])
        self.assertEqual("26.6", release["version"])
        self.assertEqual("23F81a", release["iOSSDKBuild"])
        self.assertEqual(release, upstream)
        with self.assertRaisesRegex(SystemExit, "unsupported build provenance kind"):
            build_provenance.expected_xcode_for_kind("forged", toolchain)


if __name__ == "__main__":
    unittest.main()
