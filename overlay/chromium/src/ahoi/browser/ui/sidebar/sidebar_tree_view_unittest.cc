// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

TEST_F(SidebarTreeViewTest, EmptyRootKeepsAVisibleSavedDropViewport) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->ActivateWorkspace(workspace.id) ==
              tab_tree::TabTreeStore::Result::kOk);
  EXPECT_TRUE(controller_->view_model().rows().empty());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, view->GetPreferredSize().height());
  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 0, 240, SidebarTreeRowView::kRowHeight));
  EXPECT_EQ(0U, view->materialized_row_count_for_testing());
}

TEST_F(SidebarTreeViewTest, TransientEmptyViewportPreservesMaterializedRows) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  std::vector<tab_tree::TreeNode> pages;
  for (int index = 0; index < 8; ++index) {
    pages.push_back(MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"Saved page",
                             std::to_string(index)));
  }

  auto tree = NewTreeView();
  SidebarTreeView* const tree_ptr = tree.get();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt,
                                                        std::move(pages)));
  tree->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  const size_t materialized_before_collapse =
      tree->materialized_row_count_for_testing();
  ASSERT_GT(materialized_before_collapse, 0U);

  auto scroll = SidebarTreeView::CreateScrollView(std::move(tree));
  views::ScrollView* const scroll_ptr = scroll.get();
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  views::View* const viewport =
      widget->SetContentsView(std::make_unique<views::View>());
  viewport->AddChildView(std::move(scroll));
  widget->SetBounds(gfx::Rect(0, 0, 240, 96));
  scroll_ptr->SetBoundsRect(gfx::Rect(0, 0, 240, 96));
  scroll_ptr->DeprecatedLayoutImmediately();
  widget->Show();

  // The collapse synchronously walks ScrollView's visible-bounds subscribers.
  // A zero effective viewport is presentation state, not an empty model; keep
  // the already bounded rows instead of removing their registered Textfields
  // during that traversal.
  scroll_ptr->SetBoundsRect(gfx::Rect());
  task_environment()->RunUntilIdle();
  EXPECT_EQ(materialized_before_collapse,
            tree_ptr->materialized_row_count_for_testing());

  // The production presentation host starts the reveal while the effective
  // viewport is still empty. The immediate visibility task must not discard
  // the previous bounded materialization before animated layout settles.
  scroll_ptr->SetVisible(false);
  task_environment()->RunUntilIdle();
  scroll_ptr->SetVisible(true);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(materialized_before_collapse,
            tree_ptr->materialized_row_count_for_testing());

  // Stable bounds are covered by the production-host browser regression. This
  // focused unit verifies the safety property that the transient empty frame
  // itself cannot recycle registered row/Textfield descendants.
}

TEST_F(SidebarTreeViewTest, DefersWidthToResizableSidebarViewport) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"A title that must elide inside the current sidebar", "a");
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {page}));

  EXPECT_EQ(0, view->GetPreferredSize().width());
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 188, 64));
  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, row);
  EXPECT_EQ(188, row->width());
}

TEST_F(SidebarTreeViewTest,
       RuntimeCompositeSuppressesOnlySavedPresentationProxy) {
  gfx::ScopedAnimationDurationScaleMode disable_animations(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  const tab_tree::TreeNode mixed_saved =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Mixed saved", "b");
  const tab_tree::TreeNode third =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Third", "c");
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(
      std::nullopt, {first, mixed_saved, third}));
  ASSERT_TRUE(controller_->SelectNode(mixed_saved.id));

  view->SetRuntimeCompositeSuppressedNodes({mixed_saved.id});
  EXPECT_FALSE(controller_->view_model().selected_node_id().has_value());
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 128));
  EXPECT_EQ(3U, controller_->view_model().rows().size());
  EXPECT_NE(nullptr, view->GetMaterializedRowForTesting(first.id));
  EXPECT_EQ(nullptr, view->GetMaterializedRowForTesting(mixed_saved.id));
  SidebarTreeRowView* third_row = view->GetMaterializedRowForTesting(third.id);
  ASSERT_NE(nullptr, third_row);
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, third_row->y());
  EXPECT_EQ(2 * SidebarTreeRowView::kRowHeight,
            view->GetPreferredSize().height());

  view->SetRuntimeCompositeSuppressedNodes({});
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 128));
  EXPECT_NE(nullptr, view->GetMaterializedRowForTesting(mixed_saved.id));
  EXPECT_EQ(3 * SidebarTreeRowView::kRowHeight,
            view->GetPreferredSize().height());
}

