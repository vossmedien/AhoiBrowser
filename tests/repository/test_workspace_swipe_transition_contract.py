import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
AHOI = ROOT / "overlay/chromium/src/ahoi/browser"
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"


def text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


class WorkspaceSwipeTransitionContractTest(unittest.TestCase):
    def test_transition_is_built_into_the_production_sidebar(self):
        build = text(AHOI / "ui/sidebar/BUILD.gn")

        self.assertIn('"workspace_transition_animator.cc"', build)
        self.assertIn('"workspace_transition_animator.h"', build)
        self.assertIn('"workspace_transition_animator_unittest.cc"', build)
        self.assertLess(
            build.index('source_set("sidebar_presentation")'),
            build.index('"workspace_transition_animator.cc"'),
        )
        self.assertLess(
            build.index('"workspace_transition_animator.cc"'),
            build.index('source_set("browser_sidebar")'),
        )

    def test_successful_relative_switch_animates_real_chrome_and_contents(self):
        host = text(
            AHOI
            / "ui/sidebar/browser_sidebar_host_workspace_transition.cc"
        )

        activation_start = host.index(
            "bool BrowserSidebarHostView::ActivateRelativeWorkspaceWithTransition"
        )
        activation_end = host.index(
            "void BrowserSidebarHostView::StartWorkspaceTransition"
        )
        activation = host[activation_start:activation_end]
        self.assertIn("CancelWorkspaceTransition();", activation)
        self.assertIn("ActivateRelativeWorkspaceForWindow", activation)
        self.assertIn("previous_workspace != activated_workspace", activation)
        self.assertIn("StartWorkspaceTransition(delta);", activation)
        self.assertLess(
            activation.index("ActivateRelativeWorkspaceForWindow"),
            activation.index("StartWorkspaceTransition(delta);"),
        )
        self.assertIn("GetBrowserView().contents_container()", host)
        self.assertIn("layer(), contents->layer()", host)

    def test_gesture_entry_point_preserves_truthful_activation_source(self):
        public_header = text(AHOI / "ui/sidebar/browser_sidebar_host.h")
        adapter = text(AHOI / "ui/sidebar/browser_sidebar_host.cc")
        host = text(
            AHOI
            / "ui/sidebar/browser_sidebar_host_workspace_transition.cc"
        )

        self.assertIn(
            "ActivateRelativeBrowserWorkspaceByGesture", public_header
        )
        self.assertIn(
            "host->ActivateRelativeWorkspaceByGesture(delta)", adapter
        )
        self.assertIn("WorkspaceActivationSource::kGesture", host)
        self.assertIn("WorkspaceActivationSource::kKeyboard", host)
        self.assertIn(
            "ActivateRelativeWorkspaceForWindow(browser_, delta,\n"
            "                                                          source)",
            host,
        )

    def test_browser_view_routes_gesture_and_keyboard_to_distinct_entries(self):
        patch = text(PATCH)

        self.assertEqual(
            1, patch.count("ActivateRelativeBrowserWorkspaceByGesture(")
        )
        self.assertEqual(
            1, patch.count("ActivateRelativeBrowserWorkspace(")
        )
        self.assertRegex(
            patch,
            re.compile(
                r"make_unique<ahoi::WorkspaceSwipeEventHandler>"
                r"[\s\S]{0,800}ActivateRelativeBrowserWorkspaceByGesture\(",
            ),
        )
        self.assertRegex(
            patch,
            re.compile(
                r"is_ahoi_relative_workspace_shortcut"
                r"[\s\S]{0,500}ActivateRelativeBrowserWorkspace\(",
            ),
        )

    def test_animation_is_interruptible_and_reduced_motion_finishes_stable(self):
        animator_header = text(
            AHOI / "ui/sidebar/workspace_transition_animator.h"
        )
        animator = text(
            AHOI / "ui/sidebar/workspace_transition_animator.cc"
        )
        animator_test = text(
            AHOI / "ui/sidebar/workspace_transition_animator_unittest.cc"
        )
        appearance = text(
            AHOI / "ui/sidebar/browser_sidebar_host_media.cc"
        )

        self.assertIn("Cancel();", animator)
        self.assertIn("AbortAllAnimations()", animator)
        self.assertIn("layer->SetOpacity(1.0f)", animator)
        self.assertIn("layer->SetTransform(gfx::Transform())", animator)
        self.assertIn("reduced_motion", animator)
        self.assertIn("!gfx::Animation::ShouldRenderRichAnimation()", animator)
        self.assertIn("base::WeakPtr<ui::Layer>", animator_header)
        self.assertNotIn("raw_ptr<ui::Layer>", animator_header)
        self.assertIn("sidebar_layer->AsWeakPtr()", animator)
        self.assertIn("contents_layer->AsWeakPtr()", animator)
        self.assertIn(
            "DestroyedSurfaceIsIgnoredWhenTransitionIsCancelled",
            animator_test,
        )
        self.assertIn("reduced_motion_ = policy.reduced_motion", appearance)
        self.assertIn("CancelWorkspaceTransition();", appearance)


if __name__ == "__main__":
    unittest.main()
