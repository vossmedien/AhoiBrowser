// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"

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

TEST_F(SidebarTreeViewTest, BindsCustomEmojiIconToFolderRow) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Tooling", "a");
  folder.icon = u"🛠️";
  folder.accent_argb = 0xFF4F8DE8u;

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(
      controller_->view_model().ReplaceChildren(std::nullopt, {folder}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, row);
  EXPECT_TRUE(row->is_folder());
  EXPECT_EQ(u"🛠️", row->folder_icon_for_testing());
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
  ASSERT_TRUE(folder_row->OnMousePressed(press));
  folder_row->OnMouseReleased(release);
  EXPECT_TRUE(model.IsExpanded(folder.id));
  EXPECT_EQ(2U, model.rows().size());
}

TEST_F(SidebarTreeViewTest, ExpandAnimatesExistingRowsAndCompletes) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);

  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const tab_tree::TreeNode child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Issue tracker", "a");
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Other page", "b");

  auto view = NewTreeView();
  SidebarTreeView* const tree_view = view.get();
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, page}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, {child}));
  tree_view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  ASSERT_EQ(SidebarTreeRowView::kRowHeight,
            tree_view->GetMaterializedRowForTesting(page.id)->y());

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree_view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* page_row =
      tree_view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, page_row);
  EXPECT_TRUE(tree_view->row_bounds_animation_running_for_testing());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, page_row->y());

  tree_view->CompleteRowBoundsAnimationForTesting();
  EXPECT_EQ(2 * SidebarTreeRowView::kRowHeight, page_row->y());
}

TEST_F(SidebarTreeViewTest, ExpandLandsImmediatelyWhenRichMotionIsDisabled) {
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  ASSERT_TRUE(render_mode);

  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const tab_tree::TreeNode child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Issue tracker", "a");
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Other page", "b");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, page}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, {child}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* page_row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, page_row);
  EXPECT_FALSE(view->row_bounds_animation_running_for_testing());
  EXPECT_EQ(2 * SidebarTreeRowView::kRowHeight, page_row->y());
}

TEST_F(SidebarTreeViewTest, SavedPageSelectsAndActivatesOnlyOnMouseUp) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  tab_tree::TreeNode second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "b");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {first, second}));
  ASSERT_TRUE(controller_->SelectNode(first.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  ASSERT_NE(nullptr, second_row);
  const gfx::Point click_point(80, SidebarTreeRowView::kRowHeight / 2);
  ui::MouseEvent press(ui::EventType::kMousePressed, click_point, click_point,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, click_point,
                         click_point, base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);

  ASSERT_TRUE(second_row->OnMousePressed(press));
  EXPECT_EQ(first.id, model.selected_node_id());
  EXPECT_FALSE(delegate_.activated_node.has_value());

  second_row->OnMouseReleased(release);
  EXPECT_EQ(second.id, model.selected_node_id());
  EXPECT_EQ(second.id, delegate_.activated_node);
}

TEST_F(SidebarTreeViewTest, NativeDragAndOutsideReleaseDoNotActivateRow) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "a");
  const tab_tree::TreeNode second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "b");

  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {first, second}));
  ASSERT_TRUE(controller_->SelectNode(first.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  SidebarTreeRowView* second_row =
      view->GetMaterializedRowForTesting(second.id);
  ASSERT_NE(nullptr, second_row);
  const gfx::Point click_point(80, SidebarTreeRowView::kRowHeight / 2);
  ui::MouseEvent press(ui::EventType::kMousePressed, click_point, click_point,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, click_point,
                         click_point, base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);

  ASSERT_TRUE(second_row->OnMousePressed(press));
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(second_row, click_point, &drag_data);
  second_row->OnMouseReleased(release);
  EXPECT_EQ(first.id, model.selected_node_id());
  EXPECT_FALSE(delegate_.activated_node.has_value());
  EXPECT_EQ(second.id, drag::ReadSavedSidebarTabDragPayload(drag_data));
  second_row->OnDragDone();

  const gfx::Point outside_point(-20, click_point.y());
  ui::MouseEvent outside_release(ui::EventType::kMouseReleased, outside_point,
                                 outside_point, base::TimeTicks::Now(),
                                 ui::EF_LEFT_MOUSE_BUTTON,
                                 ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(second_row->OnMousePressed(press));
  second_row->OnMouseReleased(outside_release);
  EXPECT_EQ(first.id, model.selected_node_id());
  EXPECT_FALSE(delegate_.activated_node.has_value());
}

TEST_F(SidebarTreeViewTest, SavedPageTrailingActionRequiresRealHover) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Running", "a");
  delegate_.saved_page_running = true;

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {page}));
  ASSERT_TRUE(controller_->SelectNode(page.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));

  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, row);
  const gfx::Point action_point(row->width() - 16,
                                SidebarTreeRowView::kRowHeight / 2);
  EXPECT_FALSE(row->IsTrailingActionAt(action_point));

  ui::MouseEvent enter(ui::EventType::kMouseEntered, action_point, action_point,
                       base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE);
  row->OnMouseEntered(enter);
  EXPECT_TRUE(row->IsTrailingActionAt(action_point));

  ui::MouseEvent exit(ui::EventType::kMouseExited, action_point, action_point,
                      base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE);
  row->OnMouseExited(exit);
  EXPECT_FALSE(row->IsTrailingActionAt(action_point));
}
}  // namespace

}  // namespace ahoi::sidebar