TEST_F(SidebarTreeViewTest, ExposesLocalizedTabStatusOnSavedPageRows) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Video call", "a");
  delegate_.saved_page_status_text = u"Microphone recording";
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {page}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, row);
  ui::AXNodeData data;
  row->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(u"Video call — Microphone recording",
            data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_EQ(u"Microphone recording", row->GetTooltipText());
}

TEST_F(SidebarTreeViewTest, MaterializesOnlyViewportRowsForTenThousandNodes) {
  constexpr size_t kNodeCount = 10000;
  tab_tree::Workspace workspace = MakeWorkspace();
  std::vector<tab_tree::TreeNode> nodes;
  nodes.reserve(kNodeCount);
  for (size_t index = 0; index < kNodeCount; ++index) {
    nodes.push_back(MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"Page",
                             std::to_string(index)));
  }

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt,
                                                        std::move(nodes)));

  constexpr size_t kFirstVisible = 5000;
  view->SynchronizeRowsForTesting(gfx::Rect(
      0, static_cast<int>(kFirstVisible * SidebarTreeRowView::kRowHeight), 240,
      10 * SidebarTreeRowView::kRowHeight));
  EXPECT_LE(view->materialized_row_count_for_testing(),
            10U + 2U * SidebarTreeView::kOverscanRows);
  EXPECT_NE(nullptr,
            view->GetMaterializedRowForTesting(
                controller_->view_model().rows()[kFirstVisible].node_id));
  size_t previous_row_index = 0;
  bool first_materialized_row = true;
  for (views::View* child_view : view->children()) {
    auto* row = views::AsViewClass<SidebarTreeRowView>(child_view);
    if (!row) {
      // FocusRing and other framework-owned decoration views are not part of
      // the virtualized row budget.
      continue;
    }
    if (!first_materialized_row) {
      EXPECT_LT(previous_row_index, row->row_index());
    }
    first_materialized_row = false;
    previous_row_index = row->row_index();
  }

  const base::Uuid editing_id =
      controller_->view_model().rows()[kFirstVisible].node_id;
  ASSERT_TRUE(controller_->SelectNode(editing_id));
  view->BeginRenameSelectedNode();

  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 9000 * SidebarTreeRowView::kRowHeight, 240,
                10 * SidebarTreeRowView::kRowHeight));
  EXPECT_LE(view->materialized_row_count_for_testing(),
            11U + 2U * SidebarTreeView::kOverscanRows);
  ASSERT_NE(nullptr, view->GetMaterializedRowForTesting(editing_id));
  EXPECT_TRUE(view->GetMaterializedRowForTesting(editing_id)->is_editing());

  view->CancelRename();
  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 9000 * SidebarTreeRowView::kRowHeight, 240,
                10 * SidebarTreeRowView::kRowHeight));
  EXPECT_EQ(nullptr, view->GetMaterializedRowForTesting(editing_id));

  view->SynchronizeRowsForTesting(gfx::Rect(
      0, static_cast<int>(kFirstVisible * SidebarTreeRowView::kRowHeight), 240,
      10 * SidebarTreeRowView::kRowHeight));
  const base::Uuid dragged_id =
      controller_->view_model().rows()[kFirstVisible].node_id;
  SidebarTreeRowView* dragged_row =
      view->GetMaterializedRowForTesting(dragged_id);
  ASSERT_NE(nullptr, dragged_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(dragged_row, gfx::Point(24, 16), &drag_data);

  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 9000 * SidebarTreeRowView::kRowHeight, 240,
                10 * SidebarTreeRowView::kRowHeight));
  EXPECT_EQ(dragged_row, view->GetMaterializedRowForTesting(dragged_id));

  dragged_row->OnDragDone();
  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 9000 * SidebarTreeRowView::kRowHeight, 240,
                10 * SidebarTreeRowView::kRowHeight));
  EXPECT_EQ(nullptr, view->GetMaterializedRowForTesting(dragged_id));
}

