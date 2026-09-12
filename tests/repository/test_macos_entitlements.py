import copy
import datetime as dt
import json
import pathlib
import plistlib
import sys
import tempfile
import unittest
from unittest import mock


from tools.verify_macos_entitlements import (
    CLOUDKIT_ENTITLEMENT_KEYS,
    development_acceptance_policy,
    expected_rule_entitlements,
    load_policy,
    parse_entitlements,
    runtime_build_settings,
    validate_provisioning_profile,
    verify,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from release import signing  # noqa: E402


POLICY_PATH = ROOT / "config/macos-entitlements.json"
NOW = dt.datetime(2026, 8, 30, tzinfo=dt.timezone.utc)


def profile_fixture(policy: dict, profile_name: str) -> dict:
    browser = next(rule for rule in policy["rules"] if rule["id"] == "browser-app")
    expected = expected_rule_entitlements(policy, profile_name, browser)
    entitlements = {key: copy.deepcopy(expected[key]) for key in CLOUDKIT_ENTITLEMENT_KEYS}
    entitlements["get-task-allow"] = profile_name == "cloudkit-development"
    value = {
        "UUID": "11111111-2222-3333-4444-555555555555",
        "Name": f"AhoiBrowser {profile_name}",
        "TeamIdentifier": ["248AJ5BN47"],
        "ApplicationIdentifierPrefix": ["248AJ5BN47"],
        "Platform": ["OSX"],
        "DeveloperCertificates": [b"certificate-fixture"],
        "CreationDate": NOW - dt.timedelta(days=1),
        "ExpirationDate": NOW + dt.timedelta(days=365),
        "Entitlements": entitlements,
    }
    if profile_name == "cloudkit-development":
        value["ProvisionedDevices"] = ["MAC-DEVICE-UDID"]
    else:
        value["ProvisionsAllDevices"] = True
    return value


class MacOSEntitlementPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = load_policy(POLICY_PATH)

    def test_policy_is_bound_to_current_chromium_and_framework_version(self):
        pin = json.loads((ROOT / "config/chromium.json").read_text())
        self.assertEqual(pin["commit"], self.policy["chromiumCommit"])
        self.assertEqual(pin["version"], self.policy["chromiumVersion"])
        self.assertEqual(pin["version"], self.policy["frameworkVersion"])
        self.assertEqual("provider-free", self.policy["defaultSigningProfile"])
        self.assertEqual(
            {
                "provider-free",
                "cloudkit-development",
                "cloudkit-production",
            },
            set(self.policy["signingProfiles"]),
        )

    def test_development_acceptance_scope_preserves_rights_and_binds_runtime(self):
        scope = ROOT / "artifacts/e2e/shared-sync-development-scope-20260908.json"
        scoped = development_acceptance_policy(self.policy, scope, "cloudkit-development")
        self.assertEqual(self.policy["rules"], scoped["rules"])
        self.assertEqual(self.policy["publicIdentity"], scoped["publicIdentity"])
        self.assertEqual(self.policy["signingProfiles"], scoped["signingProfiles"])
        expected = json.loads(scope.read_text())
        runtime = runtime_build_settings(scoped, "cloudkit-development")
        self.assertEqual(expected["zoneName"], runtime["AHOI_CLOUDKIT_ZONE_NAME"])
        self.assertEqual(expected["subscriptionIdentifier"], runtime["AHOI_CLOUDKIT_SUBSCRIPTION_ID"])
        self.assertEqual(expected["syncKeychainAccount"], runtime["AHOI_SYNC_KEYCHAIN_ACCOUNT"])
        self.assertEqual(expected["scopeID"], runtime["AHOI_SYNC_ACCEPTANCE_SCOPE_ID"])
        self.assertEqual("payload-key", self.policy["runtimeConfiguration"]["syncKeychainAccount"])
        with self.assertRaises(SystemExit):
            runtime_build_settings(scoped, "cloudkit-production")

    def test_development_acceptance_scope_rejects_cross_scope_and_privilege_changes(self):
        source = ROOT / "artifacts/e2e/shared-sync-development-scope-20260908.json"
        scope = json.loads(source.read_text())
        with tempfile.TemporaryDirectory() as directory:
            candidate = pathlib.Path(directory) / "scope.json"
            for key, value in (
                ("zoneName", "AhoiBrowserSyncV3"),
                ("syncKeychainAccount", "payload-key"),
                ("containerIdentifier", "iCloud.other"),
                ("syncKeychainAccessGroup", "248AJ5BN47.*"),
                ("syncKeyVersion", "2"),
                ("environment", "Production"),
                ("schemaVersion", True),
            ):
                with self.subTest(key=key):
                    changed = {**scope, key: value}
                    candidate.write_text(json.dumps(changed))
                    with self.assertRaises(SystemExit):
                        development_acceptance_policy(self.policy, candidate, "cloudkit-development")
        for profile in ("provider-free", "cloudkit-production"):
            with self.subTest(profile=profile), self.assertRaises(SystemExit):
                development_acceptance_policy(self.policy, source, profile)

    def test_browser_requires_exact_upstream_entitlements(self):
        expected = self.policy["rules"][0]["entitlements"]
        role = verify(self.policy, "Contents/MacOS/AhoiBrowser", expected)
        self.assertEqual("browser-app", role)

        missing = copy.deepcopy(expected)
        missing.pop("com.apple.security.device.camera")
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/AhoiBrowser", missing)

    def test_jit_is_allowed_only_for_renderer_and_gpu(self):
        renderer = (
            "Contents/Frameworks/AhoiBrowser Framework.framework/"
            "Versions/152.0.7977.65/"
            "Helpers/AhoiBrowser Helper (Renderer).app/Contents/MacOS/"
            "AhoiBrowser Helper (Renderer)"
        )
        jit = {"com.apple.security.cs.allow-jit": True}
        self.assertEqual("renderer-helper", verify(self.policy, renderer, jit))

        generic = (
            "Contents/Frameworks/AhoiBrowser Framework.framework/"
            "Versions/152.0.7977.65/"
            "Helpers/AhoiBrowser Helper.app/Contents/MacOS/AhoiBrowser Helper"
        )
        with self.assertRaises(SystemExit):
            verify(self.policy, generic, jit)
        self.assertEqual("generic-helper", verify(self.policy, generic, {}))

        stale_renderer = renderer.replace("152.0.7977.65", "151.0.7922.170")
        with self.assertRaises(SystemExit):
            verify(self.policy, stale_renderer, jit)

    def test_risky_or_unknown_entitlements_fail_closed(self):
        actual = {"com.apple.security.cs.disable-library-validation": True}
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/AhoiBrowser", actual)
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/UnknownHelper", {})

    def test_cloudkit_profiles_are_exact_and_environment_specific(self):
        browser = next(
            rule for rule in self.policy["rules"] if rule["id"] == "browser-app"
        )
        development = expected_rule_entitlements(
            self.policy, "cloudkit-development", browser
        )
        production = expected_rule_entitlements(
            self.policy, "cloudkit-production", browser
        )
        for profile_name, expected, cloud, aps in (
            ("cloudkit-development", development, "Development", "development"),
            ("cloudkit-production", production, "Production", "production"),
        ):
            with self.subTest(profile=profile_name):
                self.assertEqual(
                    "browser-app",
                    verify(
                        self.policy,
                        "Contents/MacOS/AhoiBrowser",
                        expected,
                        profile_name,
                    ),
                )
                self.assertEqual(
                    ["iCloud.app.ahoibrowser.AhoiBrowser"],
                    expected["com.apple.developer.icloud-container-identifiers"],
                )
                self.assertEqual(
                    [
                        "248AJ5BN47.app.ahoibrowser.sync",
                        "248AJ5BN47.app.ahoibrowser.commands",
                    ],
                    expected["keychain-access-groups"],
                )
                self.assertEqual(
                    cloud,
                    expected["com.apple.developer.icloud-container-environment"],
                )
                self.assertEqual(
                    aps, expected["com.apple.developer.aps-environment"]
                )
                self.assertNotIn("com.apple.developer.web-browser", expected)

    def test_cloudkit_profiles_reject_foreign_container_group_and_wildcard(self):
        browser = next(
            rule for rule in self.policy["rules"] if rule["id"] == "browser-app"
        )
        expected = expected_rule_entitlements(
            self.policy, "cloudkit-development", browser
        )
        for key, value in (
            (
                "com.apple.developer.icloud-container-identifiers",
                ["iCloud.de.vossmedien.DisplayPilot"],
            ),
            ("keychain-access-groups", ["248AJ5BN47.*"]),
            (
                "keychain-access-groups",
                ["248AJ5BN47.app.ahoibrowser.sync", "248AJ5BN47.foreign"],
            ),
        ):
            with self.subTest(key=key, value=value):
                wrong = copy.deepcopy(expected)
                wrong[key] = value
                with self.assertRaises(SystemExit):
                    verify(
                        self.policy,
                        "Contents/MacOS/AhoiBrowser",
                        wrong,
                        "cloudkit-development",
                    )

    def test_concrete_development_and_production_profiles_validate(self):
        for profile_name in ("cloudkit-development", "cloudkit-production"):
            with self.subTest(profile=profile_name):
                metadata = validate_provisioning_profile(
                    self.policy,
                    profile_name,
                    profile_fixture(self.policy, profile_name),
                    now=NOW,
                )
                self.assertEqual(
                    profile_name.split("-")[1].capitalize(),
                    metadata["cloudKitEnvironment"],
                )
                self.assertEqual(
                    "iCloud.app.ahoibrowser.AhoiBrowser",
                    metadata["cloudKitContainerIdentifier"],
                )

    def test_profile_readback_rejects_wrong_mode_wildcards_and_device_shape(self):
        wrong_environment = profile_fixture(self.policy, "cloudkit-production")
        wrong_environment["Entitlements"][
            "com.apple.developer.icloud-container-environment"
        ] = "Development"
        with self.assertRaises(SystemExit):
            validate_provisioning_profile(
                self.policy, "cloudkit-production", wrong_environment, now=NOW
            )

        wildcard = profile_fixture(self.policy, "cloudkit-development")
        wildcard["Entitlements"]["keychain-access-groups"] = ["WRONG00000.*"]
        with self.assertRaises(SystemExit):
            validate_provisioning_profile(
                self.policy, "cloudkit-development", wildcard, now=NOW
            )

        development_as_distribution = profile_fixture(
            self.policy, "cloudkit-development"
        )
        development_as_distribution.pop("ProvisionedDevices")
        development_as_distribution["ProvisionsAllDevices"] = True
        with self.assertRaises(SystemExit):
            validate_provisioning_profile(
                self.policy,
                "cloudkit-development",
                development_as_distribution,
                now=NOW,
            )

    def test_apple_mac_profile_allowlist_authorizes_only_exact_app_claims(self):
        profile = profile_fixture(self.policy, "cloudkit-development")
        entitlements = profile["Entitlements"]
        entitlements.pop("get-task-allow")
        entitlements["com.apple.developer.icloud-container-environment"] = [
            "Production", "Development"
        ]
        entitlements["com.apple.developer.icloud-services"] = "*"
        entitlements["keychain-access-groups"] = ["248AJ5BN47.*"]
        metadata = validate_provisioning_profile(
            self.policy, "cloudkit-development", profile, now=NOW
        )
        self.assertEqual("Development", metadata["cloudKitEnvironment"])
        self.assertEqual(self.policy["publicIdentity"]["keychainAccessGroups"],
                         metadata["keychainAccessGroups"])
        browser = self.policy["rules"][0]
        claims = expected_rule_entitlements(self.policy, "cloudkit-development", browser)
        self.assertEqual("browser-app", verify(self.policy, "Contents/MacOS/AhoiBrowser",
                                                claims, "cloudkit-development"))
        claims["keychain-access-groups"] = ["248AJ5BN47.*"]
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/AhoiBrowser", claims, "cloudkit-development")
        entitlements["com.apple.developer.icloud-container-environment"] = ["Production"]
        with self.assertRaises(SystemExit):
            validate_provisioning_profile(self.policy, "cloudkit-development", profile, now=NOW)

    def test_overlay_contracts_match_policy_without_placeholders(self):
        sync_root = ROOT / "overlay/chromium/src/ahoi/browser/sync"
        browser = next(
            rule for rule in self.policy["rules"] if rule["id"] == "browser-app"
        )
        for file_name, profile_name in (
            (
                "AhoiBrowserCloudKit.entitlements.template",
                "cloudkit-development",
            ),
            (
                "AhoiBrowserCloudKit.Production.entitlements.template",
                "cloudkit-production",
            ),
        ):
            with self.subTest(profile=profile_name):
                raw = (sync_root / file_name).read_bytes()
                self.assertNotIn(b"$(", raw)
                self.assertNotIn(b"DisplayPilot", raw)
                fragment = plistlib.loads(raw)
                expected = expected_rule_entitlements(
                    self.policy, profile_name, browser
                )
                self.assertEqual(
                    {key: expected[key] for key in CLOUDKIT_ENTITLEMENT_KEYS},
                    fragment,
                )

        xcconfig = (
            sync_root / "AhoiBrowserCloudKit.xcconfig.template"
        ).read_text(encoding="utf-8")
        self.assertNotIn("$(", xcconfig)
        self.assertNotIn("DisplayPilot", xcconfig)
        for value in (
            "248AJ5BN47",
            "app.ahoibrowser.AhoiBrowser",
            "iCloud.app.ahoibrowser.AhoiBrowser",
            "248AJ5BN47.app.ahoibrowser.sync",
            "248AJ5BN47.app.ahoibrowser.commands",
            "Development",
            "Production",
        ):
            self.assertIn(value, xcconfig)

    def test_preparation_embeds_exact_profile_and_stamps_runtime_readback(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-mac-cloudkit-") as directory:
            root = pathlib.Path(directory)
            app = root / "AhoiBrowser.app"
            info_path = app / "Contents/Info.plist"
            info_path.parent.mkdir(parents=True)
            info_path.write_bytes(
                plistlib.dumps(
                    {"CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser"}
                )
            )
            profile = root / "AhoiBrowser.provisionprofile"
            profile.write_bytes(b"cms-profile-fixture")
            entitlements = root / "AhoiBrowser.entitlements"
            metadata = {
                "uuid": "11111111-2222-3333-4444-555555555555",
                "name": "AhoiBrowser cloudkit-development",
                "teamIdentifier": "248AJ5BN47",
                "appIdentifierPrefix": "248AJ5BN47",
                "bundleIdentifier": "app.ahoibrowser.AhoiBrowser",
                "cloudKitContainerIdentifier": "iCloud.app.ahoibrowser.AhoiBrowser",
                "keychainAccessGroups": [
                    "248AJ5BN47.app.ahoibrowser.sync",
                    "248AJ5BN47.app.ahoibrowser.commands",
                ],
                "cloudKitEnvironment": "Development",
                "apsEnvironment": "development",
                "expiration": "2027-08-30T00:00:00Z",
            }
            with mock.patch.object(
                signing, "_profile_metadata", return_value=metadata
            ):
                result = signing.prepare_cloudkit_app(
                    app,
                    signing_profile_name="cloudkit-development",
                    provisioning_profile_path=profile,
                    policy_path=POLICY_PATH,
                    entitlements_output=entitlements,
                )
            stamped = plistlib.loads(info_path.read_bytes())
            self.assertEqual(
                "iCloud.app.ahoibrowser.AhoiBrowser",
                stamped["AHOI_CLOUDKIT_CONTAINER_ID"],
            )
            self.assertEqual("Development", stamped["AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT"])
            self.assertEqual("development", stamped["AHOI_APS_ENVIRONMENT"])
            self.assertEqual(
                profile.read_bytes(),
                (app / "Contents/embedded.provisionprofile").read_bytes(),
            )
            self.assertEqual("cloudkit-development", result["signingProfile"])
            self.assertEqual(
                "Development",
                plistlib.loads(entitlements.read_bytes())[
                    "com.apple.developer.icloud-container-environment"
                ],
            )

    def test_plist_parser_accepts_empty_and_binary_payloads(self):
        self.assertEqual({}, parse_entitlements(b"\n"))
        payload = {"com.apple.security.cs.allow-jit": True}
        self.assertEqual(
            payload,
            parse_entitlements(plistlib.dumps(payload, fmt=plistlib.FMT_BINARY)),
        )


if __name__ == "__main__":
    unittest.main()
