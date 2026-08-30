import os
import pathlib
import plistlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MOBILE = ROOT / "apps" / "AhoiMobile"
PREFLIGHT = MOBILE / "scripts" / "release-preflight.sh"
COMMIT = "a" * 40

PUBLIC = {
    "AHOI_APPLE_TEAM_ID": "248AJ5BN47",
    "AHOI_MOBILE_BUNDLE_ID": "app.ahoibrowser.AhoiBrowser",
    "AHOI_MOBILE_CORE_BUNDLE_ID": "app.ahoibrowser.AhoiBrowser.core",
    "AHOI_MOBILE_TEST_BUNDLE_ID": "app.ahoibrowser.AhoiBrowser.tests",
    "AHOI_MOBILE_UI_TEST_BUNDLE_ID": "app.ahoibrowser.AhoiBrowser.uitests",
    "AHOI_CLOUDKIT_CONTAINER_ID": "iCloud.app.ahoibrowser.AhoiBrowser",
    "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP": "248AJ5BN47.app.ahoibrowser.sync",
    "AHOI_SYNC_KEYCHAIN_SERVICE": "app.ahoibrowser.sync.payload-key",
    "AHOI_SYNC_KEYCHAIN_ACCOUNT": "payload-key",
    "AHOI_SYNC_KEY_VERSION": "1",
    "AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP": "248AJ5BN47.app.ahoibrowser.commands",
    "AHOI_COMMAND_KEYCHAIN_SERVICE": "app.ahoibrowser.remote-command.signing-key",
    "AHOI_COMMAND_KEYCHAIN_ACCOUNT": "device-signing-key",
}

MODE_CONTRACT = {
    "DebugLocal": ("", "", ""),
    "CloudKitDevelopment": (
        "AhoiMobile.entitlements.template",
        "development",
        "Development",
    ),
    "TestFlightBootstrap": (
        "AhoiMobile.entitlements.template",
        "production",
        "Production",
    ),
    "DefaultBrowserDevelopment": (
        "AhoiMobile.DefaultBrowser.entitlements.template",
        "development",
        "Development",
    ),
    "ReleasePostGrant": (
        "AhoiMobile.DefaultBrowser.entitlements.template",
        "production",
        "Production",
    ),
}


def mode_environment(mode: str) -> dict[str, str]:
    entitlements, aps, cloud = MODE_CONTRACT[mode]
    values = {
        **os.environ,
        **PUBLIC,
        "SRCROOT": str(MOBILE),
        "CONFIGURATION": mode,
        "AHOI_BUILD_MODE": mode,
        "AHOI_SOURCE_COMMIT": COMMIT
        if mode in {"TestFlightBootstrap", "ReleasePostGrant"}
        else "NOT_AVAILABLE",
        "AHOI_MOBILE_MARKETING_VERSION": "1.0",
        "AHOI_MOBILE_BUILD_NUMBER": "42",
        "AHOI_APP_USES_NON_EXEMPT_ENCRYPTION": "NO",
        "AHOI_APS_ENVIRONMENT": aps,
        "AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT": cloud,
        "CODE_SIGN_ENTITLEMENTS": entitlements,
        "CODE_SIGN_STYLE": "Automatic",
        "AHOI_PROVISIONING_PROFILE_SPECIFIER": "",
        "AHOI_MANUAL_SIGNING_FALLBACK": "NO",
        "CODE_SIGN_IDENTITY": "",
    }
    if mode == "DebugLocal":
        for name in (
            "AHOI_APPLE_TEAM_ID",
            "AHOI_CLOUDKIT_CONTAINER_ID",
            "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP",
            "AHOI_SYNC_KEYCHAIN_SERVICE",
            "AHOI_SYNC_KEYCHAIN_ACCOUNT",
            "AHOI_SYNC_KEY_VERSION",
            "AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP",
            "AHOI_COMMAND_KEYCHAIN_SERVICE",
            "AHOI_COMMAND_KEYCHAIN_ACCOUNT",
        ):
            values[name] = ""
    return values


