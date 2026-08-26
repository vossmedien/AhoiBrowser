import json
import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_json(relative_path: str):
    with (ROOT / relative_path).open(encoding="utf-8") as handle:
        return json.load(handle)


class RepositoryBuildContractTests(unittest.TestCase):
    def test_build_provenance_is_external_work_root_safe_and_fail_closed(self):
        provenance = (ROOT / "tools/build_provenance.py").read_text(
            encoding="utf-8"
        )
        dependency_check = (ROOT / "tools/chromium_dependencies.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('os.environ.get("AHOI_WORK_ROOT")', provenance)
        self.assertIn('choices=("upstream", "dev", "release")', provenance)
        self.assertIn("collect_revisions", provenance)
        self.assertIn("verify_deps_pins", provenance)
        self.assertIn("logical_path", provenance)
        self.assertNotIn("optional_output", provenance)
        self.assertIn("verify_build_tool_identity", provenance)
        self.assertIn("verify_profile_binding", provenance)
        self.assertIn('"version": clang_version_line', provenance)
        self.assertNotIn('"version": clang_version,', provenance)
        for key in (
            "gnBinarySha256",
            "ninjaBinarySha256",
            "clangArchiveSha256",
            "clangBinarySha256",
            "lldBinarySha256",
        ):
            self.assertRegex(
                load_json("config/toolchain.json")["buildTools"][key],
                r"^[0-9a-f]{64}$",
            )
        self.assertIn('"--actual"', dependency_check)
        self.assertIn('"--output-json"', dependency_check)

    def test_upstream_control_uses_full_build_provenance(self):
        builder = (ROOT / "scripts/build-upstream.sh").read_text(encoding="utf-8")
        self.assertIn("build_provenance.py", builder)
        self.assertIn("--kind upstream", builder)
        self.assertGreaterEqual(builder.count('"${SCRIPT_DIR}/verify-upstream.sh"'), 2)
        self.assertIn("unexpectedly contains an x86_64 slice", builder)

    def test_fetch_and_build_rerun_hooks_under_the_pinned_toolchain(self):
        fetch = (ROOT / "scripts/fetch-chromium.sh").read_text(encoding="utf-8")
        hooks = (ROOT / "scripts/run-chromium-hooks.sh").read_text(
            encoding="utf-8"
        )
        upstream = (ROOT / "scripts/build-upstream.sh").read_text(
            encoding="utf-8"
        )
        ahoi = (ROOT / "scripts/build-ahoi.sh").read_text(encoding="utf-8")
        self.assertIn("--nohooks", fetch)
        self.assertIn("ahoi_require_gclient_config", fetch)
        self.assertIn("config/gclient.py", fetch)
        self.assertIn('"${SCRIPT_DIR}/check-host.sh"', hooks)
        self.assertIn("gclient runhooks", hooks)
        self.assertIn('"${SCRIPT_DIR}/run-chromium-hooks.sh"', upstream)
        self.assertIn('hook_args=(--allow-source-overlay)', ahoi)
        self.assertIn("--compatible-dev-xcode", ahoi)
        self.assertLess(
            upstream.index('"${SCRIPT_DIR}/run-chromium-hooks.sh"'),
            upstream.index("gn gen"),
        )
        self.assertLess(
            ahoi.index('"${SCRIPT_DIR}/run-chromium-hooks.sh"'),
            ahoi.index("gn gen"),
        )
        self.assertGreaterEqual(ahoi.count("ahoi_require_overlay_state"), 2)

    def test_forged_hook_state_cannot_bypass_a_build_hook_run(self):
        for relative in ("scripts/build-upstream.sh", "scripts/build-ahoi.sh"):
            builder = (ROOT / relative).read_text(encoding="utf-8")
            with self.subTest(builder=relative):
                self.assertNotIn("ahoi_require_hook_state", builder)
                self.assertEqual(
                    1,
                    builder.count('"${SCRIPT_DIR}/run-chromium-hooks.sh"'),
                )

    def test_hook_state_is_invalidated_and_atomically_published(self):
        fetch = (ROOT / "scripts/fetch-chromium.sh").read_text(encoding="utf-8")
        hooks = (ROOT / "scripts/run-chromium-hooks.sh").read_text(
            encoding="utf-8"
        )
        common = (ROOT / "scripts/lib/common.sh").read_text(encoding="utf-8")
        self.assertLess(
            fetch.index("ahoi_invalidate_hook_state"), fetch.index("gclient sync")
        )
        self.assertLess(
            hooks.index("ahoi_invalidate_hook_state"), hooks.index("gclient runhooks")
        )
        self.assertLess(hooks.index("gclient runhooks"), hooks.index("os.replace"))
        self.assertIn('rm -f -- "$(ahoi_hook_state_file)"', common)
        self.assertIn("tempfile.mkstemp", hooks)
        self.assertEqual(2, hooks.count("os.replace("))
        self.assertNotIn("path.write_text", hooks)
        self.assertIn('"schemaVersion": 3', hooks)
        self.assertIn('"toolchainMode": toolchain_mode', hooks)

    def test_xcode_26_6_is_dev_only_and_release_stays_on_reference_pin(self):
        toolchain = load_json("config/toolchain.json")
        compatible = toolchain["xcode"]["compatibleDevelopment"]
        self.assertEqual("26.5", toolchain["xcode"]["requiredVersion"])
        self.assertEqual("17F42", toolchain["xcode"]["requiredBuild"])
        self.assertEqual("26.6", compatible["version"])
        self.assertEqual("17F113", compatible["build"])
        self.assertIn("ahoi-dev only", compatible["scope"])
        ios_sdk = toolchain["sdks"]["iOS"]
        self.assertEqual("26.5", ios_sdk["testedVersion"])
        self.assertEqual("23F73", ios_sdk["pinnedReferenceBuild"])
        self.assertEqual("23F81a", ios_sdk["compatibleDevelopmentBuild"])
        self.assertNotIn("testedBuild", ios_sdk)
        builder = (ROOT / "scripts/build-ahoi.sh").read_text(encoding="utf-8")
        self.assertIn("dev)", builder)
        self.assertIn('toolchain_mode="compatible-development"', builder)
        self.assertIn("release)", builder)
        self.assertIn('toolchain_mode="pinned-reference"', builder)
        provenance = (ROOT / "tools/build_provenance.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("expected_xcode_for_kind", provenance)
        host_check = (ROOT / "scripts/check-host.sh").read_text(encoding="utf-8")
        common = (ROOT / "scripts/lib/common.sh").read_text(encoding="utf-8")
        self.assertIn('ahoi_expected_ios_sdk_build "${xcode_mode}"', host_check)
        self.assertIn("ahoi_expected_ios_sdk_build", common)
        self.assertIn('"${expected_ios_sdk_build}"', common)
        self.assertIn('expected_xcode["iOSSDKBuild"]', provenance)

        helper_script = ROOT / "scripts/lib/common.sh"
        for mode, expected_build in (
            ("pinned-reference", "23F73"),
            ("compatible-development", "23F81a"),
        ):
            with self.subTest(toolchain_mode=mode):
                completed = subprocess.run(
                    [
                        "bash",
                        "-c",
                        'source "$1"; ahoi_expected_ios_sdk_build "$2"',
                        "ahoi-sdk-test",
                        str(helper_script),
                        mode,
                    ],
                    cwd=ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(expected_build, completed.stdout.strip())
        forged = subprocess.run(
            [
                "bash",
                "-c",
                'source "$1"; ahoi_expected_ios_sdk_build forged',
                "ahoi-sdk-test",
                str(helper_script),
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(0, forged.returncode)
        self.assertIn("unsupported Xcode toolchain mode", forged.stderr)

    def test_gclient_config_is_canonical_and_minimal(self):
        config = (ROOT / "config/gclient.py").read_text(encoding="utf-8")
        self.assertEqual(1, config.count('"name"        : \'src\''))
        self.assertIn("https://chromium.googlesource.com/chromium/src.git", config)
        self.assertIn('"custom_vars": {}', config)
        self.assertNotIn("target_os", config)
        common = (ROOT / "scripts/lib/common.sh").read_text(encoding="utf-8")
        provenance = (ROOT / "tools/build_provenance.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("ahoi_require_gclient_config", common)
        self.assertIn("canonical config", provenance)

    def test_no_obvious_committed_secret_files(self):
        forbidden_suffixes = {
            ".p12",
            ".pfx",
            ".p8",
            ".mobileprovision",
            ".provisionprofile",
            ".key",
            ".pem",
        }
        candidates = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        offenders = [
            relative
            for relative in candidates
            if pathlib.Path(relative).suffix in forbidden_suffixes
        ]
        self.assertEqual([], offenders)

    def test_github_actions_are_immutable(self):
        workflows = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / ".github/workflows").glob("*.yml"))
        )
        action_references = re.findall(r"uses:\s*[^@\s]+@([^\s#]+)", workflows)
        self.assertTrue(action_references)
        for reference in action_references:
            with self.subTest(reference=reference):
                self.assertRegex(reference, r"^[0-9a-f]{40}$")

    def test_github_enforces_lint_secrets_and_dco(self):
        workflow = (
            ROOT / ".github/workflows/repository-contract.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("action-shellcheck@", workflow)
        self.assertIn("actionlint/actionlint", workflow)
        self.assertIn("gitleaks/gitleaks-action@", workflow)
        self.assertIn("tools/check_dco.py", workflow)
        self.assertIn("fetch-depth: 0", workflow)

    def test_directly_invoked_scripts_are_executable(self):
        for path in sorted((ROOT / "scripts").glob("*.sh")):
            with self.subTest(path=path.name):
                self.assertTrue(path.stat().st_mode & 0o111, path)
        for relative in (
            "tools/evidence.py",
            "tools/build_provenance.py",
            "tools/chromium_dependencies.py",
            "tools/compose_overlay.py",
            "tools/verify_chromium_pin.py",
            "tools/check_dco.py",
            "tools/overlay_fingerprint.py",
            "tools/overlay_state.py",
            "tools/verify_macos_entitlements.py",
        ):
            path = ROOT / relative
            with self.subTest(path=relative):
                self.assertTrue(path.stat().st_mode & 0o111, path)


if __name__ == "__main__":
    unittest.main()
