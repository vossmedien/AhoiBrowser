import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"
STANDARD_IMPORT_PATCH = (
    ROOT / "patches/chromium/0013-ahoi-standard-import-surface.patch"
)
PAGE_ROOT = (
    ROOT
    / "overlay/chromium/src/chrome/browser/resources/settings/ahoi_page"
)
WEBUI_TEST = (
    ROOT
    / "overlay/chromium/src/chrome/test/data/webui/settings/ahoi_page_test.ts"
)
ARC_IMPORT_UI_ROOT = (
    ROOT
    / "overlay/chromium/src/chrome/browser/resources/settings/people_page"
)
ARC_IMPORT_WEBUI_TEST = (
    ROOT
    / "overlay/chromium/src/chrome/test/data/webui/settings/"
    "ahoi_arc_import_section_test.ts"
)
ARC_IMPORT_ROOT = (
    ROOT / "overlay/chromium/src/ahoi/browser/importer/arc"
)


class AhoiSettingsPageContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.patch = PATCH.read_text(encoding="utf-8")
        self.standard_import_patch = STANDARD_IMPORT_PATCH.read_text(
            encoding="utf-8"
        )
        self.page = (PAGE_ROOT / "ahoi_page.html.ts").read_text(encoding="utf-8")
        self.controller = (PAGE_ROOT / "ahoi_page.ts").read_text(encoding="utf-8")
        self.webui_test = WEBUI_TEST.read_text(encoding="utf-8")
        self.arc_import_component = (
            ARC_IMPORT_UI_ROOT / "ahoi_arc_import_section.html.ts"
        ).read_text(encoding="utf-8")
        self.arc_import_controller = (
            ARC_IMPORT_UI_ROOT / "ahoi_arc_import_section.ts"
        ).read_text(encoding="utf-8")
        self.arc_import_webui_test = ARC_IMPORT_WEBUI_TEST.read_text(
            encoding="utf-8"
        )
        self.arc_import_service = (
            ARC_IMPORT_ROOT / "arc_import_service.cc"
        ).read_text(encoding="utf-8")

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
            "ahoi.appearance.sidebar_page_tint_enabled",
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

    def test_sync_opt_in_prepares_local_state_without_cloudkit_transport(self):
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
            "CloudKit-Übertragung ist in diesem Build nicht verfügbar",
            "Lokale Sync-Daten und die ausstehende Outbox bleiben auf "
            "diesem Mac.",
            "Sie werden erst am Übergang zu einem konfigurierten "
            "CloudKit-Transport verschlüsselt.",
            "CloudKit transport is unavailable in this build",
            "Local sync data and its pending outbox remain on this Mac.",
            "They are encrypted only at the boundary to a configured "
            "CloudKit transport.",
        ):
            self.assertIn(marker, self.patch)
        for false_claim in (
            "local encrypted sync",
            "local encrypted sync store",
            "lokale verschlüsselte Synchronisierung",
            "lokalen verschlüsselten Sync-Speicher",
        ):
            self.assertNotIn(false_claim, self.patch)

        sync_control = re.search(
            r'(?s:id="ahoiSyncEnabled".*?)</settings-toggle-button>',
            self.page,
        )
        remote_control = re.search(
            r'(?s:id="ahoiRemoteControlEnabled".*?)</settings-toggle-button>',
            self.page,
        )
        retention_control = re.search(
            r'(?s:id="ahoiHistoryRetention".*?)</settings-dropdown-menu>',
            self.page,
        )
        self.assertIsNotNone(sync_control)
        self.assertIsNotNone(remote_control)
        self.assertIsNotNone(retention_control)
        self.assertNotIn("?disabled", sync_control.group(0))
        self.assertIn(
            "!this.remoteControlStatus_?.canEnable", remote_control.group(0)
        )
        self.assertNotIn("!this.cloudKitAvailable_", remote_control.group(0))
        self.assertNotIn("!this.syncEnabledPref_?.value", remote_control.group(0))
        self.assertNotIn("!this.cloudKitAvailable_", retention_control.group(0))
        self.assertIn(
            "!this.syncEnabledPref_?.value", retention_control.group(0)
        )
        for marker in (
            "syncOptInCanPrepareLocalStateWithoutCloudKitTransport",
            "futureCloudKitAvailabilityUnlocksSafeSyncPrefs",
            "sync.click()",
            "assertFalse(sync.disabled)",
            "assertTrue(remote.disabled)",
            "assertFalse(retention.disabled)",
        ):
            self.assertIn(marker, self.webui_test)

    def test_webui_contract_covers_route_prefs_and_appearance_removal(self):
        for marker in (
            "hasDedicatedRouteAndTopLevelMenuItem",
            "syncOptInCanPrepareLocalStateWithoutCloudKitTransport",
            "sidebarPageTintIsOptionalAndPersistsUserChoice",
            "futureCloudKitAvailabilityUnlocksSafeSyncPrefs",
            "enablingToolkitKeepsOneRecoverableAddressBarEntry",
            "doesNotOwnDedicatedAhoiControls",
            '"ahoi_page_test.ts"',
        ):
            self.assertIn(marker, self.webui_test + self.patch)

    def test_arc_import_uses_standard_surface_and_remains_transactional(self):
        combined_backend = "\n".join(
            (ARC_IMPORT_ROOT / name).read_text(encoding="utf-8")
            for name in (
                "arc_import_backup.cc",
                "arc_import_discovery.cc",
                "arc_import_service.cc",
                "arc_split_runtime.cc",
            )
        )
        for marker in (
            "ArcImportHandler",
            "//ahoi/browser/importer/arc:settings_handler",
            "IDS_SETTINGS_AHOI_ARC_IMPORT_SECTION",
            "Aus Arc importieren",
        ):
            self.assertIn(marker, self.patch)
        self.assertNotIn('id="ahoiArcImportAssistant"', self.page)
        self.assertNotIn("ahoiArcDiscover", self.controller)
        self.assertIn(
            "doesNotOwnASeparateArcImportAssistant", self.webui_test
        )
        for marker in (
            'id="ahoiArcBackupConfirmation"',
            'id="ahoiArcCommitConfirmation"',
            'id="ahoiArcCommit"',
            "this.arcImportPreview_.stats.splits > 0",
            'id="ahoiArcResultSkipped"',
            'id="ahoiArcResultDegraded"',
            'id="ahoiArcResultExcluded"',
            'id="ahoiArcResultFourPane"',
        ):
            self.assertIn(marker, self.arc_import_component)
        for marker in (
            "ahoiArcDiscover",
            "ahoiArcCommit",
            "this.arcBackupConfirmed_",
            "this.arcCommitConfirmed_",
        ):
            self.assertIn(marker, self.arc_import_controller)
        for marker in (
            "import './ahoi_arc_import_section.js';",
            "ahoiImportKind?: 'arc'",
            "if (this.isArcImportSelected_())",
            'id="ahoiArcImport"',
            "arcImportSelected_",
            '"people_page/ahoi_arc_import_section.ts"',
            '"people_page/ahoi_arc_import_section.html.ts"',
            '"people_page/ahoi_arc_import_section.css"',
            "IDS_SETTINGS_AHOI_ARC_IMPORT_RESULT_FOUR_PANE",
            "Annähernd übernommene Vier-Pane-Verhältnisse",
        ):
            self.assertIn(marker, self.standard_import_patch)
        for marker in (
            "IsArcApplicationRunning()",
            "AreArcProfileFilesOpen",
            "FlushPersistenceForBackup",
            "CreateArcImportBackup",
            "RollbackAndFinish",
            "ExistingSplitMatches",
            "committed_journal_state_->idempotency_key == idempotency_key",
        ):
            self.assertIn(marker, combined_backend)
        for marker in (
            "arcUsesTheStandardSourceSelectAndCannotCallStandardImport",
            "splitChoiceOnlyAppearsForRealPreviewSplitsAndCommitIsConfirmed",
            "resultReportsImportedSkippedDegradedExcludedAndFourPane",
            "assertEquals(0, browserProxy.getCallCount('importData'))",
        ):
            self.assertIn(marker, self.arc_import_webui_test)

    def test_pref_service_imports_are_available_on_every_platform(self):
        chromeos_guard = self.controller.index(
            '// <if expr="not is_chromeos">'
        )
        for marker in (
            "import {PrefService} from",
            "import {PrefServiceObserverMixinLit} from",
        ):
            self.assertLess(self.controller.index(marker), chromeos_guard)

    def test_arc_preview_parses_immutable_snapshot_before_handle_scan(self):
        start = self.arc_import_service.index(
            "ArcImportService::DiscoverImport("
        )
        end = self.arc_import_service.index("ArcImportService::CommitResult")
        discovery = self.arc_import_service[start:end]
        ordered_markers = (
            "InspectDefaultArcApplication()",
            "DiscoverArcSourceAt(application_support_dir)",
            "CaptureArcSnapshot(*discovery.source)",
            "ParseArcSnapshot(std::move(*snapshot.snapshot))",
            "AreArcProfileFilesOpen(*discovery.source)",
        )
        positions = [discovery.index(marker) for marker in ordered_markers]
        self.assertEqual(sorted(positions), positions)
        self.assertNotIn("IsArcApplicationRunning()", discovery)

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
