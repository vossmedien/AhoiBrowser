// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ahoi/browser/ui/visual_style.h"
#include "ui/views/view_test_api.h"

namespace ahoi::sidebar {

namespace {

TEST_F(SidebarTreeViewTest, RenameCommitsThroughControllerAndStore) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Old title", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_TRUE(controller_->SelectNode(page.id));

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  view->BeginRenameSelectedNode();
  SidebarTreeRowView* row = view->GetMaterializedRowForTesting(page.id);
  ASSERT_NE(nullptr, row);
  EXPECT_TRUE(row->is_editing());

  view->CommitRename(page.id, u"New title");
  tab_tree::TreeNode stored;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(page.id, &stored));
  EXPECT_EQ(u"New title", stored.title);
  EXPECT_FALSE(row->is_editing());
}

TEST_F(SidebarTreeViewTest,
       SuppressedRuntimeProxyRejectsStaleSelectionActions) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Mixed saved", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_TRUE(controller_->SelectNode(page.id));

  auto view = NewTreeView();
  view->SetRuntimeCompositeSuppressedNodes({page.id});
  EXPECT_FALSE(controller_->view_model().selected_node_id().has_value());
  ASSERT_TRUE(controller_->SelectNode(page.id));
  view->SetRuntimeCompositeSuppressedNodes({page.id});
  EXPECT_FALSE(controller_->view_model().selected_node_id().has_value());

  ASSERT_TRUE(controller_->SelectNode(page.id));
  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));
  EXPECT_FALSE(delegate_.activated_node.has_value());
  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_F2, ui::EF_NONE)));
  EXPECT_FALSE(view->editing_node_id_for_testing().has_value());
  EXPECT_TRUE(view->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_DELETE, ui::EF_NONE)));
  tab_tree::TreeNode stored;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(page.id, &stored));
}

TEST_F(SidebarTreeViewTest,
       EmptySavedTreeExposesAndClearsDragTargetPresentation) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));

  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().rows().empty());
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            view->GetPreferredSize().height());

  views::ViewTestApi view_test_api(view.get());
  view_test_api.ClearNeedsPaint();
  view->SetDragTargetVisible(true);
  EXPECT_TRUE(view->drag_target_visible_for_testing());
  EXPECT_FALSE(view->drag_target_accepting_for_testing());
  EXPECT_TRUE(view_test_api.needs_paint());

  view_test_api.ClearNeedsPaint();
  SidebarTreeView::DropIndicator root_indicator;
  root_indicator.source_runtime_tab_handle = 7;
  view->SetDropIndicatorForTesting(root_indicator);
  EXPECT_TRUE(view->drag_target_accepting_for_testing());
  ASSERT_TRUE(view->drop_indicator_for_testing().has_value());
  EXPECT_FALSE(view->drop_indicator_for_testing()->target_node_id.has_value());
  EXPECT_TRUE(view_test_api.needs_paint());

  view_test_api.ClearNeedsPaint();
  view->SetDragTargetVisible(false);
  EXPECT_FALSE(view->drag_target_visible_for_testing());
  EXPECT_FALSE(view->drag_target_accepting_for_testing());
  EXPECT_FALSE(view->drop_indicator_for_testing().has_value());
  EXPECT_TRUE(view_test_api.needs_paint());
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            view->GetPreferredSize().height());
}