TEST_F(SidebarTreeViewTest, RecycledDragRowRestoresItsTitleVisibility) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Split title", "a");
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {page}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(row);
  row->SetIsDragging(true);
  EXPECT_FALSE(row->title_visible_for_testing());

  row->Unbind();
  row->Bind(0, controller_->view_model().rows().front(), page,
            /*selected=*/false);
  EXPECT_TRUE(row->title_visible_for_testing());
  EXPECT_EQ(row->title(), u"Split title");
}

TEST_F(SidebarTreeViewTest, RebindingUnchangedRowDoesNotInvalidateLayout) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Stable title", "stable");
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {page}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(row);
  row->DeprecatedLayoutImmediately();
  ASSERT_FALSE(row->needs_layout());

  row->Bind(0, controller_->view_model().rows().front(), page,
            /*selected=*/false);
  EXPECT_FALSE(row->needs_layout());
}

TEST_F(SidebarTreeViewTest,
       ImportedFolderMetadataUsesClosedFolderVisualWithoutDisclosure) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Tooling", "a");
  folder.icon = u"star";
  folder.accent_argb = 0xFF4F8DE8u;

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(
      controller_->view_model().ReplaceChildren(std::nullopt, {folder}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, row);
  EXPECT_TRUE(row->is_folder());
  EXPECT_FALSE(row->disclosure_visible_for_testing());
  EXPECT_FALSE(row->uses_open_folder_icon_for_testing());
}

TEST_F(SidebarTreeViewTest, SplitTabsShareOneSegmentedVisualRow) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  tab_tree::TreeNode second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "b");
  tab_tree::TreeNode third =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Third", "c");

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(
      std::nullopt, {first, second, third}));

  delegate_.split_groups = {{first.id, second.id}};
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* first_row = view->GetMaterializedRowForTesting(first.id);
  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  SidebarTreeRowView* third_row = view->GetMaterializedRowForTesting(third.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  ASSERT_NE(nullptr, third_row);
  EXPECT_TRUE(first_row->is_split_segment_for_testing());
  EXPECT_TRUE(second_row->is_split_segment_for_testing());
  EXPECT_EQ(first_row->y(), second_row->y());
  EXPECT_LT(first_row->x(), second_row->x());
  first_row->DeprecatedLayoutImmediately();
  second_row->DeprecatedLayoutImmediately();
  // These are the final child-label paint bounds, not just the preferred title
  // geometry. Both labels hard-stop before their pane's trailing/bottom split
  // separators and apply a matching local canvas clip.
  for (SidebarTreeRowView* split_row : {first_row, second_row}) {
    const gfx::Rect paint_bounds = split_row->title_paint_bounds_for_testing();
    EXPECT_LT(paint_bounds.right(), split_row->width());
    EXPECT_LT(paint_bounds.bottom(), split_row->height());
    EXPECT_EQ(gfx::Rect(gfx::Point(), paint_bounds.size()),
              split_row->title_paint_clip_bounds_for_testing());
  }
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, third_row->y());
  EXPECT_EQ(2 * SidebarTreeRowView::kRowHeight,
            view->GetPreferredSize().height());

  delegate_.split_groups = {{first.id, second.id, third.id}};
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  first_row = view->GetMaterializedRowForTesting(first.id);
  second_row = view->GetMaterializedRowForTesting(second.id);
  third_row = view->GetMaterializedRowForTesting(third.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  ASSERT_NE(nullptr, third_row);
  EXPECT_EQ(first_row->y(), second_row->y());
  EXPECT_EQ(second_row->y(), third_row->y());
  EXPECT_LT(first_row->x(), second_row->x());
  EXPECT_LT(second_row->x(), third_row->x());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, view->GetPreferredSize().height());
}

TEST_F(SidebarTreeViewTest, SavedSplitVisualDataControlsSegmentBounds) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  tab_tree::TreeNode second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "b");

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(
      controller_->view_model().ReplaceChildren(std::nullopt, {first, second}));
  delegate_.split_groups = {{first.id, second.id}};
  delegate_.split_visual_data = split_tabs::SplitTabVisualData(
      split_tabs::SplitTabLayout::kSideBySide, 0.75);
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* first_row = view->GetMaterializedRowForTesting(first.id);
  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  EXPECT_GT(first_row->width(), second_row->width());
  EXPECT_EQ(first_row->y(), second_row->y());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, view->GetPreferredSize().height());
}

