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