TEST_F(SidebarTreeViewTest, DropZonesValidateAndPaintBeforeInsideAfter) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  tab_tree::TreeNode folder = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kFolder, u"Target", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  const int target_y = SidebarTreeRowView::kRowHeight;
  const auto before = view->CalculateDropIndicatorForTesting(
      source.id, gfx::Point(12, target_y + 1),
      SidebarTreeController::DropOperation::kCopy);
  const auto inside = view->CalculateDropIndicatorForTesting(
      source.id, gfx::Point(12, target_y + SidebarTreeRowView::kRowHeight / 2),
      SidebarTreeController::DropOperation::kCopy);
  const auto after = view->CalculateDropIndicatorForTesting(
      source.id, gfx::Point(12, target_y + SidebarTreeRowView::kRowHeight - 1),
      SidebarTreeController::DropOperation::kCopy);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(inside.has_value());
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(SidebarTreeController::DropPosition::kBefore, before->position);
  EXPECT_EQ(SidebarTreeController::DropPosition::kInside, inside->position);
  EXPECT_EQ(SidebarTreeController::DropPosition::kAfter, after->position);

  view->SetDropIndicatorForTesting(before);
  ASSERT_TRUE(view->insertion_marker_visible_for_testing());
  const gfx::Rect stable_before_edge =
      view->insertion_marker_bounds_for_testing();
  const auto same_before_zone = view->CalculateDropIndicatorForTesting(
      source.id, gfx::Point(12, target_y + 6),
      SidebarTreeController::DropOperation::kCopy);
  ASSERT_TRUE(same_before_zone.has_value());
  view->SetDropIndicatorForTesting(same_before_zone);
  EXPECT_EQ(stable_before_edge, view->insertion_marker_bounds_for_testing());
  view->SetDropIndicatorForTesting(after);
  EXPECT_GT(view->insertion_marker_bounds_for_testing().y(),
            stable_before_edge.y());

  view->SetDropIndicatorForTesting(inside);
  SidebarTreeRowView* target = view->GetMaterializedRowForTesting(folder.id);
  ASSERT_NE(nullptr, target);
  EXPECT_EQ(SidebarTreeController::DropPosition::kInside,
            target->drop_position_for_testing());
  view->SetDropIndicatorForTesting(std::nullopt);
  EXPECT_FALSE(target->drop_position_for_testing().has_value());

  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(source.id);
  ASSERT_NE(nullptr, source_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(), &drag_data);
  ASSERT_TRUE(drag_data.GetString().has_value());
  EXPECT_EQ(u"Source", *drag_data.GetString());
  const gfx::PointF inside_point(12,
                                 target_y + SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, inside_point, inside_point,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);

  tab_tree::TreeNode moved_source;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &moved_source));
  EXPECT_EQ(folder.id, moved_source.parent_id);
}

TEST_F(SidebarTreeViewTest, TabOnTabDropUsesVisibleSplitTarget) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  tab_tree::TreeNode target =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Target", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(target));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.can_split = true;
  delegate_.split_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  const gfx::Point split_point(
      120, SidebarTreeRowView::kRowHeight + SidebarTreeRowView::kRowHeight / 2);
  const auto indicator = view->CalculateDropIndicatorForTesting(
      source.id, split_point, SidebarTreeController::DropOperation::kMove);
  ASSERT_TRUE(indicator.has_value());
  EXPECT_EQ(SidebarTreeView::DropIndicator::Action::kSplit, indicator->action);

  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(source.id);
  SidebarTreeRowView* target_row =
      view->GetMaterializedRowForTesting(target.id);
  ASSERT_NE(nullptr, source_row);
  ASSERT_NE(nullptr, target_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(24, 16), &drag_data);
  // Payload creation is the earliest deterministic boundary before AppKit's
  // nested native drag loop, so it publishes the source immediately. The
  // later lifecycle callback must be idempotent.
  EXPECT_EQ(source.id, delegate_.drag_state);
  view->OnWillStartDragForView(source_row);
  EXPECT_EQ(source.id, delegate_.drag_state);

  const gfx::PointF split_point_f(split_point);
  ui::DropTargetEvent drop_event(drag_data, split_point_f, split_point_f,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  EXPECT_TRUE(target_row->is_split_drop_target_for_testing());
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  ASSERT_EQ(1u, delegate_.split_requests.size());
  EXPECT_EQ(source.id, delegate_.split_requests.front().first);
  EXPECT_EQ(target.id, delegate_.split_requests.front().second);
  view->OnRowDragDone();
  EXPECT_FALSE(delegate_.drag_state.has_value());
}

