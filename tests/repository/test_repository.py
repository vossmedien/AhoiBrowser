import json
import os
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_json(relative_path: str):
    with (ROOT / relative_path).open(encoding="utf-8") as handle:
        return json.load(handle)


class RepositoryContractTests(unittest.TestCase):
    def test_registry_covers_every_master_test_id(self):
        master = (ROOT / "outputs/AhoiBrowser-Master-Zielprompt.md").read_text(encoding="utf-8")
        expected_rows = re.findall(
            r"^- `([A-Z][A-Z0-9]{1,9}-\d{2,3})`: (.+)$",
            master,
            re.MULTILINE,
        )
        expected = dict(expected_rows)
        registry = load_json("config/test-registry.json")["tests"]
        actual = {entry["id"]: entry for entry in registry}
        self.assertEqual(333, len(registry))
        self.assertEqual(len(registry), len(actual), "test IDs must be unique")
        self.assertEqual(set(expected), set(actual))
        for entry in registry:
            with self.subTest(test_id=entry["id"]):
                self.assertEqual(expected[entry["id"]], entry["description"])
                self.assertEqual(entry["id"].split("-", 1)[0], entry["suite"])
                self.assertEqual("NOT_RUN", entry["status"])
                self.assertTrue(entry["releaseCritical"])
                self.assertIn(entry["primaryClass"], {"UNIT", "INTEGRATION", "CU_E2E", "ASSISTED_E2E"})
                self.assertEqual(
                    len(entry["requiredEvidenceClasses"]),
                    len(set(entry["requiredEvidenceClasses"])),
                    "required evidence classes must be unique",
                )
                self.assertIn("UNIT", entry["requiredEvidenceClasses"])
                self.assertIn("INTEGRATION", entry["requiredEvidenceClasses"])
                self.assertIn(entry["primaryClass"], entry["requiredEvidenceClasses"])

    def test_required_documents_exist(self):
        required = {
            "README.md",
            "LICENSE",
            "CONTRIBUTING.md",
            "SECURITY.md",
            "docs/PRODUCT_PRINCIPLES.md",
            "docs/ARCHITECTURE.md",
            "docs/UPSTREAM.md",
            "docs/BUILDING.md",
            "docs/SECURITY.md",
            "docs/THREAT_MODEL.md",
            "docs/PRIVACY.md",
            "docs/NETWORK.md",
            "docs/EXTENSION_COMPATIBILITY.md",
            "docs/SYNC.md",
            "docs/HTTP_AUTH.md",
            "docs/spikes/HTTP_AUTH_FIXTURE.md",
            "docs/spikes/CLOUDKIT.md",
            "docs/UI_SYSTEM.md",
            "docs/SPLIT_VIEW.md",
            "docs/TESTING.md",
            "docs/RELEASING.md",
            "docs/LEGAL.md",
            "docs/PHASE0_FEASIBILITY.md",
            "schemas/e2e-result.schema.json",
            "config/version.json",
            "config/split-view.json",
            "config/gclient.py",
            "tools/chromium_dependencies.py",
            "tools/compose_overlay.py",
            "tools/verify_chromium_pin.py",
            "tools/check_dco.py",
            "tools/overlay_fingerprint.py",
            "tools/overlay_state.py",
            "tools/verify_macos_entitlements.py",
            "scripts/run-chromium-hooks.sh",
            "outputs/AhoiBrowser-Master-Zielprompt.md",
            "fixtures/http-auth/README.md",
            "fixtures/http-auth/fixture_server.py",
            "fixtures/http-auth/manage.py",
            "fixtures/http-auth/tests/test_http_auth_fixture.py",
            "spikes/cloudkit/Package.swift",
            "spikes/cloudkit/Sources/AhoiCloudKitSpike/AppleCloudKitAdapter.swift",
            "spikes/cloudkit/Sources/AhoiCloudKitSpike/SyncBoundary.swift",
        }
        missing = sorted(path for path in required if not (ROOT / path).is_file())
        self.assertEqual([], missing)

    def test_chromium_pin_is_concrete(self):
        pin = load_json("config/chromium.json")
        self.assertRegex(pin["version"], r"^\d+\.0\.\d+\.\d+$")
        self.assertRegex(pin["commit"], r"^[0-9a-f]{40}$")
        self.assertEqual(int(pin["version"].split(".")[0]), pin["milestone"])
        self.assertEqual(int(pin["version"].split(".")[2]), pin["branchHead"])
        self.assertEqual("Stable", pin["channel"])
        self.assertEqual("Mac", pin["platform"])
        self.assertTrue(pin["pinnable"])
        self.assertEqual(1.0, pin["rolloutFraction"])
        self.assertRegex(pin["branchPoint"], r"^[0-9a-f]{40}$")

    def test_product_version_has_apple_and_channel_components(self):
        version = load_json("config/version.json")
        self.assertRegex(version["marketingVersion"], r"^\d+(?:\.\d+){2}$")
        self.assertRegex(version["buildNumber"], r"^[1-9]\d*$")
        self.assertIn(version["channel"], {"nightly", "beta", "stable"})
        self.assertEqual(
            (ROOT / "VERSION").read_text(encoding="utf-8").strip(),
            version["displayVersion"],
        )

    def test_depot_tools_pin_is_concrete(self):
        pin = load_json("config/depot-tools.json")
        self.assertRegex(pin["commit"], r"^[0-9a-f]{40}$")
        self.assertTrue(pin["source"].startswith("https://chromium.googlesource.com/"))

    def test_depot_tools_bootstrap_is_explicit_absolute_and_reverified(self):
        bootstrap = (ROOT / "scripts/bootstrap-depot-tools.sh").read_text(encoding="utf-8")
        common = (ROOT / "scripts/lib/common.sh").read_text(encoding="utf-8")

        export_call = "ahoi_export_depot_tools_environment"
        ensure_call = '"${AHOI_DEPOT_TOOLS_DIR}/ensure_bootstrap"'
        enable_call = "ahoi_enable_depot_tools"
        self.assertLess(bootstrap.index(export_call), bootstrap.index(ensure_call))
        self.assertLess(bootstrap.index(ensure_call), bootstrap.index(enable_call))
        self.assertLess(bootstrap.index(enable_call), bootstrap.index("depot_tools ready"))
        self.assertIn('export DEPOT_TOOLS_DIR="${AHOI_DEPOT_TOOLS_DIR}"', common)
        self.assertIn('export DEPOT_TOOLS_UPDATE=0', common)
        self.assertIn("ahoi_require_depot_tools_python", common)

    def test_depot_tools_python_pointer_validation_fails_closed(self):
        def validate(work_root: pathlib.Path) -> subprocess.CompletedProcess[str]:
            environment = os.environ.copy()
            environment["AHOI_WORK_ROOT"] = str(work_root)
            return subprocess.run(
                [
                    "/bin/bash",
                    "-c",
                    'source "$1/scripts/lib/common.sh"; ahoi_require_depot_tools_python',
                    "ahoi-depot-tools-test",
                    str(ROOT),
                ],
                cwd=ROOT,
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            work_root = pathlib.Path(temporary_directory).resolve()
            depot_tools = work_root / "depot_tools"
            python_directory = depot_tools / "bootstrap-python" / "python3" / "bin"
            python_directory.mkdir(parents=True)
            python_binary = python_directory / "python3"
            python_binary.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            python_binary.chmod(0o755)
            pointer = depot_tools / "python3_bin_reldir.txt"

            missing = validate(work_root)
            self.assertNotEqual(0, missing.returncode)
            self.assertIn("missing regular bootstrap pointer", missing.stderr)

            pointer.write_text("bootstrap-python/python3/bin", encoding="utf-8")
            valid = validate(work_root)
            self.assertEqual(0, valid.returncode, valid.stderr)

            python_binary.chmod(0o644)
            non_executable = validate(work_root)
            self.assertNotEqual(0, non_executable.returncode)
            self.assertIn("bootstrap Python is not executable", non_executable.stderr)
            python_binary.chmod(0o755)

            pointer.write_text(str(python_directory), encoding="utf-8")
            absolute = validate(work_root)
            self.assertNotEqual(0, absolute.returncode)
            self.assertIn("bootstrap pointer must be relative", absolute.stderr)

            outside_python = work_root / "outside" / "python3"
            outside_python.parent.mkdir()
            outside_python.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            outside_python.chmod(0o755)
            pointer.write_text("../outside", encoding="utf-8")
            traversal = validate(work_root)
            self.assertNotEqual(0, traversal.returncode)
            self.assertIn("bootstrap Python must resolve inside depot_tools", traversal.stderr)

            pointer.write_text("missing/python3/bin", encoding="utf-8")
            missing_binary = validate(work_root)
            self.assertNotEqual(0, missing_binary.returncode)
            self.assertIn("bootstrap Python must resolve inside depot_tools", missing_binary.stderr)

    def test_foundation_python_version_is_explicit(self):
        toolchain = load_json("config/toolchain.json")
        self.assertRegex(toolchain["python"]["minimumVersion"], r"^\d+\.\d+$")
        host_check = (ROOT / "scripts/check-host.sh").read_text(encoding="utf-8")
        self.assertIn("python.minimumVersion", host_check)
        self.assertIn('df -P "${work_parent}"', host_check)
        self.assertIn('diskutil info "${work_device}"', host_check)

    def test_evidence_schema_uses_strict_statuses_and_installed_bundle(self):
        schema = load_json("schemas/e2e-result.schema.json")
        statuses = load_json("config/test-statuses.json")["allowed"]
        self.assertEqual(statuses, schema["properties"]["status"]["enum"])
        serialized = json.dumps(schema)
        self.assertEqual(2, schema["properties"]["schemaVersion"]["const"])
        self.assertIn("/Applications/AhoiBrowser.app", serialized)
        self.assertIn("notarizationVerified", serialized)
        self.assertIn("hardenedRuntimeVerified", serialized)
        self.assertIn("executedBy", schema["required"])
        self.assertIn("requirement", schema["required"])
        self.assertIn("source", schema["required"])
        self.assertIn("BLOCKED_ENTITLEMENT", serialized)
        validator = (ROOT / "tools/evidence.py").read_text(encoding="utf-8")
        self.assertIn("testClass does not match registry", validator)
        self.assertIn('"xcrun", "stapler", "validate"', validator)
        release_gate = load_json("config/release-evidence.json")
        self.assertEqual(1, release_gate["schemaVersion"])
        self.assertIs(False, release_gate["releasePassEnabled"])
        self.assertEqual(
            {
                "build-provenance",
                "signed-package-provenance",
                "notarization-receipt",
                "installed-bundle-binding",
            },
            set(release_gate["requiredChain"]),
        )
        self.assertIn("release_evidence_chain_ready", validator)

    def test_sync_never_contains_secrets(self):
        policy = load_json("config/sync-policy.json")
        required_denials = {
            "cookies",
            "passwords",
            "autofill",
            "siteData",
            "cache",
            "permissions",
            "extensionStorage",
            "incognito",
            "splitTopology",
            "keychainSecrets",
            "secretHeaders",
            "httpAuthSecrets",
        }
        self.assertTrue(required_denials.issubset(policy["neverSync"]))
        self.assertTrue(set(policy["sync"]).isdisjoint(policy["neverSync"]))

    def test_theme_and_shortcut_contract(self):
        theme = load_json("config/theme.json")
        self.assertEqual(["system", "light", "dark"], theme["appearances"])
        self.assertTrue(theme["workspaceAccentOverride"])
        self.assertTrue(theme["glass"]["reducedTransparencyFallback"])
        self.assertTrue(theme["glass"]["neverRequiredForLayout"])
        self.assertGreaterEqual(theme["accessibility"]["minimumTextContrast"], 4.5)
        shortcuts = load_json("config/shortcuts.json")
        self.assertEqual("Command+L", shortcuts["commands"]["focusCommandBar"])
        self.assertEqual("Command+T", shortcuts["commands"]["newTemporaryTab"])
        self.assertEqual("Option+Space", shortcuts["commands"]["quickWindow"])
        self.assertEqual("g ", shortcuts["commandPrefixes"]["googleSearch"])

    def test_split_view_contract_is_bounded_native_and_release_critical(self):
        contract = load_json("config/split-view.json")
        self.assertEqual(1, contract["schemaVersion"])
        self.assertEqual(2, contract["minimumPanes"])
        self.assertEqual(4, contract["maximumPanes"])
        self.assertEqual(
            {
                "two-columns",
                "two-rows",
                "three-columns",
                "three-rows",
                "four-grid",
                "main-left",
                "main-right",
                "main-top",
                "main-bottom",
            },
            set(contract["layouts"]),
        )
        self.assertTrue(contract["dragAndDrop"]["pageRowBeforeAfterReorders"])
        self.assertTrue(contract["dragAndDrop"]["folderCenterNests"])
        self.assertTrue(contract["dragAndDrop"]["pageRowCenterCreatesSplit"])
        self.assertTrue(contract["dragAndDrop"]["cancelIsAtomic"])
        self.assertEqual(
            "add-as-four-grid-with-visible-preview",
            contract["dragAndDrop"]["fourthPanePolicy"],
        )
        self.assertEqual(
            "reject-with-visible-explanation",
            contract["dragAndDrop"]["fifthPanePolicy"],
        )
        self.assertTrue(contract["dragAndDrop"]["sidebarMirrorsContentLayout"])
        self.assertFalse(contract["dragAndDrop"]["normalIncognitoMixing"])
        self.assertTrue(contract["persistence"]["normalWindowWorkspaceSession"])
        self.assertFalse(contract["persistence"]["incognito"])
        self.assertFalse(contract["persistence"]["cloudSync"])
        self.assertTrue(contract["security"]["oneActivePane"])
        self.assertTrue(contract["security"]["activePaneIndicatorRequired"])
        self.assertTrue(contract["security"]["originIndicatorForEveryPane"])
        self.assertTrue(contract["security"]["siteIsolationPreserved"])

        spec = (ROOT / "docs/SPLIT_VIEW.md").read_text(encoding="utf-8")
        architecture = (ROOT / "docs/ARCHITECTURE.md").read_text(encoding="utf-8")
        ui_system = (ROOT / "docs/UI_SYSTEM.md").read_text(encoding="utf-8")
        for required in (
            "TabStripModel",
            "SplitTabCollection",
            "MultiContentsView",
            "ContentsContainerView",
            "SessionService",
            "exactly two, three, or four",
            "OffTheRecordProfile",
            "DevTools",
            "Picture in Picture",
        ):
            self.assertIn(required, spec)
        self.assertIn("SplitViewService", architecture)
        self.assertIn("parallel Ahoi-specific pane controller is prohibited", architecture)
        self.assertIn("page creates a real split", ui_system)
        self.assertIn("fifth pane is rejected visibly", ui_system)

        split_tests = [
            entry
            for entry in load_json("config/test-registry.json")["tests"]
            if entry["suite"] == "SPLIT"
        ]
        self.assertEqual(34, len(split_tests))
        self.assertEqual(
            {"SPLIT-01", "SPLIT-02", "SPLIT-03", "SPLIT-04"},
            {
                entry["id"]
                for entry in split_tests
                if entry["primaryClass"] == "INTEGRATION"
            },
        )
        self.assertTrue(
            all(
                entry["primaryClass"] == "CU_E2E"
                for entry in split_tests[4:]
            )
        )

    def test_only_pass_is_release_success(self):
        statuses = load_json("config/test-statuses.json")
        self.assertEqual(["PASS"], statuses["releaseSuccess"])
        self.assertIn("NOT_RUN", statuses["allowed"])
        self.assertIn("BLOCKED_ENTITLEMENT", statuses["allowed"])

    def test_external_features_default_off(self):
        gates = load_json("config/feature-gates.json")["gates"]
        for name in ("proprietaryCodecs", "widevine", "selectiveUboClassicMv2", "crashUpload"):
            with self.subTest(name=name):
                self.assertFalse(gates[name]["default"])
        external = {entry["id"]: entry for entry in load_json("config/external-gates.json")["gates"]}
        self.assertEqual("blocked-entitlement", external["widevine-mla"]["state"])
        self.assertEqual("blocked-legal-review", external["proprietary-codecs"]["state"])
        self.assertTrue(
            {
                "chrome-web-store",
                "google-api-services",
                "safe-browsing-service",
                "onepassword-additional-browser",
            }.issubset(external)
        )

    def test_build_profiles_preserve_security(self):
        forbidden = ("--no-sandbox", "--ignore-certificate-errors", "site_isolation=false")
        for path in (ROOT / "config/build").glob("*.gn"):
            content = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertIn('target_cpu = "arm64"', content)
                self.assertIn('target_os = "mac"', content)
                self.assertFalse(any(item in content for item in forbidden))
        release = (ROOT / "config/build/ahoi-release.gn").read_text(encoding="utf-8")
        self.assertIn("is_component_build = false", release)
        self.assertIn("is_official_build = true", release)
        self.assertIn("proprietary_codecs = false", release)
        self.assertIn('branding_file_path = "//ahoi/branding/BRANDING"', release)
        self.assertIn('branding_path_component = "chromium"', release)
        branding = (ROOT / "overlay/chromium/src/ahoi/branding/BRANDING").read_text(encoding="utf-8")
        self.assertIn("PRODUCT_FULLNAME=AhoiBrowser", branding)
        self.assertIn("MAC_BUNDLE_ID=app.ahoibrowser.AhoiBrowser", branding)
        self.assertNotIn("Google Chrome", branding)

    def test_patch_series_references_existing_files(self):
        series = ROOT / "patches/chromium/series"
        for raw in series.read_text(encoding="utf-8").splitlines():
            entry = raw.strip()
            if not entry or entry.startswith("#"):
                continue
            self.assertTrue((series.parent / entry).is_file(), entry)

    def test_installed_app_verifier_targets_only_ahoibrowser(self):
        verifier = (ROOT / "scripts/verify-installed-app.sh").read_text(encoding="utf-8")
        self.assertIn('/Applications/AhoiBrowser.app', verifier)
        self.assertIn("codesign --verify --deep --strict", verifier)
        self.assertIn("spctl --assess", verifier)
        self.assertIn("stapler validate", verifier)
        self.assertIn("get-task-allow", verifier)
        self.assertIn("AHOI_TEAM_ID", verifier)
        self.assertIn("AHOI_CODESIGN_IDENTITY", verifier)
        self.assertIn("TeamIdentifier", verifier)
        self.assertIn("Hardened Runtime", verifier)
        self.assertIn('AhoiBuildProfile=release', verifier)
        self.assertIn("verify_macos_entitlements.py", verifier)
        self.assertNotIn("|| true", verifier)
        self.assertIn('find "${app_path}" -type f -print0', verifier)
        self.assertNotIn("-perm -111", verifier)
        self.assertIn('lipo -archs "${candidate}"', verifier)
        self.assertIn('architectures}" = "arm64"', verifier)

    def test_branded_build_never_accepts_chromium_as_product_output(self):
        builder = (ROOT / "scripts/build-ahoi.sh").read_text(encoding="utf-8")
        self.assertIn('app_path="${out_dir}/AhoiBrowser.app"', builder)
        self.assertNotIn('app_path="${out_dir}/Chromium.app"', builder)
        self.assertIn("branded AhoiBrowser.app was not produced", builder)
        self.assertIn("stamp-built-app.sh", builder)
        self.assertIn("build_provenance.py", builder)

    def test_component_development_bundle_is_staged_portably(self):
        builder = (ROOT / "scripts/build-ahoi.sh").read_text(encoding="utf-8")
        stager = (ROOT / "scripts/stage-component-runtime.sh").read_text(
            encoding="utf-8"
        )
        verifier = (ROOT / "scripts/verify-built-app.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("stage-component-runtime.sh", builder)
        self.assertIn("sign-development-app.sh", builder)
        self.assertIn("is_component_build = true", stager)
        self.assertIn("libc++_chrome.dylib", stager)
        self.assertIn("ahoi-component-runtime.sha256", stager)
        self.assertIn("libc++_chrome.dylib", verifier)
        self.assertIn("shasum -a 256 -s -c", verifier)
        signer = (ROOT / "scripts/sign-development-app.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("AhoiBuildProfile", signer)
        self.assertIn("forbidden for release bundles", signer)
        self.assertIn("codesign --verify --deep --strict", signer)

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
            self.assertRegex(load_json("config/toolchain.json")["buildTools"][key], r"^[0-9a-f]{64}$")
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
        self.assertIn('--compatible-dev-xcode', ahoi)
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
        self.assertIn('dev)', builder)
        self.assertIn('toolchain_mode="compatible-development"', builder)
        self.assertIn('release)', builder)
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
        provenance = (ROOT / "tools/build_provenance.py").read_text(encoding="utf-8")
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

    def test_http_auth_fixture_is_loopback_only_and_honest_about_scope(self):
        server = (ROOT / "fixtures/http-auth/fixture_server.py").read_text(
            encoding="utf-8"
        )
        fixture_docs = (ROOT / "docs/spikes/HTTP_AUTH_FIXTURE.md").read_text(
            encoding="utf-8"
        )
        self.assertIn('LOOPBACK_HOST = "127.0.0.1"', server)
        self.assertIn('"authorization": "[REDACTED]"', server)
        self.assertIn("The fixture closes no release gate on its own", fixture_docs)
        self.assertIn("AUTH-27", fixture_docs)

    def test_cloudkit_spike_uses_native_encrypted_values_and_denies_secrets(self):
        adapter = (
            ROOT
            / "spikes/cloudkit/Sources/AhoiCloudKitSpike/AppleCloudKitAdapter.swift"
        ).read_text(encoding="utf-8")
        boundary = (
            ROOT / "spikes/cloudkit/Sources/AhoiCloudKitSpike/SyncBoundary.swift"
        ).read_text(encoding="utf-8")
        spike_docs = (ROOT / "docs/spikes/CLOUDKIT.md").read_text(encoding="utf-8")
        self.assertIn("encryptedValues", adapter)
        for denied_case in (
            ".cookie",
            ".password",
            ".incognito",
            ".keychainSecret",
            ".headerSecret",
            ".httpAuthSecret",
        ):
            self.assertIn(denied_case, boundary)
        self.assertIn("Kein behaupteter CloudKit-Roundtrip", spike_docs)

    def test_extension_phase_zero_controls_are_narrow_and_honest(self):
        mv3 = load_json("fixtures/extensions/mv3-smoke/manifest.json")
        denied_mv2 = load_json(
            "fixtures/extensions/mv2-denied-control/manifest.json"
        )
        self.assertEqual(3, mv3["manifest_version"])
        self.assertEqual({"storage", "tabs"}, set(mv3["permissions"]))
        self.assertNotIn("host_permissions", mv3)
        self.assertEqual(2, denied_mv2["manifest_version"])
        denied_docs = (
            ROOT / "fixtures/extensions/mv2-denied-control/README.md"
        ).read_text(encoding="utf-8")
        self.assertIn("must reject", denied_docs)


if __name__ == "__main__":
    unittest.main()
