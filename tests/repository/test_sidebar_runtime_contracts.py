import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "overlay/chromium/src/ahoi/browser"
PATCH = ROOT / "patches/chromium/0001-ahoi-m153-integration-seams.patch"


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
        # 2b0de42 moved the navigation/binding transaction into the shared
        # MaterializeSavedPage helper (browser_sidebar_host_page_actions.cc).
        self.assertIn(
            "MaterializeSavedPage(node, /*require_local_model=*/false).valid",
            activation,
        )
        page_actions = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_page_actions.cc"
        )
        materialize = function(
            page_actions,
            "BrowserSidebarSplitDropSource BrowserSidebarHostView::MaterializeSavedPage(",
            "}  // namespace ahoi::sidebar",
        )
        self.assertTrue(materialize)
        self.assertIn(
            "tabs::TabInterface::MaybeGetFromContents(\n"
            "          params.navigated_or_inserted_contents)",
            materialize,
        )
        self.assertIn(
            "BindTreeNodeToTab(\n                             node, weak_opened_tab.get())",
            materialize,
        )
        self.assertIn("FindTabByTreeNodeId(node.id)", materialize)
        # Only the duplicate created by this call is retired, never the winner.
        self.assertIn(
            "weak_opened_tab && weak_opened_tab.get() != weak_existing.get()",
            materialize,
        )
        # Binding failure without a winner fails closed by closing the tab.
        self.assertRegex(
            materialize,
            r"if \(weak_opened_tab\) \{\s*"
            r"base::OnceClosure rollback = make_rollback\(weak_opened_tab\);\s*"
            r"std::move\(rollback\)\.Run\(\);\s*\}\s*return \{\};\s*\}$",
        )
        self.assertNotIn("tab_count_before", source)
        self.assertNotIn("tab_count_before", page_actions)

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
        # 7265d73 split drop execution out of sidebar_tree_view_drag.cc.
        drop_execution = text(
            OVERLAY / "ui/sidebar/sidebar_tree_view_drop_execution.cc"
        )
        self.assertIn(
            "std::vector<base::Uuid> move_group{indicator.source_node_id}",
            drop_execution,
        )
        self.assertIn("ExtractSavedSplitPaneAfterDrop", drop_execution)
        self.assertIn("virtual bool ExtractSavedSplitPaneAfterDrop", delegate)
        extraction = function(
            host,
            "bool BrowserSidebarHostView::ExtractSavedSplitPaneAfterDrop",
            "bool BrowserSidebarHostView::CanSaveTemporaryTab",
        )
        self.assertNotIn("CHECK", extraction)
        self.assertIn("return false", extraction)
        self.assertRegex(
            drop_execution,
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
        # 7265d73 moved the split row into sidebar_runtime_split_views.cc.
        split_row = function(
            text(OVERLAY / "ui/sidebar/sidebar_runtime_split_views.cc"),
            "class OpenTabSplitRowView final",
            "BEGIN_METADATA(OpenTabSplitRowView)",
        )
        remote_rows = text(OVERLAY / "ui/sidebar/sidebar_remote_tab_views.cc")
        # 7265d73 moved sidebar construction into browser_sidebar_host_layout.cc.
        host_core = text(OVERLAY / "ui/sidebar/browser_sidebar_host_layout.cc")
        device_tabs = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_device_tabs.cc"
        )
        tree_view = text(OVERLAY / "ui/sidebar/sidebar_tree_view.cc")
        tree_header = text(OVERLAY / "ui/sidebar/sidebar_tree_view.h")
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
        # c510bfb replaced the empty-tree row-height minimum with a permanent
        # trailing root-append surface of one row height, so an empty
        # workspace stays a real drop target without a fake row.
        self.assertIn(
            "static constexpr int kRootAppendDropHeight = "
            "SidebarTreeRowView::kRowHeight;",
            tree_header,
        )
        self.assertIn(
            "height = base::saturated_cast<int>(static_cast<int64_t>(height) +\n"
            "                                     kRootAppendDropHeight);",
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
        # 30cd18f ("put sync controls in Ahoi Settings") deleted the sidebar
        # sync controls and their disclosure body; the sidebar no longer
        # owns a sync status label.
        self.assertFalse(
            (OVERLAY / "ui/sidebar/sidebar_sync_controls.cc").exists()
        )
        self.assertIn("kSplitPaneCornerRadius", outline)
        self.assertIn("GetThickness(bool is_active, bool is_highlighted)", outline)

    def test_sidebar_drag_targets_stay_visible_repaint_and_clear_without_fake_rows(self):
        style = text(OVERLAY / "ui/visual_style.h")
        tree_header = text(OVERLAY / "ui/sidebar/sidebar_tree_view.h")
        tree_view = text(OVERLAY / "ui/sidebar/sidebar_tree_view.cc")
        tree_drag = text(OVERLAY / "ui/sidebar/sidebar_tree_view_drag.cc")
        # 7265d73 split drag presentation and layout out of the host files.
        host = text(
            OVERLAY / "ui/sidebar/browser_sidebar_host_drag_presentation.cc"
        )
        runtime_targets = text(
            OVERLAY / "ui/sidebar/sidebar_runtime_drop_targets.cc"
        )
        host_core = text(OVERLAY / "ui/sidebar/browser_sidebar_host_layout.cc")
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
        # 0d21276 stopped painting the whole section while a drag is merely
        # visible (two competing highlights); c510bfb then scoped the broad
        # accepted surface to the trailing root-append area. Only a validated
        # root append (no target node) paints it.
        self.assertIn(
            "const bool root_accepting = drop_indicator_.has_value() &&\n"
            "                              !drop_indicator_->target_node_id.has_value() &&\n"
            "                              !append_target.IsEmpty();",
            tree_view,
        )
        self.assertIn(
            "gfx::RectF append_target(GetRootAppendDropBounds(visual_rows));",
            tree_view,
        )
        self.assertNotIn("if (drag_target_visible_", tree_view)
        # c510bfb replaced the empty-tree row-height minimum with a permanent
        # trailing root-append surface of one row height, so an empty
        # workspace stays a real drop target without a fake row.
        self.assertIn(
            "static constexpr int kRootAppendDropHeight = "
            "SidebarTreeRowView::kRowHeight;",
            tree_header,
        )
        self.assertIn(
            "height = base::saturated_cast<int>(static_cast<int64_t>(height) +\n"
            "                                     kRootAppendDropHeight);",
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

        self.assertIn("workspace_selector_host", host_core)
        self.assertIn("views::FillLayout", host_core)
        workspace_button = host_core.index(
            "workspace_selector_host->AddChildView("
        )
        new_group = host_core.index(
            "new_group_drop_target_ =\n      workspace_selector_host->AddChildView"
        )
        workspace_header = host_core.index(
            "workspace_header->AddChildView(std::move(workspace_selector_host))"
        )
        tabs_surface = host_core.index(
            "auto tabs_surface = CreateSidebarTabsSurfaceView()"
        )
        self.assertLess(workspace_button, new_group)
        self.assertLess(new_group, workspace_header)
        self.assertLess(workspace_header, tabs_surface)

        open_target = function(
            runtime_targets,
            "class OpenTabsDropTargetView final",
            "BEGIN_METADATA(OpenTabsDropTargetView)",
        )
        # d84d8e3 generalized accepting_saved_tab_ to accepting_tab_ (saved and
        # runtime payloads). 0d21276 deliberately dropped the pre-hover
        # kHoverSurface paint: the open-tabs target paints only while it is
        # the hovered, validated target.
        self.assertIn("accepting_tab_", open_target)
        self.assertIn("return accepting_tab_ && payload.has_value()", open_target)
        self.assertIn("highlighted_", open_target)
        self.assertRegex(
            open_target,
            r"SetBackground\(\s*highlighted_\s*\?\s*"
            r"views::CreateRoundedRectBackground\(\s*"
            r"visual_style::kDropTargetSurface,",
        )
        self.assertNotIn("visual_style::kHoverSurface", open_target)
        self.assertNotIn("TreeNode", open_target)
        new_group_target = function(
            runtime_targets,
            "class NewGroupDropTargetView final",
            "BEGIN_METADATA(NewGroupDropTargetView)",
        )
        self.assertNotIn(
            "SetBoundsRect(parent()->GetLocalBounds())", new_group_target
        )
        self.assertNotIn("PreferredSizeChanged()", new_group_target)
        self.assertNotIn("parent()->InvalidateLayout()", new_group_target)
        self.assertNotIn("DeprecatedLayoutImmediately()", new_group_target)
        self.assertIn(
            "EmptySavedTreeExposesAndClearsDragTargetPresentation",
            tree_tests,
        )
        self.assertIn(
            "OpenTabsTargetPaintsOnlyWhileHoveredAndDoesNotAffectNewGroup",
            runtime_tests,
        )
        self.assertIn(
            "NewGroupOverlaysWorkspaceWithoutChangingGeometry", runtime_tests
        )
        self.assertIn(
            "EXPECT_EQ(stable_workspace_bounds, target_ptr->bounds())",
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