TEST_F(SidebarTreeViewTest,
       SavedTabDropHysteresisReleasesAfterFourDipBoundary) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode leading =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Leading", "a");
  tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "c");
  tab_tree::TreeNode target =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Target", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(leading));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(target));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.can_split = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(source.id);
  ASSERT_NE(nullptr, source_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(24, 16), &drag_data);

  const int target_y = SidebarTreeRowView::kRowHeight;
  const int boundary =
      target_y + GetSidebarEdgeDropTargetExtent(SidebarTreeRowView::kRowHeight);
  const auto update_at =
      [&](int y) -> std::optional<SidebarTreeController::DropPosition> {
    const gfx::PointF point(120, y);
    ui::DropTargetEvent event(drag_data, point, point,
                              ui::DragDropTypes::DRAG_MOVE);
    EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
              view->OnDragUpdated(event));
    if (!view->drop_indicator_for_testing().has_value()) {
      return std::nullopt;
    }
    return view->drop_indicator_for_testing()->position;
  };

  EXPECT_EQ(std::make_optional(SidebarTreeController::DropPosition::kBefore),
            update_at(boundary - 1));
  EXPECT_EQ(std::make_optional(SidebarTreeController::DropPosition::kBefore),
            update_at(boundary + 1));
  EXPECT_EQ(std::make_optional(SidebarTreeController::DropPosition::kInside),
            update_at(boundary + 4));
}

TEST_F(SidebarTreeViewTest,
       SplitDropPreviewUsesLeadingHalfAndSuppressesTrailingState) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode page = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
      u"A deliberately long title that must elide in the split preview", "a");
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
  ui::MouseEvent enter(ui::EventType::kMouseEntered, action_point, action_point,
                       base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE);
  row->OnMouseEntered(enter);
  ASSERT_TRUE(row->IsTrailingActionAt(action_point));
  ASSERT_TRUE(row->should_paint_trailing_state_for_testing());
  const gfx::Rect normal_title_bounds = row->title_bounds_for_testing();
  ASSERT_GT(normal_title_bounds.right(), row->width() / 2);
  EXPECT_EQ(page.title, row->GetViewAccessibility().GetCachedName());

  row->SetSplitDropTarget(true);

  const gfx::Rect preview_title_bounds = row->title_bounds_for_testing();
  const gfx::Rect preview_paint_bounds = row->title_paint_bounds_for_testing();
  EXPECT_GT(preview_title_bounds.width(), 0);
  EXPECT_LE(preview_title_bounds.right(), row->width() / 2);
  EXPECT_LT(preview_paint_bounds.right(), row->width() / 2);
  EXPECT_LT(preview_paint_bounds.bottom(), row->height());
  EXPECT_EQ(gfx::Rect(gfx::Point(), preview_paint_bounds.size()),
            row->title_paint_clip_bounds_for_testing());
  EXPECT_LT(preview_title_bounds.width(), normal_title_bounds.width());
  EXPECT_FALSE(row->IsTrailingActionAt(action_point));
  EXPECT_FALSE(row->should_paint_trailing_state_for_testing());
  EXPECT_EQ(u"Split with " + page.title,
            row->GetViewAccessibility().GetCachedName());

  row->SetSplitDropTarget(false);
  EXPECT_EQ(normal_title_bounds, row->title_bounds_for_testing());
  EXPECT_TRUE(row->IsTrailingActionAt(action_point));
  EXPECT_TRUE(row->should_paint_trailing_state_for_testing());
  EXPECT_EQ(page.title, row->GetViewAccessibility().GetCachedName());
}

TEST_F(SidebarTreeViewTest, TemporaryTabDropSavesInsideFolder) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.can_save_temporary = true;
  delegate_.save_temporary_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  ui::OSExchangeData drag_data;
  base::Pickle pickle;
  pickle.WriteInt(77);
  drag_data.SetPickledData(drag::RuntimeSidebarTabDragFormat(), pickle);
  const gfx::PointF inside_point(120, SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, inside_point, inside_point,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  ASSERT_EQ(1u, delegate_.saved_temporary_tabs.size());
  EXPECT_EQ(77, delegate_.saved_temporary_tabs.front().first);
  EXPECT_EQ(folder.id,
            delegate_.saved_temporary_tabs.front().second.target_node_id);
  EXPECT_EQ(SidebarTreeController::DropPosition::kInside,
            delegate_.saved_temporary_tabs.front().second.position);
}