def run_preflight(*arguments: str, env: dict[str, str] | None = None):
    return subprocess.run(
        [str(PREFLIGHT), *arguments],
        cwd=ROOT,
        env=env or {**os.environ, "SRCROOT": str(MOBILE)},
        check=False,
        capture_output=True,
        text=True,
    )


def materialize_export(template_name: str, path: pathlib.Path) -> dict:
    raw = (MOBILE / template_name).read_text(encoding="utf-8")
    raw = raw.replace("$(AHOI_MOBILE_BUNDLE_ID)", PUBLIC["AHOI_MOBILE_BUNDLE_ID"])
    raw = raw.replace("$(AHOI_APPLE_TEAM_ID)", PUBLIC["AHOI_APPLE_TEAM_ID"])
    payload = plistlib.loads(raw.encode("utf-8"))
    path.write_bytes(plistlib.dumps(payload, sort_keys=True))
    return payload


class MobileSigningModeTests(unittest.TestCase):
    def assert_preflight_passes(self, mode: str):
        completed = run_preflight("--build-settings", env=mode_environment(mode))
        self.assertEqual(0, completed.returncode, completed.stderr + completed.stdout)

    def assert_preflight_rejects(self, mode: str, **overrides: str):
        environment = mode_environment(mode)
        environment.update(overrides)
        completed = run_preflight("--build-settings", env=environment)
        self.assertNotEqual(0, completed.returncode, completed.stdout)
        self.assertIn("ERROR:", completed.stderr)

    def test_all_five_modes_accept_only_their_exact_positive_contract(self):
        for mode in MODE_CONTRACT:
            with self.subTest(mode=mode):
                self.assert_preflight_passes(mode)

    def test_debug_local_rejects_every_provider_capability(self):
        self.assert_preflight_rejects(
            "DebugLocal",
            AHOI_CLOUDKIT_CONTAINER_ID=PUBLIC["AHOI_CLOUDKIT_CONTAINER_ID"],
        )
        self.assert_preflight_rejects(
            "DebugLocal", CODE_SIGN_ENTITLEMENTS="AhoiMobile.entitlements.template"
        )

    def test_pregrant_modes_reject_default_browser_entitlement(self):
        for mode in ("CloudKitDevelopment", "TestFlightBootstrap"):
            with self.subTest(mode=mode):
                self.assert_preflight_rejects(
                    mode,
                    CODE_SIGN_ENTITLEMENTS="AhoiMobile.DefaultBrowser.entitlements.template",
                )

    def test_postgrant_modes_require_default_browser_entitlement(self):
        for mode in ("DefaultBrowserDevelopment", "ReleasePostGrant"):
            with self.subTest(mode=mode):
                self.assert_preflight_rejects(
                    mode, CODE_SIGN_ENTITLEMENTS="AhoiMobile.entitlements.template"
                )

    def test_public_identity_environment_and_groups_fail_closed(self):
        self.assert_preflight_rejects(
            "CloudKitDevelopment",
            AHOI_CLOUDKIT_CONTAINER_ID="iCloud.de.vossmedien.DisplayPilot",
        )
        self.assert_preflight_rejects(
            "CloudKitDevelopment",
            AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP="248AJ5BN47.foreign.commands",
        )
        self.assert_preflight_rejects(
            "TestFlightBootstrap", AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT="Development"
        )
        self.assert_preflight_rejects(
            "TestFlightBootstrap", AHOI_APS_ENVIRONMENT="development"
        )

    def test_candidate_modes_require_exact_source_commit(self):
        for mode in ("TestFlightBootstrap", "ReleasePostGrant"):
            with self.subTest(mode=mode):
                self.assert_preflight_rejects(
                    mode, AHOI_SOURCE_COMMIT="__AHOI_SOURCE_COMMIT_UNRESOLVED__"
                )

    def test_automatic_is_default_and_manual_is_explicit_fallback(self):
        self.assert_preflight_rejects(
            "CloudKitDevelopment",
            CODE_SIGN_STYLE="Manual",
            CODE_SIGN_IDENTITY="Apple Development",
            AHOI_PROVISIONING_PROFILE_SPECIFIER="Ahoi Development",
        )
        environment = mode_environment("CloudKitDevelopment")
        environment.update(
            {
                "CODE_SIGN_STYLE": "Manual",
                "CODE_SIGN_IDENTITY": "Apple Development",
                "AHOI_PROVISIONING_PROFILE_SPECIFIER": "Ahoi Development",
                "AHOI_MANUAL_SIGNING_FALLBACK": "YES",
            }
        )
        completed = run_preflight("--build-settings", env=environment)
        self.assertEqual(0, completed.returncode, completed.stderr)

    def test_export_templates_are_public_testflight_eligible_and_automatic(self):
        pairs = (
            ("ExportOptions.plist.template", "TestFlightBootstrap"),
            ("ExportOptions.ReleasePostGrant.plist.template", "ReleasePostGrant"),
        )
        with tempfile.TemporaryDirectory(prefix="ahoi-export-tests-") as raw:
            for index, (template, mode) in enumerate(pairs):
                with self.subTest(mode=mode):
                    path = pathlib.Path(raw) / f"export-{index}.plist"
                    materialize_export(template, path)
                    completed = run_preflight(
                        "--export-options", str(path), "--mode", mode
                    )
                    self.assertEqual(0, completed.returncode, completed.stderr)

    def test_internal_only_bootstrap_and_automatic_profile_pin_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-export-negative-") as raw:
            path = pathlib.Path(raw) / "export.plist"
            payload = materialize_export("ExportOptions.plist.template", path)
            payload["testFlightInternalTestingOnly"] = True
            path.write_bytes(plistlib.dumps(payload, sort_keys=True))
            completed = run_preflight(
                "--export-options", str(path), "--mode", "TestFlightBootstrap"
            )
            self.assertNotEqual(0, completed.returncode)
            payload["testFlightInternalTestingOnly"] = False
            payload["provisioningProfiles"] = {
                PUBLIC["AHOI_MOBILE_BUNDLE_ID"]: "Pinned Profile"
            }
            path.write_bytes(plistlib.dumps(payload, sort_keys=True))
            completed = run_preflight(
                "--export-options", str(path), "--mode", "TestFlightBootstrap"
            )
            self.assertNotEqual(0, completed.returncode)

    def test_reviewed_manual_export_fallback_is_supported(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-export-manual-") as raw:
            path = pathlib.Path(raw) / "export.plist"
            payload = materialize_export("ExportOptions.plist.template", path)
            payload.update(
                {
                    "signingStyle": "manual",
                    "signingCertificate": "Apple Distribution",
                    "provisioningProfiles": {
                        PUBLIC["AHOI_MOBILE_BUNDLE_ID"]: "Ahoi App Store"
                    },
                }
            )
            path.write_bytes(plistlib.dumps(payload, sort_keys=True))
            environment = {
                **os.environ,
                "SRCROOT": str(MOBILE),
                "AHOI_MANUAL_SIGNING_FALLBACK": "YES",
            }
            completed = run_preflight(
                "--export-options",
                str(path),
                "--mode",
                "TestFlightBootstrap",
                env=environment,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)

    def test_project_and_entitlement_sources_expose_the_five_mode_topology(self):
        project = (MOBILE / "project.yml").read_text(encoding="utf-8")
        for mode in MODE_CONTRACT:
            self.assertIn(f"  {mode}:", project)
            self.assertIn(f"Configurations/{mode}.xcconfig", project)
        self.assertIn('AhoiSourceCommit: "$(AHOI_SOURCE_COMMIT)"', project)
        self.assertIn('AhoiBuildMode: "$(AHOI_BUILD_MODE)"', project)
        self.assertIn("SWIFT_STRICT_CONCURRENCY: complete", project)
        common = plistlib.loads(
            (MOBILE / "AhoiMobile.entitlements.template").read_bytes()
        )
        postgrant = plistlib.loads(
            (MOBILE / "AhoiMobile.DefaultBrowser.entitlements.template").read_bytes()
        )
        self.assertNotIn("com.apple.developer.web-browser", common)
        self.assertIs(postgrant["com.apple.developer.web-browser"], True)
        self.assertEqual(
            ["$(AHOI_CLOUDKIT_CONTAINER_ID)"],
            common["com.apple.developer.icloud-container-identifiers"],
        )


if __name__ == "__main__":
    unittest.main()