TEST_F(SidebarTreeViewTest,
       ThreeAndFourPaneSavedSplitsReserveReadableMultilineHeight) {
  gfx::ScopedAnimationDurationScaleMode disable_animations(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  tab_tree::TreeNode second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "b");
  tab_tree::TreeNode third =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Third", "c");
  tab_tree::TreeNode fourth =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Fourth", "d");

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(
      std::nullopt, {first, second, third, fourth}));

  delegate_.split_groups = {{first.id, second.id, third.id}};
  delegate_.split_visual_data = split_tabs::SplitTabVisualData::ForThreePane(
      split_tabs::SplitTabLayout::kSideBySide,
      split_tabs::SplitTabArrangement::kMainStart);
  const int three_pane_height = GetSplitRowPreferredHeight(
      3, *delegate_.split_visual_data, SidebarTreeRowView::kRowHeight);
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(
      gfx::Rect(0, 0, 240, three_pane_height + SidebarTreeRowView::kRowHeight));

  SidebarTreeRowView* first_row = view->GetMaterializedRowForTesting(first.id);
  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  SidebarTreeRowView* third_row = view->GetMaterializedRowForTesting(third.id);
  SidebarTreeRowView* fourth_row =
      view->GetMaterializedRowForTesting(fourth.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  ASSERT_NE(nullptr, third_row);
  ASSERT_NE(nullptr, fourth_row);
  EXPECT_EQ(three_pane_height + SidebarTreeRowView::kRowHeight,
            view->GetPreferredSize().height());
  EXPECT_EQ(three_pane_height, first_row->height());
  EXPECT_LT(second_row->y(), third_row->y());
  EXPECT_GE(second_row->height(), 24);
  EXPECT_GE(third_row->height(), 24);
  EXPECT_EQ(three_pane_height, fourth_row->y());

  delegate_.split_groups = {{first.id, second.id, third.id, fourth.id}};
  delegate_.split_visual_data = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kSideBySide);
  const int four_pane_height = GetSplitRowPreferredHeight(
      4, *delegate_.split_visual_data, SidebarTreeRowView::kRowHeight);
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, four_pane_height));

  first_row = view->GetMaterializedRowForTesting(first.id);
  second_row = view->GetMaterializedRowForTesting(second.id);
  third_row = view->GetMaterializedRowForTesting(third.id);
  fourth_row = view->GetMaterializedRowForTesting(fourth.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  ASSERT_NE(nullptr, third_row);
  ASSERT_NE(nullptr, fourth_row);
  EXPECT_EQ(four_pane_height, view->GetPreferredSize().height());
  EXPECT_EQ(first_row->y(), second_row->y());
  EXPECT_EQ(third_row->y(), fourth_row->y());
  EXPECT_LT(first_row->y(), third_row->y());
  EXPECT_GE(first_row->height(), 24);
  EXPECT_GE(fourth_row->height(), 24);
}

TEST_F(SidebarTreeViewTest, CrossGroupSplitStaysAtDeepestVisibleParent) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  tab_tree::TreeNode nested = MakeNode(
      workspace, folder.id, tab_tree::TreeNodeType::kSavedPage, u"Nested", "a");
  tab_tree::TreeNode root =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Root", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(nested));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(root));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ExpandNode(folder.id));
  delegate_.split_groups = {{root.id, nested.id}};

  auto view = NewTreeView();
  view->OnSplitGroupsChanged();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* nested_row =
      view->GetMaterializedRowForTesting(nested.id);
  SidebarTreeRowView* root_row = view->GetMaterializedRowForTesting(root.id);
  ASSERT_NE(nullptr, nested_row);
  ASSERT_NE(nullptr, root_row);
  EXPECT_EQ(nested_row->y(), root_row->y());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, nested_row->y());
  EXPECT_GT(nested_row->x(), 0);
  EXPECT_GT(root_row->x(), 0);
}

