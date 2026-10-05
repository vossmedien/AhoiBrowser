import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_ROOT = ROOT / "patches/chromium"

INTEGRATION_PATCH = "0001-ahoi-m153-integration-seams.patch"
DETERMINISTIC_PATCH = "0002-ahoi-deterministic-platform-tests.patch"
# Retired 2026-09-25: M153 contains the upstream tracing fix, and the patch
# had shrunk to a reformat of tracing_support.cc.
RETIRED_TRACING_PATCH = "0003-ahoi-upstream-page-load-tracing-test-isolation.patch"
LEAN_GUARDS_PATCH = "0004-ahoi-lean-profile-compose-guards.patch"
# The three foundation layers still lead the series. Since the M153 rebase
# (29dfe7a) further ordered product layers follow them; `series` owns the
# complete order.
FOUNDATION_SERIES = (
    INTEGRATION_PATCH,
    DETERMINISTIC_PATCH,
    LEAN_GUARDS_PATCH,
)
M155_PIN = {
    "version": "155.0.8059.26",
    "milestone": 155,
    "tag": "refs/tags/155.0.8059.26",
    "commit": "16c3e55476d3564bea713314b2fff638749ce3e6",
    "branchHead": 8059,
    "branchHeadPosition": 842,
    "branchPoint": "199ac2cd7ab289e11c2236bd2cede848781213da",
    "branchPosition": 1697595,
    "channel": "Stable",
    "platform": "Mac",
    "rolloutFraction": 0.005,
    "rolloutPolicy": "staged-stable",
    "pinnable": False,
    "source": "https://chromium.googlesource.com/chromium/src.git",
}

DETERMINISTIC_PATHS = (
    "components/autofill/core/browser/metrics/autofill_metrics_test_base.cc",
    "components/input/web_input_event_builders_mac_unittest.mm",
)


def series_entries() -> tuple[str, ...]:
    return tuple(
        line.strip()
        for line in (PATCH_ROOT / "series").read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )


def patch_text(filename: str) -> str:
    return (PATCH_ROOT / filename).read_text(encoding="utf-8")


def touched_paths(payload: str) -> tuple[str, ...]:
    pairs = re.findall(r"^diff --git a/(\S+) b/(\S+)$", payload, re.MULTILINE)
    if any(source != destination for source, destination in pairs):
        raise AssertionError("active patches must not rename paths")
    return tuple(source for source, _ in pairs)