TEST_F(SidebarTreeViewTest, TemporaryTabOnSavedTabUsesSplitTarget) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode target =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Target", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(target));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.can_split_temporary = true;
  delegate_.split_temporary_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  ui::OSExchangeData drag_data;
  base::Pickle pickle;
  pickle.WriteInt(91);
  drag_data.SetPickledData(drag::RuntimeSidebarTabDragFormat(), pickle);
  const gfx::PointF inside_point(120, SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, inside_point, inside_point,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  ASSERT_EQ(1u, delegate_.split_temporary_requests.size());
  EXPECT_EQ(91, delegate_.split_temporary_requests.front().first);
  EXPECT_EQ(target.id, delegate_.split_temporary_requests.front().second);
}

TEST_F(SidebarTreeViewTest, SavedSplitPaneDropReordersTargetedGridSegment) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
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
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(second));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(third));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(fourth));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.split_groups = {{first.id, second.id, third.id, fourth.id}};
  const auto split_visual_data = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kStacked);
  delegate_.split_visual_data = split_visual_data;
  delegate_.can_reorder_split = true;
  delegate_.reorder_split_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  // Derive the fourth pane's center from the production layout so this remains
  // valid when the minimum readable split-row height changes.
  const int split_row_height = GetSplitRowPreferredHeight(
      4, split_visual_data, SidebarTreeRowView::kRowHeight);
  const gfx::Point target_point =
      GetSplitSegmentBounds(gfx::Rect(0, 0, 240, split_row_height), 3, 4,
                            split_visual_data)
          .CenterPoint();
  const auto indicator = view->CalculateDropIndicatorForTesting(
      first.id, target_point, SidebarTreeController::DropOperation::kMove);
  ASSERT_TRUE(indicator.has_value());
  EXPECT_EQ(SidebarTreeView::DropIndicator::Action::kReorderSplitPane,
            indicator->action);
  EXPECT_EQ(fourth.id, indicator->target_node_id);

  SidebarTreeRowView* source_row = view->GetMaterializedRowForTesting(first.id);
  ASSERT_NE(nullptr, source_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(30, 16), &drag_data);
  const gfx::PointF target_point_f(target_point);
  ui::DropTargetEvent drop_event(drag_data, target_point_f, target_point_f,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  ASSERT_EQ(1u, delegate_.reorder_split_requests.size());
  EXPECT_EQ(std::make_pair(first.id, fourth.id),
            delegate_.reorder_split_requests.front());
}

TEST_F(SidebarTreeViewTest,
       SavedSplitPaneDropOnItsOwnGridEdgeExtractsThatPane) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
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
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(second));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(third));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(fourth));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.split_groups = {{first.id, second.id, third.id, fourth.id}};
  delegate_.split_visual_data = split_tabs::SplitTabVisualData::ForFourPane(
      split_tabs::SplitTabLayout::kSideBySide);
  delegate_.can_extract_saved_split = true;
  delegate_.extract_saved_split_succeeds = true;

  auto view = NewTreeView();
  const int split_height = GetSplitRowPreferredHeight(
      4, *delegate_.split_visual_data, SidebarTreeRowView::kRowHeight);
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, split_height));
  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(fourth.id);
  ASSERT_NE(nullptr, source_row);

  // The fourth pane starts on the second visual row. Its own top edge is a
  // detach target; calculating against the outer split row would incorrectly
  // classify this same point as an inside/reorder drop.
  const gfx::Point detach_point(source_row->bounds().CenterPoint().x(),
                                source_row->y() + 1);
  const auto indicator = view->CalculateDropIndicatorForTesting(
      fourth.id, detach_point, SidebarTreeController::DropOperation::kMove);
  ASSERT_TRUE(indicator.has_value());
  EXPECT_EQ(fourth.id, indicator->target_node_id);
  EXPECT_EQ(SidebarTreeController::DropPosition::kBefore, indicator->position);
  EXPECT_EQ(SidebarTreeView::DropIndicator::Action::kExtractSplitPane,
            indicator->action);

  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(30, 1), &drag_data);
  const gfx::PointF detach_point_f(detach_point);
  ui::DropTargetEvent drop_event(drag_data, detach_point_f, detach_point_f,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);

  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  EXPECT_TRUE(delegate_.reorder_split_requests.empty());
  ASSERT_EQ(1u, delegate_.extracted_split_requests.size());
  EXPECT_EQ(fourth.id, delegate_.extracted_split_requests.front());
}