TEST_F(SidebarTreeViewTest,
       SearchKeepsCrossFolderSplitPartnersInTheirRealHierarchy) {
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  ASSERT_TRUE(render_mode);
  const tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  const tab_tree::TreeNode first_folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"First folder", "a");
  const tab_tree::TreeNode second_folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Second folder", "b");
  const tab_tree::TreeNode first =
      MakeNode(workspace, first_folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Matching page", "a");
  const tab_tree::TreeNode second =
      MakeNode(workspace, second_folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Split partner", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(first_folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(second_folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(second));

  auto view = NewTreeView();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.split_groups = {{first.id, second.id}};
  view->OnSplitGroupsChanged();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->SetSearchMatches({first.id}));
  ASSERT_EQ(4U, controller_->view_model().rows().size());
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 160));

  SidebarTreeRowView* first_row = view->GetMaterializedRowForTesting(first.id);
  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  ASSERT_NE(nullptr, first_row);
  ASSERT_NE(nullptr, second_row);
  EXPECT_FALSE(first_row->is_split_segment_for_testing());
  EXPECT_FALSE(second_row->is_split_segment_for_testing());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, first_row->y());
  EXPECT_EQ(3 * SidebarTreeRowView::kRowHeight, second_row->y());
  EXPECT_EQ(4 * SidebarTreeRowView::kRowHeight,
            view->GetPreferredSize().height());
}

TEST_F(SidebarTreeViewTest,
       SearchFolderRowsExposeNavigationWithoutDisclosureState) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  const tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const tab_tree::TreeNode child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Matching page", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(child));

  auto view = NewTreeView();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->SetSearchMatches({child.id}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  SidebarTreeRowView* folder_row =
      view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, folder_row);
  EXPECT_FALSE(folder_row->disclosure_visible_for_testing());
  ui::AXNodeData folder_data;
  folder_row->GetViewAccessibility().GetAccessibleNodeData(&folder_data);
  EXPECT_FALSE(folder_data.HasState(ax::mojom::State::kExpanded));
  EXPECT_FALSE(folder_data.HasState(ax::mojom::State::kCollapsed));
  EXPECT_EQ(ax::mojom::DefaultActionVerb::kOpen,
            folder_data.GetDefaultActionVerb());

  ASSERT_TRUE(controller_->SelectNode(child.id));
  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_LEFT, ui::EF_NONE)));
  EXPECT_EQ(folder.id, controller_->view_model().selected_node_id());
  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RIGHT, ui::EF_NONE)));
  EXPECT_EQ(child.id, controller_->view_model().selected_node_id());
}

TEST_F(SidebarTreeViewTest,
       RightArrowSelectsFirstVisibleChildPastRuntimeProxy) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const tab_tree::TreeNode suppressed_child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Running split pane", "a");
  const tab_tree::TreeNode visible_child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Visible saved page", "b");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, {suppressed_child, visible_child}));
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  ASSERT_TRUE(controller_->SelectNode(folder.id));
  view->SetRuntimeCompositeSuppressedNodes({suppressed_child.id});

  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RIGHT, ui::EF_NONE)));
  ASSERT_TRUE(model.selected_node_id().has_value());
  EXPECT_EQ(visible_child.id, *model.selected_node_id());
}

