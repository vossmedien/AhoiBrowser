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

TEST_F(SidebarTreeViewTest, TrailingSurfaceAppendsAfterExpandedRootFolder) {
  const auto workspace = MakeWorkspace();
  const auto source =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Source", "a");
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Last root folder", "b");
  const auto child =
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Last visible child", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  for (const auto& node : {source, folder, child}) {
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(node));
  }
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ExpandNode(folder.id));
  auto view = NewTreeView();
  const int rows_height = 3 * SidebarTreeRowView::kRowHeight;
  const gfx::Rect bounds(0, 0, 240,
                         rows_height + SidebarTreeView::kRootAppendDropHeight);
  view->SetBoundsRect(bounds);
  view->SynchronizeRowsForTesting(bounds);
  const auto* source_row = view->GetMaterializedRowForTesting(source.id);
  ASSERT_TRUE(source_row);
  ui::OSExchangeData data;
  drag::WriteSavedSidebarTabDragPayload(&data, source.id, source.title);
  const gfx::PointF edge(120, rows_height - 1);
  ui::DropTargetEvent edge_event(data, edge, edge,
                                 ui::DragDropTypes::DRAG_MOVE);
  ASSERT_EQ(ui::DragDropTypes::DRAG_MOVE, view->OnDragUpdated(edge_event));
  ASSERT_TRUE(view->drop_indicator_for_testing());
  EXPECT_EQ(child.id, view->drop_indicator_for_testing()->target_node_id);
  EXPECT_TRUE(view->insertion_marker_visible_for_testing());

  const gfx::PointF tail(
      120, rows_height + SidebarTreeView::kRootAppendDropHeight / 2);
  ui::DropTargetEvent tail_event(data, tail, tail,
                                 ui::DragDropTypes::DRAG_MOVE);
  ASSERT_EQ(ui::DragDropTypes::DRAG_MOVE, view->OnDragUpdated(tail_event));
  ASSERT_TRUE(view->drop_indicator_for_testing());
  EXPECT_FALSE(view->drop_indicator_for_testing()->target_node_id);
  EXPECT_EQ(
      gfx::Rect(0, rows_height, 240, SidebarTreeView::kRootAppendDropHeight),
      view->drop_indicator_for_testing()->target_bounds);
  EXPECT_TRUE(view->drag_target_accepting_for_testing());
  EXPECT_FALSE(view->insertion_marker_visible_for_testing());
  auto callback = view->GetDropCallback(tail_event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation operation = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(tail_event, operation, nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, operation);
  std::vector<tab_tree::TreeNode> roots;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(workspace.id, std::nullopt, &roots));
  ASSERT_EQ(2u, roots.size());
  EXPECT_EQ(folder.id, roots.front().id);
  EXPECT_EQ(source.id, roots.back().id);
  EXPECT_FALSE(roots.back().parent_id);
}

TEST_F(SidebarTreeViewTest, TemporaryTailDropUsesGuardWithoutLayoutJump) {
  const auto workspace = MakeWorkspace();
  const auto page = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"Saved", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  auto view = NewTreeView();
  view->SetBounds(0, 0, 240, view->GetPreferredSize().height());
  const gfx::Size before_drag = view->GetPreferredSize();
  view->SetDragTargetVisible(true);
  EXPECT_EQ(before_drag, view->GetPreferredSize());
  ui::OSExchangeData data;
  drag::WriteRuntimeSidebarTabDragPayload(&data, 123, u"Temporary");
  const gfx::PointF point(120, SidebarTreeRowView::kRowHeight +
                                   SidebarTreeView::kRootAppendDropHeight / 2);
  ui::DropTargetEvent event(data, point, point, ui::DragDropTypes::DRAG_MOVE);
  delegate_.can_save_temporary = false;
  EXPECT_EQ(ui::DragDropTypes::DRAG_NONE, view->OnDragUpdated(event));
  EXPECT_FALSE(view->drag_target_accepting_for_testing());
  view->OnDragExited();
  delegate_.can_save_temporary = true;
  delegate_.save_temporary_succeeds = true;
  ASSERT_EQ(ui::DragDropTypes::DRAG_MOVE, view->OnDragUpdated(event));
  EXPECT_TRUE(view->drag_target_accepting_for_testing());
  EXPECT_FALSE(view->insertion_marker_visible_for_testing());
  auto callback = view->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation operation = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, operation, nullptr);
  EXPECT_EQ(ui::mojom::DragOperation::kMove, operation);
  ASSERT_EQ(1u, delegate_.saved_temporary_tabs.size());
  const auto& [handle, target] = delegate_.saved_temporary_tabs.front();
  EXPECT_EQ(123, handle);
  EXPECT_EQ(workspace.id, target.workspace_id);
  EXPECT_FALSE(target.target_node_id);
  EXPECT_EQ(SidebarTreeController::DropPosition::kInside, target.position);
  view->SetDragTargetVisible(false);
  EXPECT_FALSE(view->drag_target_accepting_for_testing());
  EXPECT_EQ(before_drag, view->GetPreferredSize());
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

}  // namespace

}  // namespace ahoi::sidebar
