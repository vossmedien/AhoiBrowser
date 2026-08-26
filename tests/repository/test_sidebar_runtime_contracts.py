import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "overlay/chromium/src/ahoi/browser"
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"


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


def function(source: str, start: str, end: str) -> str:
    match = re.search(
        rf"{re.escape(start)}.*?(?=\n{re.escape(end)})",
        source,
        re.DOTALL,
    )
    return "" if match is None else match.group(0)


class SidebarRuntimeContractsTest(unittest.TestCase):
    def test_saved_new_tab_activation_uses_exact_navigation_identity_and_fails_closed(self):
        source = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_tree_actions.cc"
        )
        activation = function(
            source,
            "void BrowserSidebarHostView::ActivateSavedPage",
            "bool BrowserSidebarHostView::CanSplitSavedPages",
        )
        self.assertTrue(activation)
        self.assertIn("params.navigated_or_inserted_contents", activation)
        self.assertIn("BindTreeNodeToTab(node, opened_tab)", activation)
        self.assertIn("FindTabByTreeNodeId(node.id)", activation)
        self.assertIn("opened_tab != existing", activation)
        self.assertEqual(2, activation.count("opened_tab->Close()"))
        self.assertNotIn("tab_count_before", source)

        bridge_test = text(OVERLAY / "session/session_bridge_unittest.cc")
        self.assertIn(
            "ExplicitSavedNewTabBindingSurvivesDeferredGenericMatching",
            bridge_test,
        )

    def test_native_drag_presentation_only_finishes_at_completion_boundaries(self):
        patch = text(PATCH)
        browser_view = patch_section(
            patch, "chrome/browser/ui/views/frame/browser_view.cc"
        )
        controller = text(OVERLAY / "ui/split_drop/split_drop_controller.cc")
        controller_header = text(
            OVERLAY / "ui/split_drop/split_drop_controller.h"
        )

        self.assertRegex(
            browser_view,
            r"void BrowserView::OnDragExited\(\)[\s\S]*?OnTargetExited\(\);",
        )
        self.assertRegex(
            browser_view,
            r"void BrowserView::OnDragDone\(\)[\s\S]*?CompleteDrag\(\);",
        )
        self.assertNotIn("CancelDrag", browser_view)
        self.assertIn("void SplitDropController::OnTargetExited()", controller)
        self.assertIn("void SplitDropController::CompleteDrag()", controller)
        self.assertIn(
            "CancelBrowserSidebarSplitDropDrag(browser_sidebar_host)", controller
        )
        self.assertIn("Authoritative completion boundary", controller_header)

    def test_floating_sidebar_drag_routing_survives_chrome_z_order(self):
        patch = text(PATCH)
        browser_view = patch_section(
            patch, "chrome/browser/ui/views/frame/browser_view.cc"
        )
        vertical_region = patch_section(
            patch,
            "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc",
        )
        host_routing = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_drag_routing.cc"
        )
        host_appearance = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_media.cc"
        )
        browser_test = text(
            OVERLAY / "ui/shell/floating_browser_view_browsertest.cc"
        )

        self.assertIn("AhoiSidebarDragViewTargeterDelegate", browser_view)
        self.assertIn("top_container_->SetEventTargeter", browser_view)
        self.assertIn("IsAnyBrowserSidebarDragActive()", browser_view)
        self.assertIn("ConvertPointToScreen", browser_view)
        self.assertIn("SetPaintToLayer()", vertical_region)
        self.assertIn("SetFillsBoundsOpaquely(false)", vertical_region)
        self.assertIn("ActiveSidebarDragHosts", host_routing)
        self.assertIn("BrowserSidebarHostView::GetDropFormats", host_routing)
        self.assertIn("BrowserSidebarHostView::OnDragUpdated", host_routing)
        self.assertIn("return base::DoNothing()", host_routing)
        self.assertIn("layer()->SetFillsBoundsOpaquely(false)", host_appearance)
        self.assertIn(
            "AhoiFloatingSidebarOwnsItsNativeDragRoute", browser_test
        )
        self.assertIn("SidebarPresentationMode::kDocked", browser_test)
        self.assertIn("SidebarPresentationMode::kFloating", browser_test)
        self.assertIn("views::DropHelper", browser_test)

    def test_split_extraction_preserves_tab_and_web_contents_identity(self):
        operation = text(
            OVERLAY / "ui/sidebar/sidebar_split_tab_operations.cc"
        )
        tree_drag = text(OVERLAY / "ui/sidebar/sidebar_tree_view_drag.cc")
        browser_test = text(
            OVERLAY / "ui/split_drop/split_layout_menu_browsertest.cc"
        )

        self.assertIn("RemoveSplit(split_id)", operation)
        self.assertIn("RestoreSplit(", operation)
        for forbidden in ("Navigate", "WebContents", "->Close("):
            self.assertNotIn(forbidden, operation)
        self.assertIn("CanExtractSavedSplitPaneForDrop", tree_drag)
        self.assertIn(
            "std::vector<base::Uuid> move_group{indicator.source_node_id}",
            tree_drag,
        )
        self.assertIn("ExtractSavedSplitPaneAfterDrop", tree_drag)
        self.assertIn(
            "DragExtractionKeepsWebContentsAndRemainingSplit", browser_test
        )

    def test_sidebar_density_sync_disclosure_and_pane_outline_are_centralized(self):
        style = text(OVERLAY / "ui/visual_style.h")
        actions = text(OVERLAY / "ui/sidebar/sidebar_action_views.cc")
        sync_controls = text(OVERLAY / "ui/sidebar/sidebar_sync_controls.cc")
        patch = text(PATCH)
        outline = patch_section(
            patch, "chrome/browser/ui/views/frame/contents_container_outline.h"
        ) + patch_section(
            patch, "chrome/browser/ui/views/frame/contents_container_outline.cc"
        )

        self.assertIn("kTreeRowHeight = 36", style)
        self.assertIn("kSidebarHeaderActionSize = 32", style)
        self.assertIn(
            "kSplitPaneCornerRadius = kContentCardCornerRadius", style
        )
        self.assertIn("kSplitPaneInactiveOutlineThickness = 1", style)
        self.assertIn("kSplitPaneActiveOutlineThickness = 2", style)
        self.assertIn("kSplitPaneHighlightedOutlineThickness = 3", style)
        self.assertIn("kSplitPaneInactiveOutline = kDivider", style)
        self.assertIn("kSplitPaneActiveOutline = kAccent", style)
        self.assertIn("kSplitPaneHighlightedOutline = kFocusRing", style)
        self.assertIn(
            "preferred_height=*/visual_style::kSidebarHeaderActionSize",
            actions,
        )
        self.assertIn(
            "status_label_ = settings_body_->AddChildView", sync_controls
        )
        self.assertNotIn("status_label_ = AddChildView", sync_controls)
        self.assertIn("kSplitPaneCornerRadius", outline)
        self.assertIn("GetThickness(bool is_active, bool is_highlighted)", outline)


if __name__ == "__main__":
    unittest.main()