TEST_F(SidebarTreeViewTest,
       KeyboardSelectionAndAccessibilityExposeTreeContract) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Docs", "b");
  tab_tree::TreeNode child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Issue tracker", "a");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, page}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, {child}));
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  ASSERT_TRUE(controller_->SelectNode(folder.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 128));

  SidebarTreeRowView* folder_row =
      view->GetMaterializedRowForTesting(folder.id);
  SidebarTreeRowView* child_row = view->GetMaterializedRowForTesting(child.id);
  SidebarTreeRowView* page_row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, folder_row);
  ASSERT_NE(nullptr, child_row);
  ASSERT_NE(nullptr, page_row);

  ui::AXNodeData tree_data;
  view->GetViewAccessibility().GetAccessibleNodeData(&tree_data);
  EXPECT_EQ(ax::mojom::Role::kTree, tree_data.role);
  EXPECT_TRUE(tree_data.HasState(ax::mojom::State::kVertical));
  EXPECT_EQ(ax::mojom::Restriction::kReadOnly, tree_data.GetRestriction());

  ui::AXNodeData folder_data;
  folder_row->GetViewAccessibility().GetAccessibleNodeData(&folder_data);
  EXPECT_EQ(ax::mojom::Role::kTreeItem, folder_data.role);
  EXPECT_EQ(u"Project", folder_data.GetString16Attribute(
                            ax::mojom::StringAttribute::kName));
  EXPECT_EQ(1, folder_data.GetIntAttribute(
                   ax::mojom::IntAttribute::kHierarchicalLevel));
  EXPECT_EQ(1, folder_data.GetIntAttribute(ax::mojom::IntAttribute::kPosInSet));
  EXPECT_EQ(2, folder_data.GetIntAttribute(ax::mojom::IntAttribute::kSetSize));
  EXPECT_TRUE(folder_data.HasState(ax::mojom::State::kExpanded));
  EXPECT_TRUE(
      folder_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_TRUE(folder_data.HasAction(ax::mojom::Action::kDoDefault));
  EXPECT_EQ(&folder_row->GetViewAccessibility(),
            view->GetViewAccessibility().GetActiveDescendantView());

  ui::AXNodeData child_data;
  child_row->GetViewAccessibility().GetAccessibleNodeData(&child_data);
  EXPECT_EQ(2, child_data.GetIntAttribute(
                   ax::mojom::IntAttribute::kHierarchicalLevel));
  EXPECT_EQ(1, child_data.GetIntAttribute(ax::mojom::IntAttribute::kPosInSet));
  EXPECT_EQ(1, child_data.GetIntAttribute(ax::mojom::IntAttribute::kSetSize));

  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE)));
  ASSERT_TRUE(model.selected_node_id().has_value());
  EXPECT_EQ(child.id, *model.selected_node_id());

  // Selection can re-materialize the virtual viewport. Accessibility clients
  // always resolve the current row, so mirror that contract instead of using
  // a recycled pointer captured before the selection change.
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 128));
  page_row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, page_row);
  ui::AXActionData focus_action;
  focus_action.action = ax::mojom::Action::kFocus;
  EXPECT_TRUE(page_row->HandleAccessibleAction(focus_action));
  ASSERT_TRUE(model.selected_node_id().has_value());
  EXPECT_EQ(page.id, *model.selected_node_id());

  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 128));
  child_row = view->GetMaterializedRowForTesting(child.id);
  ASSERT_NE(nullptr, child_row);
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  EXPECT_TRUE(child_row->HandleAccessibleAction(action));
  EXPECT_EQ(child.id, delegate_.activated_node);
}

TEST_F(SidebarTreeViewTest, SingleClickOnFolderRowCollapsesAndExpands) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  tab_tree::TreeNode child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Issue tracker", "a");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, {child}));
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  SidebarTreeRowView* folder_row =
      view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, folder_row);
  EXPECT_FALSE(folder_row->disclosure_visible_for_testing());
  EXPECT_TRUE(folder_row->uses_open_folder_icon_for_testing());
  const gfx::Point click_point(80, SidebarTreeRowView::kRowHeight / 2);
  ui::MouseEvent press(ui::EventType::kMousePressed, click_point, click_point,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, click_point,
                         click_point, base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);

  ASSERT_TRUE(folder_row->OnMousePressed(press));
  EXPECT_FALSE(model.selected_node_id().has_value());
  EXPECT_TRUE(view->CanStartDragForView(
      folder_row, click_point,
      gfx::Point(click_point.x(), click_point.y() + 12)));
  EXPECT_EQ(ui::DragDropTypes::DRAG_MOVE,
            view->GetDragOperationsForView(folder_row, click_point));
  folder_row->OnMouseReleased(release);
  EXPECT_FALSE(model.IsExpanded(folder.id));
  EXPECT_EQ(1U, model.rows().size());
  EXPECT_EQ(folder.id, model.selected_node_id());

  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  folder_row = view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, folder_row);
  EXPECT_FALSE(folder_row->uses_open_folder_icon_for_testing());
  ASSERT_TRUE(folder_row->OnMousePressed(press));
  folder_row->OnMouseReleased(release);
  EXPECT_TRUE(model.IsExpanded(folder.id));
  EXPECT_EQ(2U, model.rows().size());

  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  folder_row = view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, folder_row);
  EXPECT_TRUE(folder_row->uses_open_folder_icon_for_testing());
}

}  // namespace

}  // namespace ahoi::sidebar
