import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ZEN_ROOT = ROOT / "overlay/chromium/src/ahoi/browser/importer/zen"
CHROMIUM = ROOT / ".work/chromium/src"
PEOPLE_PAGE = CHROMIUM / "chrome/browser/resources/settings/people_page"
WEBUI_TEST = (
    ROOT
    / "overlay/chromium/src/chrome/test/data/webui/settings/"
    "ahoi_zen_import_availability_test.ts"
)
PATCH = ROOT / "patches/chromium/0015-ahoi-zen-import-availability.patch"


class ZenImportAvailabilityContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.discovery_header = (
            ZEN_ROOT / "zen_profile_discovery.h"
        ).read_text(encoding="utf-8")
        self.discovery = (ZEN_ROOT / "zen_profile_discovery.cc").read_text(
            encoding="utf-8"
        )
        self.discovery_test = (
            ZEN_ROOT / "zen_profile_discovery_unittest.cc"
        ).read_text(encoding="utf-8")
        self.importer_header = (
            CHROMIUM / "chrome/browser/importer/importer_list.h"
        ).read_text(encoding="utf-8")
        self.importer = (
            CHROMIUM / "chrome/browser/importer/importer_list.cc"
        ).read_text(encoding="utf-8")
        self.handler = (
            CHROMIUM
            / "chrome/browser/ui/webui/settings/import_data_handler.cc"
        ).read_text(encoding="utf-8")
        self.proxy = (PEOPLE_PAGE / "import_data_browser_proxy.ts").read_text(
            encoding="utf-8"
        )
        self.dialog = (PEOPLE_PAGE / "import_data_dialog.ts").read_text(
            encoding="utf-8"
        )
        self.dialog_html = (PEOPLE_PAGE / "import_data_dialog.html").read_text(
            encoding="utf-8"
        )
        self.webui_test = WEBUI_TEST.read_text(encoding="utf-8")
        self.patch = PATCH.read_text(encoding="utf-8")

    def test_application_availability_is_deterministic_and_fail_closed(self):
        for marker in (
            "ZenImportAvailability AppendZenSourceProfiles(",
            "AppendZenSourceProfilesForApplication",
            "GetZenImportAvailability(application)",
            "availability != ZenImportAvailability::kAvailable",
            "return availability;",
        ):
            self.assertIn(marker, self.discovery_header + self.discovery)
        for marker in (
            "DoesNotAppendAnUninstalledApplication",
            "ZenImportAvailability::kNotInstalled",
            "ReportsRunningApplicationWithoutAppending",
            "ZenImportAvailability::kSourceRunning",
            "AppendsAvailableApplicationWithResourcePath",
            "ZenImportAvailability::kAvailable",
            'AppendASCII("Contents/Resources")',
        ):
            self.assertIn(marker, self.discovery_test)

    def test_metadata_stays_index_aligned_and_not_installed_has_no_entry(self):
        combined = self.importer_header + self.importer
        for marker in (
            "DetectedImporterSourceProfiles",
            "ImporterSourceProfileMetadata",
            "ImporterSourceProfileKind::kZen",
            "ImporterSourceProfileDisabledReason::kSourceRunning",
            "detected_profiles.metadata.resize(profiles.size())",
            "CHECK_EQ(detected_profiles.profiles.size(),",
            "source_profile_metadata_ = std::move(profiles.metadata)",
        ):
            self.assertIn(marker, combined)

        running_branch = re.search(
            r"if \(zen_availability ==\s*"
            r"ahoi::importer::zen::ZenImportAvailability::kSourceRunning\) "
            r"\{(?P<body>.*?)\n    \}",
            self.importer,
            re.DOTALL,
        )
        self.assertIsNotNone(running_branch)
        self.assertIn("profiles.push_back", running_branch.group("body"))
        self.assertNotIn("kNotInstalled", running_branch.group("body"))
        self.assertNotRegex(
            self.importer,
            r"kNotInstalled(?s:.*?)profiles\.push_back",
        )

    def test_payload_and_backend_treat_disabled_state_as_authoritative(self):
        for marker in (
            'browser_profile.Set("present", true)',
            'browser_profile.Set("available",',
            '"disabledReason"',
            'browser_profile.Set("ahoiImportKind", kAhoiImportKindZen)',
            "IsSourceProfileAvailable(browser_index)",
            "Rejected import request for a disabled source profile",
        ):
            self.assertIn(marker, self.handler)
        self.assertLess(
            self.handler.index("IsSourceProfileAvailable(browser_index)"),
            self.handler.index("const base::Value& types"),
        )
        self.assertLess(
            self.handler.index("IsSourceProfileAvailable(browser_index)"),
            self.handler.index("const base::DictValue& type_dict"),
        )

    def test_webui_disables_running_zen_and_uses_backend_index(self):
        for marker in (
            "ahoiImportKind?: 'arc'|'zen'",
            "present?: boolean",
            "available?: boolean",
            "disabledReason?: ImportSourceDisabledReason",
        ):
            self.assertIn(marker, self.proxy)
        for marker in (
            'disabled$="[[isSourceUnavailable_(item.available)]]"',
            'id="sourceDisabledReason"',
            'role="status"',
            'aria-live="polite"',
            "item.disabledReason",
        ):
            self.assertIn(marker, self.dialog_html)
        for marker in (
            "this.i18n('ahoiZenImportCloseSource')",
            "available === false",
            "this.isSourceUnavailable_(this.selected_?.available)",
            "this.browserProxy_.importData(this.selected_.index, types)",
        ):
            self.assertIn(marker, self.dialog)

    def test_strings_tests_and_browser_launcher_cover_all_three_states(self):
        generated_resources = (
            CHROMIUM / "chrome/app/generated_resources.grd"
        ).read_text(encoding="utf-8")
        german = (
            CHROMIUM / "chrome/app/resources/generated_resources_de.xtb"
        ).read_text(encoding="utf-8")
        provider = (
            CHROMIUM
            / "chrome/browser/ui/webui/settings/"
            "settings_localized_strings_provider.cc"
        ).read_text(encoding="utf-8")
        build = (
            CHROMIUM / "chrome/test/data/webui/settings/BUILD.gn"
        ).read_text(encoding="utf-8")
        launcher = (
            CHROMIUM
            / "chrome/test/data/webui/settings/settings_browsertest.cc"
        ).read_text(encoding="utf-8")
        for marker in (
            "IDS_SETTINGS_AHOI_ZEN_IMPORT_CLOSE_SOURCE",
            "Close Zen before importing",
        ):
            self.assertIn(marker, generated_resources)
        self.assertIn(
            '<translation id="6428146992608115427">'
            "Zen vor dem Import schließen</translation>",
            german,
        )
        self.assertIn("ahoiZenImportCloseSource", provider)
        for marker in (
            "runningZenIsVisibleDisabledAndCannotStartAnImport",
            "notInstalledZenDoesNotCreateAPhantomOption",
            "availableZenUsesItsStableBackendIndexAndRealCategories",
            "assertEquals(0, proxy.getCallCount('importData'))",
        ):
            self.assertIn(marker, self.webui_test)
        self.assertIn('"ahoi_zen_import_availability_test.ts"', build)
        self.assertIn("AhoiZenImportAvailability", launcher)

    def test_patch_contains_only_the_focused_chromium_seams(self):
        for path in (
            "chrome/browser/importer/importer_list.h",
            "chrome/browser/importer/importer_list.cc",
            "chrome/browser/ui/webui/settings/import_data_handler.cc",
            "chrome/browser/resources/settings/people_page/"
            "import_data_browser_proxy.ts",
            "chrome/browser/resources/settings/people_page/"
            "import_data_dialog.ts",
            "chrome/browser/resources/settings/people_page/"
            "import_data_dialog.html",
            "chrome/app/generated_resources.grd",
            "chrome/app/resources/generated_resources_de.xtb",
            "chrome/browser/ui/webui/settings/"
            "settings_localized_strings_provider.cc",
            "chrome/test/data/webui/settings/"
            "ahoi_zen_import_availability_test.ts",
        ):
            self.assertIn(path, self.patch)
        for forbidden in (
            "ahoi/browser/importer/arc/",
            "ahoi/browser/extensions/",
            "patches/chromium/series",
        ):
            self.assertNotIn(forbidden, self.patch)


if __name__ == "__main__":
    unittest.main()
