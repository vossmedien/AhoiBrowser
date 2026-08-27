import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"
PAGE_ROOT = (
    ROOT
    / "overlay/chromium/src/chrome/browser/resources/settings/ahoi_page"
)
WEBUI_TEST = (
    ROOT
    / "overlay/chromium/src/chrome/test/data/webui/settings/ahoi_page_test.ts"
)


class AhoiSettingsPageContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.patch = PATCH.read_text(encoding="utf-8")
        self.page = (PAGE_ROOT / "ahoi_page.html.ts").read_text(encoding="utf-8")
        self.controller = (PAGE_ROOT / "ahoi_page.ts").read_text(encoding="utf-8")
        self.webui_test = WEBUI_TEST.read_text(encoding="utf-8")

    def test_route_menu_main_and_build_are_first_class(self):
        for marker in (
            "AHOI: Route;",
            "'/ahoi', 'ahoi', loadTimeData.getString('ahoiPageTitle')",
            'id="ahoi" href="/ahoi"',
            '<div slot="view" id="ahoi">',
            "import '../ahoi_page/ahoi_page.js';",
            '"ahoi_page/ahoi_page.ts"',
            '"ahoi_page/ahoi_page.html.ts"',
            '"ahoi_page/ahoi_page.css"',
        ):
            self.assertIn(marker, self.patch)

    def test_lit_page_inputs_follow_build_webui_without_duplicate_ts_output(self):
        build_diff = re.search(
            r"^diff --git a/chrome/browser/resources/settings/BUILD\.gn "
            r"b/chrome/browser/resources/settings/BUILD\.gn\n"
            r"(.*?)(?=^diff --git |\Z)",
            self.patch,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(build_diff)
        payload = build_diff.group(1)

        # This page authors its Lit template as .html.ts, so both TS sources
        # belong to ts_files. Listing the controller as a web component would
        # append it to all_ts_files a second time and generate ahoi_page.js
        # twice.
        self.assertEqual(1, payload.count('+    "ahoi_page/ahoi_page.ts"'))
        self.assertEqual(
            1, payload.count('+    "ahoi_page/ahoi_page.html.ts"')
        )
        self.assertEqual(1, payload.count('+    "ahoi_page/ahoi_page.css"'))
        self.assertNotIn('"ahoi_page/ahoi_page.html"', payload)

    def test_all_ahoi_controls_have_one_owner_outside_appearance(self):
        pref_keys = (
            "ahoi.appearance.glass_enabled",
            "ahoi.navigation.floating_auto_hide_enabled",
            "ahoi.navigation.floating_reveal_notch_enabled",
            "ahoi.navigation.floating_auto_hide_delay_ms",
            "ahoi.sync.enabled",
            "ahoi.sync.remote_control.enabled",
            "ahoi.sync.history_retention_days",
            "ahoi.developer_toolkit.enabled",
            "ahoi.developer_toolbar.show_cookie_button",
            "ahoi.developer_toolbar.show_cache_button",
            "ahoi.developer_toolbar.show_toolkit_button",
        )
        for pref_key in pref_keys:
            self.assertIn(pref_key, self.page + self.controller)

        appearance_diffs = re.findall(
            r"^diff --git a/(chrome/browser/resources/settings/appearance_page/\S+)"
            r" b/\1\n(.*?)(?=^diff --git |\Z)",
            self.patch,
            re.MULTILINE | re.DOTALL,
        )
        for _, payload in appearance_diffs:
            self.assertNotIn("ahoi.", payload)
            self.assertNotIn("#ahoi", payload)

    def test_sync_prefs_are_safe_and_explicitly_not_google_sync(self):
        for marker in (
            "kSyncEnabledPref",
            "kRemoteControlEnabledPref",
            "kHistoryRetentionDaysPref",
            "settings_api::PrefType::kBoolean",
            "settings_api::PrefType::kNumber",
            "IDS_SETTINGS_AHOI_SYNC_PROVIDER_TITLE",
            "IDS_SETTINGS_AHOI_SYNC_PROVIDER_SUBLABEL",
            "IDS_SETTINGS_AHOI_CLOUDKIT_UNAVAILABLE_TITLE",
            "IDS_SETTINGS_AHOI_CLOUDKIT_UNAVAILABLE_SUBLABEL",
        ):
            self.assertIn(marker, self.patch)

        combined = self.patch + self.page + self.controller
        self.assertNotIn("kApprovedRemoteCommandKeysPref", combined)
        self.assertNotIn("approved_public_keys", combined)
        self.assertNotIn("kDeviceIdPref", combined)
        self.assertNotIn("ahoi.sync.device_id", combined)

    def test_cloudkit_availability_is_false_and_gates_every_dependent_control(self):
        combined = self.page + self.controller

        self.assertIn(
            'AddBoolean("ahoiCloudKitAvailable", false)', self.patch
        )
        self.assertIn(
            "loadTimeData.getBoolean('ahoiCloudKitAvailable')",
            self.controller,
        )
        self.assertIn('id="ahoiCloudKitUnavailableStatus"', self.page)
        self.assertIn('role="status" aria-live="polite"', self.page)
        for marker in (
            "IDS_SETTINGS_AHOI_CLOUDKIT_UNAVAILABLE_TITLE",
            "IDS_SETTINGS_AHOI_CLOUDKIT_UNAVAILABLE_SUBLABEL",
            '<translation id="8805387034065522283">',
            '<translation id="9198210059928613224">',
        ):
            self.assertIn(marker, self.patch)
        self.assertEqual(
            2,
            self.patch.count('<translation id="8805387034065522283">'),
        )
        self.assertEqual(
            2,
            self.patch.count('<translation id="9198210059928613224">'),
        )
        for control_id in (
            "ahoiSyncEnabled",
            "ahoiRemoteControlEnabled",
            "ahoiHistoryRetention",
        ):
            self.assertRegex(
                self.page,
                rf'(?s:id="{control_id}".*?)'
                r'\?disabled="\$\{!this\.cloudKitAvailable_',
            )
        self.assertGreaterEqual(combined.count("!this.cloudKitAvailable_"), 4)
        for marker in (
            "cloudKitCapabilityGateDisablesUnavailableControls",
            "futureCloudKitAvailabilityUnlocksSafeSyncPrefs",
            "await prefService.setPrefValue('ahoi.sync.enabled', true)",
            "assertTrue(sync.disabled)",
            "assertTrue(remote.disabled)",
            "assertTrue(retention.disabled)",
        ):
            self.assertIn(marker, self.webui_test)

    def test_webui_contract_covers_route_prefs_and_appearance_removal(self):
        for marker in (
            "hasDedicatedRouteAndTopLevelMenuItem",
            "cloudKitCapabilityGateDisablesUnavailableControls",
            "futureCloudKitAvailabilityUnlocksSafeSyncPrefs",
            "enablingToolkitKeepsOneRecoverableAddressBarEntry",
            "doesNotOwnDedicatedAhoiControls",
            '"ahoi_page_test.ts"',
        ):
            self.assertIn(marker, self.webui_test + self.patch)

    def test_pref_service_imports_are_available_on_every_platform(self):
        chromeos_guard = self.controller.index(
            '// <if expr="not is_chromeos">'
        )
        for marker in (
            "import {PrefService} from",
            "import {PrefServiceObserverMixinLit} from",
        ):
            self.assertLess(self.controller.index(marker), chromeos_guard)

    def test_webui_build_entry_has_mocha_browser_launcher(self):
        build_diff = re.search(
            r"^diff --git a/chrome/test/data/webui/settings/BUILD\.gn "
            r"b/chrome/test/data/webui/settings/BUILD\.gn\n"
            r"(.*?)(?=^diff --git |\Z)",
            self.patch,
            re.MULTILINE | re.DOTALL,
        )
        launcher_diff = re.search(
            r"^diff --git "
            r"a/chrome/test/data/webui/settings/settings_browsertest\.cc "
            r"b/chrome/test/data/webui/settings/settings_browsertest\.cc\n"
            r"(.*?)(?=^diff --git |\Z)",
            self.patch,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(build_diff)
        self.assertIsNotNone(launcher_diff)
        self.assertIn('+    "ahoi_page_test.ts",', build_diff.group(1))
        self.assertIn(
            "+IN_PROC_BROWSER_TEST_F(SettingsTest, AhoiPage)",
            launcher_diff.group(1),
        )
        self.assertIn(
            'settings/ahoi_page_test.js", "mocha.run()"',
            launcher_diff.group(1),
        )

    def test_real_browser_pref_roundtrip_is_wired(self):
        browser_test = (
            ROOT
            / "overlay/chromium/src/ahoi/browser/ui/settings/"
            "ahoi_settings_browsertest.cc"
        ).read_text(encoding="utf-8")
        build = (
            ROOT / "overlay/chromium/src/ahoi/browser/ui/settings/BUILD.gn"
        ).read_text(encoding="utf-8")

        for marker in (
            "AhoiRouteRoundTripsLiveProfilePref",
            'chrome::GetSettingsUrl("ahoi")',
            "#ahoi.active settings-ahoi-page",
            "ahoiDeveloperToolkitEnabled",
            "GetBoolean(ahoi::developer_toolkit_prefs::kToolkitEnabled)",
        ):
            self.assertIn(marker, browser_test)
        for marker in (
            'sources = [ "ahoi_settings_browsertest.cc" ]',
            'test("ahoi_settings_browsertests")',
        ):
            self.assertIn(marker, build)

        self.assertIn(
            "{route: routes.AHOI, pluginTag: 'settings-ahoi-page'}",
            self.patch,
        )


if __name__ == "__main__":
    unittest.main()