TEST_F(SidebarTreeViewTest,
       SavedSplitPaneDropOnOrdinaryTargetMovesOnlyExtractedPane) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  tab_tree::TreeNode sibling =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Sibling", "b");
  tab_tree::TreeNode folder = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kFolder, u"Folder", "c");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(sibling));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.split_groups = {{source.id, sibling.id}};
  delegate_.can_extract_saved_split = true;
  delegate_.extract_saved_split_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(source.id);
  ASSERT_NE(nullptr, source_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(30, 16), &drag_data);
  const gfx::PointF folder_center(
      120, SidebarTreeRowView::kRowHeight + SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, folder_center, folder_center,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);

  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  ASSERT_EQ(1u, delegate_.extracted_split_requests.size());
  EXPECT_EQ(source.id, delegate_.extracted_split_requests.front());
  tab_tree::TreeNode moved_source;
  tab_tree::TreeNode untouched_sibling;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &moved_source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(sibling.id, &untouched_sibling));
  EXPECT_EQ(folder.id, moved_source.parent_id);
  EXPECT_FALSE(untouched_sibling.parent_id.has_value());
}

TEST_F(SidebarTreeViewTest,
       FailedSavedSplitExtractionRollsBackOrdinaryTargetMove) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  tab_tree::TreeNode sibling =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Sibling", "b");
  tab_tree::TreeNode folder = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kFolder, u"Folder", "c");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(sibling));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_TRUE(controller_->SelectNode(sibling.id));
  delegate_.split_groups = {{source.id, sibling.id}};
  delegate_.can_extract_saved_split = true;
  delegate_.extract_saved_split_succeeds = false;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));
  SidebarTreeRowView* source_row =
      view->GetMaterializedRowForTesting(source.id);
  ASSERT_NE(nullptr, source_row);
  ui::OSExchangeData drag_data;
  view->WriteDragDataForView(source_row, gfx::Point(30, 16), &drag_data);
  const gfx::PointF folder_center(
      120, SidebarTreeRowView::kRowHeight + SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, folder_center, folder_center,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kMove;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);

  EXPECT_EQ(ui::mojom::DragOperation::kNone, output_operation);
  ASSERT_EQ(1u, delegate_.extracted_split_requests.size());
  EXPECT_EQ(source.id, delegate_.extracted_split_requests.front());
  tab_tree::TreeNode restored_source;
  tab_tree::TreeNode untouched_sibling;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &restored_source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(sibling.id, &untouched_sibling));
  EXPECT_EQ(source, restored_source);
  EXPECT_FALSE(untouched_sibling.parent_id.has_value());
  EXPECT_EQ(sibling.id, controller_->view_model().selected_node_id());
  std::vector<tab_tree::TreeNode> folder_children;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(workspace.id, folder.id, &folder_children));
  EXPECT_TRUE(folder_children.empty());
  std::vector<tab_tree::TreeNode> root_children;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(workspace.id, std::nullopt, &root_children));
  EXPECT_EQ((std::vector<tab_tree::TreeNode>{source, sibling, folder}),
            root_children);
}

