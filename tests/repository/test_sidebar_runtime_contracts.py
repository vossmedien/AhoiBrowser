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
        delegate = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_delegate.h"
        )
        host = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_tree_actions.cc"
        )
        drag_tests = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_drag_unittest.cc"
        )
        browser_test = text(
            OVERLAY / "ui/split_drop/split_layout_menu_browsertest.cc"
        )

        self.assertIn("RemoveSplit(split_id)", operation)
        self.assertIn("RestoreSplit(", operation)
        self.assertLess(
            operation.index("std::ranges::sort(remaining_indices)"),
            operation.index("RemoveSplit(split_id)"),
        )
        self.assertNotIn(
            "return false", operation[operation.index("RemoveSplit(split_id)") :]
        )
        for forbidden in ("Navigate", "WebContents", "->Close("):
            self.assertNotIn(forbidden, operation)
        self.assertIn("CanExtractSavedSplitPaneForDrop", tree_drag)
        self.assertIn(
            "std::vector<base::Uuid> move_group{indicator.source_node_id}",
            tree_drag,
        )
        self.assertIn("ExtractSavedSplitPaneAfterDrop", tree_drag)
        self.assertIn("virtual bool ExtractSavedSplitPaneAfterDrop", delegate)
        extraction = function(
            host,
            "bool BrowserSidebarHostView::ExtractSavedSplitPaneAfterDrop",
            "bool BrowserSidebarHostView::CanSaveTemporaryTab",
        )
        self.assertNotIn("CHECK", extraction)
        self.assertIn("return false", extraction)
        self.assertRegex(
            tree_drag,
            r"CanExtractSavedSplitPaneForDrop\(indicator\.source_node_id,[\s\S]*?"
            r"std::nullopt\)[\s\S]*?ExtractSavedSplitPaneAfterDrop",
        )
        self.assertIn(
            "SavedSplitExtractionCallbackFailsClosedWhenStateChanges",
            drag_tests,
        )
        for test_name in (
            "DragExtractionKeepsFourPaneRemainderAsThreePaneSplit",
            "DragExtractionKeepsThreePaneRemainderAsTwoPaneSplit",
            "DragExtractionDissolvesTwoPaneSplit",
            "DragExtractionRejectsUnsplitSourceWithoutMutation",
        ):
            self.assertIn(test_name, browser_test)

    def test_sidebar_density_sync_disclosure_and_pane_outline_are_centralized(self):
        style = text(OVERLAY / "ui/visual_style.h")
        actions = text(OVERLAY / "ui/sidebar/sidebar_action_views.cc")
        tree_row = text(OVERLAY / "ui/sidebar/sidebar_tree_row_view.h")
        runtime_rows = text(OVERLAY / "ui/sidebar/sidebar_runtime_tab_views.cc")
        temporary_row = function(
            runtime_rows,
            "class OpenTabRowView final",
            "BEGIN_METADATA(OpenTabRowView)",
        )
        split_row = function(
            runtime_rows,
            "class OpenTabSplitRowView final",
            "BEGIN_METADATA(OpenTabSplitRowView)",
        )
        remote_rows = text(OVERLAY / "ui/sidebar/sidebar_remote_tab_views.cc")
        host_core = text(OVERLAY / "ui/sidebar/browser_sidebar_host_core.cc")
        device_tabs = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_device_tabs.cc"
        )
        tree_view = text(OVERLAY / "ui/sidebar/sidebar_tree_view.cc")
        sync_controls = text(OVERLAY / "ui/sidebar/sidebar_sync_controls.cc")
        patch = text(PATCH)
        outline = patch_section(
            patch, "chrome/browser/ui/views/frame/contents_container_outline.h"
        ) + patch_section(
            patch, "chrome/browser/ui/views/frame/contents_container_outline.cc"
        )

        self.assertIn("kSidebarTabRowHeight = 40", style)
        self.assertNotIn("kTreeRowHeight", style)
        self.assertIn(
            "kRowHeight = visual_style::kSidebarTabRowHeight", tree_row
        )
        self.assertIn(
            "SetPreferredSize(gfx::Size(0, SidebarTreeRowView::kRowHeight))",
            temporary_row,
        )
        self.assertIn("SidebarTreeRowView::kRowHeight)));", split_row)
        self.assertIn(
            "SetPreferredSize(gfx::Size(0, SidebarTreeRowView::kRowHeight))",
            remote_rows,
        )
        self.assertIn("kSidebarSectionDividerHeight = 28", style)
        self.assertIn("CreateSidebarSectionDivider(", host_core)
        self.assertNotIn("gfx::Insets::VH(7, 0)", host_core)
        self.assertIn("show_remote_tabs = row_count > 0u", device_tabs)
        self.assertIn(
            "remote_tabs_header_->SetVisible(show_remote_tabs)", device_tabs
        )
        self.assertIn(
            "remote_tabs_container_->SetVisible(show_remote_tabs)",
            device_tabs,
        )
        self.assertNotIn(
            "remote_tabs_container_->SetVisible(profile_sync_service_ != nullptr)",
            device_tabs,
        )
        self.assertIn(
            "std::max(visual_height, SidebarTreeRowView::kRowHeight)",
            tree_view,
        )
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

    def test_sidebar_drag_targets_stay_visible_repaint_and_clear_without_fake_rows(self):
        style = text(OVERLAY / "ui/visual_style.h")
        tree_header = text(OVERLAY / "ui/sidebar/sidebar_tree_view.h")
        tree_view = text(OVERLAY / "ui/sidebar/sidebar_tree_view.cc")
        tree_drag = text(OVERLAY / "ui/sidebar/sidebar_tree_view_drag.cc")
        host = text(OVERLAY / "ui/sidebar/browser_sidebar_host_tree_actions.cc")
        runtime_targets = text(
            OVERLAY / "ui/sidebar/sidebar_runtime_drop_targets.cc"
        )
        host_core = text(OVERLAY / "ui/sidebar/browser_sidebar_host_core.cc")
        tree_tests = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_drag_unittest.cc"
        )
        runtime_tests = text(
            OVERLAY / "ui/sidebar/sidebar_runtime_drop_targets_unittest.cc"
        )

        self.assertIn("kSidebarDropTargetInset", style)
        self.assertIn("kSidebarDropTargetOutlineThickness", style)
        self.assertIn(
            "kSidebarDropTargetAcceptingOutlineThickness", style
        )
        self.assertIn("void SetDragTargetVisible(bool visible)", tree_header)
        self.assertIn("bool drag_target_visible_ = false", tree_header)
        self.assertIn("bool drag_target_accepting_ = false", tree_header)
        self.assertIn("drag_target_visible_ || empty_root_accepting", tree_view)
        self.assertIn(
            "std::max(visual_height, SidebarTreeRowView::kRowHeight)",
            tree_view,
        )
        set_indicator = function(
            tree_drag,
            "void SidebarTreeView::SetDropIndicator",
            "void SidebarTreeView::UpdateFolderAutoExpand",
        )
        self.assertIn(
            "drag_target_accepting_ = drop_indicator_.has_value()",
            set_indicator,
        )
        self.assertIn("SchedulePaint()", set_indicator)

        saved_drag = function(
            host,
            "void BrowserSidebarHostView::OnSidebarDragStateChanged",
            "void BrowserSidebarHostView::OnTemporaryTabDragStateChanged",
        )
        runtime_drag = function(
            host,
            "void BrowserSidebarHostView::OnTemporaryTabDragStateChanged",
            "void BrowserSidebarHostView::UpdateNewGroupDropTargetVisibility",
        )
        reset_drag = function(
            host,
            "void BrowserSidebarHostView::ResetDragPresentation",
            "}  // namespace ahoi::sidebar",
        )
        self.assertIn("tree_view_->SetDragTargetVisible", saved_drag)
        self.assertIn("tree_view_->SetDragTargetVisible", runtime_drag)
        self.assertIn("tree_view_->SetDragTargetVisible(false)", reset_drag)

        self.assertNotIn("workspace_drop_host", host_core)
        self.assertNotIn("views::FillLayout", host_core)
        workspace_button = host_core.index(
            "workspace_header->AddChildView(CreateWorkspaceSelectorButton"
        )
        workspace_header = host_core.index(
            "AddChildView(std::move(workspace_header))"
        )
        new_group = host_core.index(
            "new_group_drop_target_ = AddChildView(CreateNewGroupDropTargetView"
        )
        tabs_surface = host_core.index(
            "auto tabs_surface = CreateSidebarTabsSurfaceView()"
        )
        self.assertLess(workspace_button, workspace_header)
        self.assertLess(workspace_header, new_group)
        self.assertLess(new_group, tabs_surface)

        open_target = function(
            runtime_targets,
            "class OpenTabsDropTargetView final",
            "BEGIN_METADATA(OpenTabsDropTargetView)",
        )
        self.assertIn("accepting_saved_tab_", open_target)
        self.assertIn("highlighted_", open_target)
        self.assertIn("visual_style::kHoverSurface", open_target)
        self.assertIn("visual_style::kDropTargetSurface", open_target)
        self.assertNotIn("TreeNode", open_target)
        new_group_target = function(
            runtime_targets,
            "class NewGroupDropTargetView final",
            "BEGIN_METADATA(NewGroupDropTargetView)",
        )
        self.assertNotIn(
            "SetBoundsRect(parent()->GetLocalBounds())", new_group_target
        )
        self.assertIn("PreferredSizeChanged()", new_group_target)
        self.assertIn("parent()->InvalidateLayout()", new_group_target)
        self.assertIn(
            "parent()->DeprecatedLayoutImmediately()", new_group_target
        )
        self.assertIn(
            "EmptySavedTreeExposesAndClearsDragTargetPresentation",
            tree_tests,
        )
        self.assertIn(
            "OpenTabsTargetIsVisibleBeforeHoverAndClearsWithoutAffectingNewGroup",
            runtime_tests,
        )
        self.assertIn(
            "NewGroupGetsOwnStableRowBeforeNativeDragLoop", runtime_tests
        )
        self.assertIn(
            "workspace_ptr->bounds().Intersects(target_ptr->bounds())",
            runtime_tests,
        )

    def test_mixed_splits_have_one_composite_row_without_model_mutation(self):
        presentation = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_presentation.cc"
        )
        runtime_rows = text(
            OVERLAY / "ui/sidebar/sidebar_runtime_tab_views.cc"
        )
        runtime_actions = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_runtime_actions.cc"
        )
        tree_header = text(OVERLAY / "ui/sidebar/sidebar_tree_view.h")
        tree_view = text(OVERLAY / "ui/sidebar/sidebar_tree_view.cc")
        tree_projection = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_projection.cc"
        )
        tree_tests = text(OVERLAY / "ui/sidebar/sidebar_tree_view_unittest.cc")
        tree_drag_tests = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_drag_unittest.cc"
        )
        runtime_tests = text(
            OVERLAY / "ui/sidebar/sidebar_runtime_drop_targets_unittest.cc"
        )

        refresh = function(
            presentation,
            "void BrowserSidebarHostView::RefreshRuntimePresentation",
            "ui::ImageModel BrowserSidebarHostView::GetFaviconForUrl",
        )
        self.assertIn("is_visible_mixed_split", refresh)
        self.assertIn("split_data->ListTabs()", refresh)
        self.assertIn("CreateOpenTabSplitRowView", refresh)
        self.assertIn("mixed_split_saved_nodes.insert", refresh)
        self.assertIn("SetRuntimeCompositeSuppressedNodes", refresh)
        self.assertNotIn("DeleteNode", refresh)
        self.assertNotIn("MakeSavedPageTemporary", refresh)
        self.assertIn("runtime_composite_suppressed_nodes_", tree_header)
        self.assertIn(
            "runtime_composite_suppressed_nodes_.contains", tree_projection
        )
        self.assertIn("WriteOpenTabDragPayload", runtime_rows)
        self.assertIn("extracting_from_same_split", runtime_actions)
        self.assertRegex(
            runtime_actions,
            r"source_node_id\.has_value\(\) && !extracting_from_same_split",
        )
        self.assertIn(
            "RuntimeCompositeSuppressesOnlySavedPresentationProxy", tree_tests
        )
        suppression = function(
            tree_view,
            "void SidebarTreeView::SetRuntimeCompositeSuppressedNodes",
            "void SidebarTreeView::OnPaintBackground",
        )
        self.assertLess(
            suppression.index("controller_->SelectNode(std::nullopt)"),
            suppression.index("runtime_composite_suppressed_nodes_ == node_ids"),
        )
        self.assertIn("selected_node_suppressed", tree_view)
        self.assertIn(
            "SuppressedRuntimeProxyRejectsStaleSelectionActions",
            tree_drag_tests,
        )
        self.assertIn(
            "CompositePaneDragKeepsSavedOrRuntimeIdentity", runtime_tests
        )


if __name__ == "__main__":
    unittest.main()
