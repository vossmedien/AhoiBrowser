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
        self.assertEqual(335, len(registry))
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

    def test_physical_or_user_authenticated_journeys_require_assistance(self):
        registry = {
            entry["id"]: entry
            for entry in load_json("config/test-registry.json")["tests"]
        }
        assisted = {
            "WS-03",
            "NAV-09",
            "MEDIA-07",
            "AUTH-24",
            "DEV-23",
            "EXT-06",
            "EXT-07",
            "SPLIT-26",
            "PERM-01",
            "PERM-02",
            "PERM-03",
        }
        for test_id in assisted:
            with self.subTest(test_id=test_id):
                self.assertEqual("ASSISTED_E2E", registry[test_id]["primaryClass"])
                self.assertIn(
                    "ASSISTED_E2E", registry[test_id]["requiredEvidenceClasses"]
                )
                self.assertNotIn("CU_E2E", registry[test_id]["requiredEvidenceClasses"])

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
            "tools/chromium_roll.py",
            "tools/chromium_roll_discovery.py",
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
        self.assertEqual(
            {"SPLIT-26"},
            {
                entry["id"]
                for entry in split_tests[4:]
                if entry["primaryClass"] == "ASSISTED_E2E"
            },
        )
        self.assertTrue(
            all(
                entry["primaryClass"] in {"CU_E2E", "ASSISTED_E2E"}
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
        self.assertLess(
            builder.index("stage-component-runtime.sh"),
            builder.index("stamp-built-app.sh"),
        )
        self.assertLess(
            builder.index("stamp-built-app.sh"),
            builder.index("sign-development-app.sh"),
        )
        self.assertLess(
            builder.index("sign-development-app.sh"),
            builder.index("verify-built-app.sh"),
        )
        self.assertIn("is_component_build = true", stager)
        self.assertIn("AhoiBrowser Framework.framework", stager)
        self.assertIn("ditto", stager)
        self.assertIn(".ahoi-framework-stage.", stager)
        self.assertIn("previous.framework", stager)
        self.assertIn("staged component framework file differs", stager)
        self.assertIn("staged component framework symlink differs", stager)
        self.assertIn("libc++_chrome.dylib", stager)
        self.assertIn("ahoi-component-runtime.sha256", stager)
        self.assertIn("ahoi-component-framework-resources.sha256", stager)
        self.assertIn("libc++_chrome.dylib", verifier)
        self.assertIn("shasum -a 256 -s -c", verifier)
        self.assertIn("ahoi-component-framework-resources.sha256", verifier)
        signer = (ROOT / "scripts/sign-development-app.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("AhoiBuildProfile", signer)
        self.assertIn("forbidden for release bundles", signer)
        self.assertIn("tools/development_signing.py", signer)
        self.assertIn("--preserve-metadata=entitlements", signer)
        self.assertIn("designated => cdhash", signer)
        self.assertNotIn("Developer ID Application", signer)
        self.assertIn("codesign --verify --deep --strict", signer)

    def test_component_stager_replaces_stale_framework_atomically(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            work_root = pathlib.Path(temporary_directory) / "work"
            chromium_src = work_root / "chromium" / "src"
            out_dir = chromium_src / "out" / "AhoiDev"
            app_path = out_dir / "AhoiBrowser.app"
            runtime_dir = app_path / "Contents" / "Frameworks"
            app_resources = app_path / "Contents" / "Resources"
            source_framework = out_dir / "AhoiBrowser Framework.framework"
            destination_framework = runtime_dir / "AhoiBrowser Framework.framework"
            source_version = source_framework / "Versions" / "1.0"

            (chromium_src / "buildtools" / "mac").mkdir(parents=True)
            fake_gn = chromium_src / "buildtools" / "mac" / "gn"
            fake_gn.write_text(
                "#!/bin/sh\nprintf 'is_component_build = true\\n'\n",
                encoding="utf-8",
            )
            fake_gn.chmod(0o755)

            (source_version / "Resources" / "en.lproj").mkdir(parents=True)
            (source_version / "Helpers").mkdir()
            framework_binary = source_version / "AhoiBrowser Framework"
            framework_binary.write_bytes(b"current-framework-binary")
            framework_binary.chmod(0o755)
            source_locale = source_version / "Resources" / "en.lproj" / "locale.pak"
            source_locale.write_bytes(b"current-locale-pack")
            (source_framework / "Versions" / "Current").symlink_to("1.0")
            (source_framework / "Resources").symlink_to(
                "Versions/Current/Resources"
            )
            (source_framework / "Helpers").symlink_to("Versions/Current/Helpers")
            (source_framework / "AhoiBrowser Framework").symlink_to(
                "Versions/Current/AhoiBrowser Framework"
            )

            destination_framework.mkdir(parents=True)
            (destination_framework / "stale-resource.txt").write_text(
                "must disappear", encoding="utf-8"
            )
            app_resources.mkdir(parents=True)
            (out_dir / "libc++_chrome.dylib").write_bytes(b"runtime")

            environment = os.environ.copy()
            environment["AHOI_WORK_ROOT"] = str(work_root)
            subprocess.run(
                [
                    str(ROOT / "scripts" / "stage-component-runtime.sh"),
                    str(out_dir),
                    str(app_path),
                ],
                cwd=ROOT,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
            )

            destination_locale = (
                destination_framework
                / "Versions"
                / "1.0"
                / "Resources"
                / "en.lproj"
                / "locale.pak"
            )
            self.assertEqual(source_locale.read_bytes(), destination_locale.read_bytes())
            self.assertFalse((destination_framework / "stale-resource.txt").exists())
            self.assertEqual(
                "1.0", os.readlink(destination_framework / "Versions" / "Current")
            )
            framework_manifest = (
                app_resources / "ahoi-component-framework-resources.sha256"
            )
            self.assertIn("locale.pak", framework_manifest.read_text(encoding="utf-8"))
            subprocess.run(
                ["shasum", "-a", "256", "-s", "-c", str(framework_manifest)],
                cwd=destination_framework,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                b"runtime", (runtime_dir / "libc++_chrome.dylib").read_bytes()
            )

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
