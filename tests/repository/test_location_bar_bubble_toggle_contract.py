import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"
DEVELOPER_UI = (
    ROOT / "overlay/chromium/src/ahoi/browser/ui/developer_toolkit"
)
PRIVACY_UI = ROOT / "overlay/chromium/src/ahoi/browser/ui/privacy"


def text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def patch_section(payload: str, path: str) -> str:
    match = re.search(
        rf"^diff --git a/{re.escape(path)} b/{re.escape(path)}\n"
        r".*?(?=^diff --git |\Z)",
        payload,
        re.MULTILINE | re.DOTALL,
    )
    return "" if match is None else match.group(0)


class LocationBarBubbleToggleContractTest(unittest.TestCase):
    def test_reusable_button_keeps_required_views_metadata(self):
        header = text(DEVELOPER_UI / "location_bar_bubble_button.h")
        implementation = text(DEVELOPER_UI / "location_bar_bubble_button.cc")

        self.assertIn(
            "METADATA_HEADER(LocationBarBubbleButton, views::ImageButton)",
            header,
        )
        self.assertIn("metadata_impl_macros.h", implementation)
        self.assertIn("BEGIN_METADATA(LocationBarBubbleButton)", implementation)
        self.assertIn("END_METADATA", implementation)

    def test_reusable_button_suppresses_only_the_matching_mouse_release(self):
        implementation = text(DEVELOPER_UI / "location_bar_bubble_button.cc")
        unit_tests = text(
            DEVELOPER_UI / "location_bar_bubble_button_unittest.cc"
        )

        self.assertIn(
            "class LocationBarBubbleButtonTest : public views::ViewsTestBase",
            unit_tests,
        )
        self.assertIn("widget->SetContentsView(std::move(button))", unit_tests)
        self.assertIn(
            "suppress_button_release_ = IsSurfaceShowing();", implementation
        )
        self.assertRegex(
            implementation,
            r"if \(event\.IsMouseEvent\(\)\) \{\s*"
            r"return !IsSurfaceShowing\(\) && !suppress_button_release_;",
        )
        self.assertIn("return true;", implementation)
        for scenario in (
            "ExistingSurfaceDismissalDoesNotReopenOnMouseRelease",
            "ClosedSurfaceOpensOnMouseRelease",
            "NextMousePressClearsAnEarlierReleaseSuppression",
            "KeyboardAndTouchRemainTriggerableWhileSurfaceIsShowing",
        ):
            self.assertIn(scenario, unit_tests)

    def test_all_location_bar_bubble_actions_use_showing_callbacks(self):
        patch = text(PATCH)
        location_bar = patch_section(
            patch, "chrome/browser/ui/views/location_bar/location_bar_view.cc"
        )
        browser_view = patch_section(
            patch, "chrome/browser/ui/views/frame/browser_view.cc"
        )

        self.assertEqual(
            2,
            location_bar.count(
                "std::make_unique<ahoi::LocationBarBubbleButton>"
            ),
        )
        self.assertIn("IsAhoiPrivacyModeShowing", location_bar)
        self.assertIn(
            "IsAhoiDeveloperToolbarSurfaceShowing", location_bar
        )
        for surface in ("kToolkit", "kCookieManager", "kCacheClear"):
            self.assertIn(
                f"ahoi::DeveloperToolbarSurface::{surface}", location_bar
            )
        self.assertIn(
            "ahoi_developer_toolkit_controller_->IsSurfaceShowing(surface)",
            browser_view,
        )
        self.assertIn(
            "ahoi_privacy_mode_controller_->IsShowing()", browser_view
        )

    def test_controllers_expose_state_and_all_surfaces_toggle(self):
        developer_header = text(
            DEVELOPER_UI / "developer_toolkit_controller.h"
        )
        developer_source = text(
            DEVELOPER_UI / "developer_toolkit_controller.cc"
        )
        privacy_header = text(PRIVACY_UI / "privacy_mode_controller.h")

        self.assertIn(
            "bool IsSurfaceShowing(DeveloperToolbarSurface surface) const;",
            developer_header,
        )
        for widget in (
            "bubble_widget_",
            "cookie_manager_widget_",
            "cache_status_widget_",
        ):
            self.assertIn(f"return {widget} != nullptr;", developer_source)
        cache_toggle = developer_source[developer_source.index(
            "bool DeveloperToolkitController::ShowCacheClear"
        ) :]
        cache_toggle = cache_toggle[: cache_toggle.index(
            "const bool start_new_request"
        )]
        self.assertIn("cache_status_widget_->Close();", cache_toggle)
        self.assertNotIn("cache_status_widget_->Activate();", cache_toggle)
        self.assertIn("bool IsShowing() const;", privacy_header)


if __name__ == "__main__":
    unittest.main()
