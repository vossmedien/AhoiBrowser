// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ui/events/event.h"

namespace ahoi::sidebar {
namespace {

void Click(SidebarTreeView& view, const base::Uuid& node_id, int modifiers) {
  SidebarTreeRowView* row = view.GetMaterializedRowForTesting(node_id);
  ASSERT_NE(nullptr, row);
  const gfx::Point point(80, SidebarTreeRowView::kRowHeight / 2);
  const int flags = ui::EF_LEFT_MOUSE_BUTTON | modifiers;
  ui::MouseEvent press(ui::EventType::kMousePressed, point, point,
                       base::TimeTicks::Now(), flags, ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                         base::TimeTicks::Now(), flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(row->OnMousePressed(press));
  row->OnMouseReleased(release);
}

TEST_F(SidebarTreeViewTest, CommandAndShiftClickBuildAMultiSelection) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode a = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"A", "a");
  tab_tree::TreeNode b = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"B", "b");
  tab_tree::TreeNode c = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"C", "c");
  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {a, b, c}));
  ASSERT_TRUE(controller_->SelectNode(a.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 144));

  // ⌘-click adds to the selected row and does not activate anything.
  Click(*view, c.id, ui::EF_COMMAND_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{a.id, c.id}), view->multi_selection());
  EXPECT_FALSE(delegate_.activated_node.has_value());

  // ⇧-click replaces it with the range from the anchor (c) to b.
  Click(*view, b.id, ui::EF_SHIFT_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{b.id, c.id}), view->multi_selection());

  // ⌘-click on a selected row removes it.
  Click(*view, c.id, ui::EF_COMMAND_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{b.id}), view->multi_selection());

  // A plain click clears the multi-selection and activates as before.
  Click(*view, a.id, 0);
  EXPECT_TRUE(view->multi_selection().empty());
  EXPECT_EQ(a.id, delegate_.activated_node);
}

TEST_F(SidebarTreeViewTest, EscapeClearsTheMultiSelection) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode a = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"A", "a");
  tab_tree::TreeNode b = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"B", "b");
  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {a, b}));
  ASSERT_TRUE(controller_->SelectNode(a.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  Click(*view, b.id, ui::EF_COMMAND_DOWN);
  ASSERT_EQ(2u, view->multi_selection().size());
  ui::KeyEvent escape(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE,
                      ui::EF_NONE);
  EXPECT_TRUE(view->OnKeyPressed(escape));
  EXPECT_TRUE(view->multi_selection().empty());
}

}  // namespace
}  // namespace ahoi::sidebar
