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