TEST_F(SidebarTreeViewTest,
       SavedSplitExtractionCallbackFailsClosedWhenStateChanges) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const tab_tree::TreeNode source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  auto view = NewTreeView();
  SidebarTreeView::DropIndicator indicator;
  indicator.source_node_id = source.id;
  indicator.target_node_id = source.id;
  indicator.position = SidebarTreeController::DropPosition::kBefore;
  indicator.action = SidebarTreeView::DropIndicator::Action::kExtractSplitPane;
  ui::OSExchangeData drag_data;
  const gfx::PointF point;
  ui::DropTargetEvent drop_event(drag_data, point, point,
                                 ui::DragDropTypes::DRAG_MOVE);

  delegate_.can_extract_saved_split = true;
  view->SetDropIndicatorForTesting(indicator);
  views::View::DropCallback stale_callback = view->GetDropCallback(drop_event);
  delegate_.can_extract_saved_split = false;
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kMove;
  std::move(stale_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kNone, output_operation);
  EXPECT_TRUE(delegate_.extracted_split_requests.empty());

  delegate_.can_extract_saved_split = true;
  view->SetDropIndicatorForTesting(indicator);
  views::View::DropCallback failed_callback = view->GetDropCallback(drop_event);
  output_operation = ui::mojom::DragOperation::kMove;
  std::move(failed_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kNone, output_operation);
  ASSERT_EQ(1u, delegate_.extracted_split_requests.size());
  EXPECT_EQ(source.id, delegate_.extracted_split_requests.front());
}

TEST_F(SidebarTreeViewTest,
       TemporarySplitPaneDropReordersAgainstSavedSegmentWithoutSaving) {
  tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode target =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Target", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(target));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  delegate_.can_reorder_temporary_split = true;
  delegate_.reorder_temporary_split_succeeds = true;

  auto view = NewTreeView();
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 64));
  ui::OSExchangeData drag_data;
  base::Pickle pickle;
  pickle.WriteInt(123);
  drag_data.SetPickledData(drag::RuntimeSidebarTabDragFormat(), pickle);
  const gfx::PointF inside_point(120, SidebarTreeRowView::kRowHeight / 2);
  ui::DropTargetEvent drop_event(drag_data, inside_point, inside_point,
                                 ui::DragDropTypes::DRAG_MOVE);
  EXPECT_EQ(static_cast<int>(ui::mojom::DragOperation::kMove),
            view->OnDragUpdated(drop_event));
  ASSERT_TRUE(view->drop_indicator_for_testing().has_value());
  EXPECT_EQ(SidebarTreeView::DropIndicator::Action::kReorderSplitPane,
            view->drop_indicator_for_testing()->action);
  views::View::DropCallback drop_callback = view->GetDropCallback(drop_event);
  ASSERT_TRUE(drop_callback);
  ui::mojom::DragOperation output_operation = ui::mojom::DragOperation::kNone;
  std::move(drop_callback)
      .Run(drop_event, output_operation,
           /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, output_operation);
  EXPECT_TRUE(delegate_.saved_temporary_tabs.empty());
  ASSERT_EQ(1u, delegate_.reorder_temporary_split_requests.size());
  EXPECT_EQ(std::make_pair(123, target.id),
            delegate_.reorder_temporary_split_requests.front());
}

TEST(SidebarTreeViewRangeTest, AddsBoundedOverscanAtBothEdges) {
  EXPECT_EQ((SidebarTreeView::VisibleRange{.first = 3, .past_last = 12}),
            SidebarTreeView::CalculateVisibleRange(
                100,
                gfx::Rect(0, 5 * SidebarTreeRowView::kRowHeight, 240,
                          5 * SidebarTreeRowView::kRowHeight),
                2));
  EXPECT_EQ((SidebarTreeView::VisibleRange{.first = 0, .past_last = 3}),
            SidebarTreeView::CalculateVisibleRange(
                3, gfx::Rect(0, 0, 240, SidebarTreeRowView::kRowHeight), 2));
}
}  // namespace

}  // namespace ahoi::sidebar