class ProductPatchStackTests(unittest.TestCase):
    def assert_modification_only(self, payload: str) -> None:
        paths = touched_paths(payload)
        self.assertTrue(paths)
        self.assertEqual(len(paths), len(set(paths)))
        for marker in (
            "new file mode",
            "deleted file mode",
            "similarity index",
            "rename from",
            "rename to",
            "GIT binary patch",
        ):
            self.assertNotIn(marker, payload)

    def assert_full_index_modification_only(self, payload: str) -> None:
        paths = touched_paths(payload)
        index_lines = re.findall(
            r"^index ([0-9a-f]{40})\.\.([0-9a-f]{40})(?: [0-7]{6})?$",
            payload,
            re.MULTILINE,
        )
        self.assertTrue(paths)
        self.assertEqual(len(paths), len(index_lines))
        self.assertEqual(len(paths), len(set(paths)))
        for marker in (
            "new file mode",
            "deleted file mode",
            "similarity index",
            "rename from",
            "rename to",
            "GIT binary patch",
        ):
            self.assertNotIn(marker, payload)

    def test_production_pin_is_the_exact_staged_m155_mac_stable(self):
        pin = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
        self.assertEqual(M155_PIN, {key: pin.get(key) for key in M155_PIN})

        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("Chromium M155 patch ledger", ledger)
        self.assertIn(f"Chromium Mac Stable `{M155_PIN['version']}` at", ledger)
        self.assertIn(f"`{M155_PIN['commit']}`", ledger)

    def test_series_leads_with_the_foundation_layers(self):
        entries = series_entries()
        self.assertEqual(FOUNDATION_SERIES, entries[: len(FOUNDATION_SERIES)])
        self.assertEqual(len(entries), len(set(entries)))
        self.assertEqual(
            {path.name for path in PATCH_ROOT.glob("*.patch")}, set(entries)
        )

        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        for filename in entries:
            path = PATCH_ROOT / filename
            with self.subTest(patch=filename):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                payload = path.read_text(encoding="utf-8")
                self.assertTrue(payload.startswith("diff --git a/"))
                self.assertTrue(payload.endswith("\n"))
                self.assert_modification_only(payload)
                if filename in FOUNDATION_SERIES:
                    self.assert_full_index_modification_only(payload)

    def test_every_active_patch_has_exactly_one_ledger_section(self):
        ledger = (PATCH_ROOT / "README.md").read_text(encoding="utf-8")
        missing_or_duplicated = tuple(
            filename
            for filename in series_entries()
            if ledger.count(f"## `{filename}`") != 1
        )
        self.assertEqual((), missing_or_duplicated)

    def test_integration_patch_is_chromium_seams_not_product_owned_overlay(self):
        paths = touched_paths(patch_text(INTEGRATION_PATCH))
        required_seams = {
            "chrome/app/app-Info.plist",
            "chrome/browser/sessions/session_restore.cc",
            "chrome/browser/ui/browser.cc",
            "chrome/browser/ui/tabs/tab_strip_prefs.cc",
            "chrome/browser/ui/views/frame/browser_view.cc",
            "chrome/browser/ui/views/frame/multi_contents_view.cc",
            "chrome/browser/ui/views/tabs/common/split_tab_view.cc",
            "chrome/browser/resources/history/app.ts",
            "chrome/browser/resources/settings/route.ts",
            "chrome/browser/resources/settings/settings_main/settings_main.html",
            "components/embedder_support/user_agent_utils.cc",
            "extensions/browser/extensions_browser_client.cc",
            "services/network/cookie_manager.cc",
            "third_party/blink/renderer/core/page/autoscroll_controller.cc",
            "ui/views/cocoa/drag_drop_client_mac.mm",
        }
        self.assertGreaterEqual(len(paths), 200)
        self.assertTrue(required_seams.issubset(paths))
        self.assertFalse(any(path.startswith("ahoi/") for path in paths))
        self.assertTrue(
            {path.split("/", 1)[0] for path in paths}.issubset(
                {
                    "chrome",
                    "components",
                    "content",
                    "extensions",
                    "ios",
                    "services",
                    "third_party",
                    "tools",
                    "ui",
                }
            )
        )

    def test_test_only_layers_have_exact_disjoint_responsibilities(self):
        integration = set(touched_paths(patch_text(INTEGRATION_PATCH)))
        deterministic = touched_paths(patch_text(DETERMINISTIC_PATCH))

        self.assertEqual(DETERMINISTIC_PATHS, deterministic)
        self.assertTrue(integration.isdisjoint(deterministic))

        deterministic_payload = patch_text(DETERMINISTIC_PATCH)
        self.assertIn(
            'base::Time::FromUTCString("2020-01-01T00:00:00Z", &year2020)',
            deterministic_payload,
        )
        self.assertEqual(
            3,
            deterministic_payload.count(
                "ui::ScopedKeyboardLayout keyboard_layout("
                "ui::KEYBOARD_LAYOUT_ENGLISH_US);"
            ),
        )

    def test_retired_tracing_layer_stays_out_of_the_series(self):
        # M153 ships ResetWebContentsListTrackRegistrationForTesting itself.
        self.assertNotIn(RETIRED_TRACING_PATCH, series_entries())
        self.assertFalse((PATCH_ROOT / RETIRED_TRACING_PATCH).exists())

    def test_preflight_code_binds_current_patch_bytes_instead_of_test_constants(self):
        roll_tool = (ROOT / "tools/chromium_roll.py").read_text(encoding="utf-8")
        self.assertIn('"sha256": hashlib.sha256(payload).hexdigest()', roll_tool)
        self.assertIn('"patches": patch_reports', roll_tool)

    def test_m152_privacy_defaults_follow_current_metrics_backend(self):
        privacy_root = ROOT / "overlay/chromium/src/ahoi/browser/privacy"
        build = (privacy_root / "BUILD.gn").read_text(encoding="utf-8")
        implementation = (privacy_root / "privacy_defaults.cc").read_text(
            encoding="utf-8"
        )
        test = (privacy_root / "privacy_defaults_unittest.cc").read_text(
            encoding="utf-8"
        )
        combined = build + implementation + test

        self.assertNotIn("metrics_reporting_level", combined)
        self.assertNotIn("MetricsReportingLevel", combined)
        self.assertEqual(2, build.count('"//components/metrics",'))
        self.assertIn(
            '"components/metrics/metrics_profile_pref_names.h"', combined
        )
        self.assertIn("metrics::prefs::kMetricsReportingEnabled", implementation)
        self.assertIn("metrics::prefs::kAdvancedReportingEnabled", implementation)
        self.assertIn("metrics::prefs::kAdvancedReportingEnabled", test)

    def test_navigation_views_suite_stages_its_required_ui_test_pack(self):
        build = (
            ROOT / "overlay/chromium/src/ahoi/browser/ui/shell/BUILD.gn"
        ).read_text(encoding="utf-8")
        target = re.search(
            r'test\("ahoi_navigation_surface_state_unittests"\) \{'
            r"[\s\S]*?\n\}",
            build,
        )
        self.assertIsNotNone(target)
        self.assertIn('"//ui/resources:ui_test_pak"', target.group(0))
        self.assertIn('"//ui/resources:ui_test_pak_data"', target.group(0))

    def test_startup_browser_tests_use_current_new_tab_and_policy_apis(self):
        integration = patch_text(INTEGRATION_PATCH)

        self.assertIn('"//chrome/browser/policy:test_support"', integration)
        self.assertIn(
            '#include "chrome/browser/policy/policy_test_utils.h"', integration
        )
        self.assertIn(
            "class AhoiManagedStartupPolicyTest : public policy::PolicyTest {};",
            integration,
        )
        self.assertIn(
            "chrome::NewTab(empty_browser, NewTabTypes::kNewTabCommand);",
            integration,
        )
        self.assertIn("UpdateProviderPolicy(policies);", integration)
        self.assertNotIn("SetManagedPref(", integration)

    def test_startup_browser_tests_cover_real_restore_and_dialog_choices(self):
        integration = patch_text(INTEGRATION_PATCH)
        dialog_header = (
            ROOT
            / "overlay/chromium/src/ahoi/browser/ui/startup/startup_choice_dialog.h"
        ).read_text(encoding="utf-8")
        dialog_implementation = (
            ROOT
            / "overlay/chromium/src/ahoi/browser/ui/startup/startup_choice_dialog.cc"
        ).read_text(encoding="utf-8")

        self.assertIn("void SaveCurrentBrowserAsLastSession", integration)
        self.assertIn(
            "+  void TearDownOnMainThread() override {\n"
            "+    profile_keep_alive_.reset();\n"
            "+    session_keep_alive_.reset();\n"
            "+    extensions::ExtensionBrowserTest::TearDownOnMainThread();\n"
            "+  }",
            integration,
        )
        self.assertIn(
            "AhoiAskContinueRestoresAndRemembersChoice", integration
        )
        self.assertIn("AhoiAskEmptyStaysEmptyAndRemembersChoice", integration)
        self.assertIn(
            "AhoiAskContinueWithoutRememberRestoresOnce", integration
        )
        self.assertIn(
            "AhoiAskEmptyWithoutRememberStaysEmptyOnce", integration
        )
        for test_name in (
            "AhoiStartupChoiceAcceptWithoutRememberReturnsContinue",
            "AhoiStartupChoiceCancelWithoutRememberReturnsEmpty",
            "AhoiStartupChoiceCloseButtonForcesEmptyWithoutRemember",
            "AhoiStartupChoiceWidgetDestroyForcesEmptyWithoutRemember",
        ):
            self.assertIn(test_name, integration)
        self.assertIn(
            "base::test::TestFuture<ahoi::startup::StartupChoiceResult>",
            integration,
        )
        self.assertIn("ASSERT_TRUE(future.Wait());", integration)
        self.assertIn(
            "PostCrashDoesNotAutomaticallyContinueAhoiSession", integration
        )
        self.assertIn(
            "+  StartupBrowserCreator::ClearLaunchedProfilesForTesting();",
            integration,
        )
        self.assertIn(
            "+  const GURL requested_url(url::kAboutBlankURL);",
            integration,
        )
        self.assertIn(
            '#include "chrome/browser/ui/views/session_crashed_bubble_view.h"',
            integration,
        )
        self.assertIn(
            "SessionCrashedBubbleView::GetInstanceForTest()", integration
        )
        self.assertIn(
            "SetRememberStartupChoiceForTesting", dialog_header
        )
        self.assertIn(
            "checkbox->button_controller()->NotifyClick();",
            dialog_implementation,
        )

    def test_four_pane_file_system_access_test_uses_collision_safe_alias(self):
        integration = patch_text(INTEGRATION_PATCH)

        self.assertIn(
            "+using FileSystemAccessRequestType =\n"
            "+    FileSystemAccessPermissionRequestManager::RequestType;",
            integration,
        )
        self.assertIn(
            "+      RequestData(FileSystemAccessRequestType::kRestorePermissions,",
            integration,
        )
        self.assertNotIn(
            "+using RequestType = FileSystemAccessPermissionRequestManager::RequestType;",
            integration,
        )


if __name__ == "__main__":
    unittest.main()
