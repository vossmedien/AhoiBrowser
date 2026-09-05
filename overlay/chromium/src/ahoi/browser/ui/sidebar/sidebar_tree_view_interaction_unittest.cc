// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/gfx/animation/animation_container.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

std::vector<tab_tree::TreeNode> MakeMotionChildren(
    const tab_tree::Workspace& workspace,
    const tab_tree::TreeNode& folder) {
  return {
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"First child", "a"),
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Second child", "b"),
      MakeNode(workspace, folder.id, tab_tree::TreeNodeType::kSavedPage,
               u"Third child", "c"),
  };
}

void SettleTreeMotion(SidebarTreeView* tree) {
  tree->height_animation_for_testing()->Reset(1.0);
  tree->CompleteRowBoundsAnimationForTesting();
}

scoped_refptr<gfx::AnimationContainer> BindHeightTestContainer(
    SidebarTreeView* tree) {
  // Bind once before model changes start motion. Rebinding a running animation
  // would reset its start time; sharing the row container would also change the
  // BoundsAnimator observer's frame/completion boundaries.
  EXPECT_FALSE(tree->height_animation_for_testing()->is_animating());
  auto container = base::MakeRefCounted<gfx::AnimationContainer>();
  tree->height_animation_for_testing()->SetContainer(container.get());
  return container;
}

void AdvanceTreeMotion(SidebarTreeView* tree,
                       gfx::AnimationContainer* height_container,
                       base::TimeDelta elapsed) {
  gfx::AnimationContainerTestApi height_clock(height_container);
  gfx::AnimationContainerTestApi row_clock(
      tree->row_bounds_animation_container_for_testing());
  height_clock.IncrementTime(elapsed);
  row_clock.IncrementTime(elapsed);
}

void ExpectVisibleSplitRowClip(SidebarTreeRowView* row) {
  ASSERT_TRUE(row);
  ASSERT_FALSE(row->clip_path().isEmpty());
  SkRect visible_clip = row->clip_path().getBounds();
  EXPECT_TRUE(visible_clip.intersect(SkRect::MakeWH(
      static_cast<float>(row->width()), static_cast<float>(row->height()))));
  EXPECT_TRUE(
      row->clip_path().contains(row->width() / 2.0f, row->height() / 2.0f));
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

TEST_F(SidebarTreeViewTest,
       RapidFolderReversalPreservesIntermediatePreferredHeight) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto page = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"After", "b");
  auto view = NewTreeView();
  auto* tree = view.get();
  const auto height_container = BindHeightTestContainer(tree);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, page}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, MakeMotionChildren(workspace, folder)));
  const gfx::Rect viewport(0, 0, 240, 320);
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  const int collapsed_height = 2 * SidebarTreeRowView::kRowHeight;
  const int expanded_height = 5 * SidebarTreeRowView::kRowHeight;
  ASSERT_EQ(collapsed_height, tree->GetPreferredSize().height());

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration / 3);
  const int expanding_height = tree->GetPreferredSize().height();
  ASSERT_GT(expanding_height, collapsed_height);
  ASSERT_LT(expanding_height, expanded_height);

  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  EXPECT_EQ(expanding_height, tree->GetPreferredSize().height());
  ASSERT_TRUE(tree->height_animation_for_testing()->is_animating());
  tree->SynchronizeRowsForTesting(viewport);
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration / 3);
  const int collapsing_height = tree->GetPreferredSize().height();
  ASSERT_GT(collapsing_height, collapsed_height);
  ASSERT_LT(collapsing_height, expanding_height);

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  EXPECT_EQ(collapsing_height, tree->GetPreferredSize().height());
  ASSERT_TRUE(tree->height_animation_for_testing()->is_animating());
  tree->SynchronizeRowsForTesting(viewport);
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration);
  EXPECT_FALSE(tree->height_animation_for_testing()->is_animating());
  EXPECT_EQ(expanded_height, tree->GetPreferredSize().height());
  EXPECT_FALSE(tree->row_bounds_animation_running_for_testing());
}

TEST_F(SidebarTreeViewTest, MovingSplitClipFollowsEveryFolderAnimationFrame) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"First", "b");
  const auto second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Second", "c");
  auto view = NewTreeView();
  auto* tree = view.get();
  const auto height_container = BindHeightTestContainer(tree);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, first, second}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, MakeMotionChildren(workspace, folder)));
  delegate_.split_groups = {{first.id, second.id}};
  tree->OnSplitGroupsChanged();
  const gfx::Rect viewport(0, 0, 240, 320);
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  auto* first_row = tree->GetMaterializedRowForTesting(first.id);
  auto* second_row = tree->GetMaterializedRowForTesting(second.id);
  ASSERT_TRUE(first_row);
  ASSERT_TRUE(second_row);
  ASSERT_EQ(SidebarTreeRowView::kRowHeight, first_row->y());

  for (bool expanded : {true, false}) {
    SCOPED_TRACE(expanded ? "expanding" : "collapsing");
    const int start_y = first_row->y();
    const int target_y = (expanded ? 4 : 1) * SidebarTreeRowView::kRowHeight;
    ASSERT_TRUE(model.SetExpanded(folder.id, expanded));
    tree->SynchronizeRowsForTesting(viewport);
    ASSERT_TRUE(tree->row_bounds_animation_running_for_testing());
    EXPECT_EQ(start_y, first_row->y());
    ExpectVisibleSplitRowClip(first_row);
    ExpectVisibleSplitRowClip(second_row);

    for (int frame = 0; frame < 3; ++frame) {
      SCOPED_TRACE(frame);
      // Do not synchronize rows here: the animator's completed frame must
      // update shared clips after all segment bounds have moved.
      AdvanceTreeMotion(tree, height_container.get(),
                        visual_style::kTreeMotionDuration / 4);
      EXPECT_NE(start_y, first_row->y());
      EXPECT_NE(target_y, first_row->y());
      EXPECT_EQ(first_row->y(), second_row->y());
      ExpectVisibleSplitRowClip(first_row);
      ExpectVisibleSplitRowClip(second_row);
    }
    SettleTreeMotion(tree);
    EXPECT_EQ(target_y, first_row->y());
    EXPECT_EQ(target_y, second_row->y());
    ExpectVisibleSplitRowClip(first_row);
    ExpectVisibleSplitRowClip(second_row);
  }
}

TEST_F(SidebarTreeViewTest, ReducedMotionFinishesHeightAndRowsMidTransition) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto page = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"After", "b");
  auto view = NewTreeView();
  auto* tree = view.get();
  const auto height_container = BindHeightTestContainer(tree);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, page}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, MakeMotionChildren(workspace, folder)));
  const gfx::Rect viewport(0, 0, 240, 320);
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration / 3);
  ASSERT_TRUE(tree->height_animation_for_testing()->is_animating());
  ASSERT_TRUE(tree->row_bounds_animation_running_for_testing());
  ASSERT_LT(tree->GetPreferredSize().height(),
            5 * SidebarTreeRowView::kRowHeight);

  // The API refuses overlapping forced modes, so restore PLATFORM first.
  render_mode.reset();
  const auto reduced_motion = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  ASSERT_TRUE(reduced_motion);
  tree->SynchronizeRowsForTesting(viewport);

  EXPECT_FALSE(tree->height_animation_for_testing()->is_animating());
  EXPECT_FALSE(tree->row_bounds_animation_running_for_testing());
  EXPECT_EQ(5 * SidebarTreeRowView::kRowHeight,
            tree->GetPreferredSize().height());
  auto* page_row = tree->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(page_row);
  EXPECT_EQ(4 * SidebarTreeRowView::kRowHeight, page_row->y());
}

TEST_F(SidebarTreeViewTest,
       SelectingVisibleMovingRowDoesNotScrollToItsFuturePosition) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto page = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"After", "b");
  const auto trailing_first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing first", "c");
  const auto trailing_second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing second", "d");
  const auto trailing_third =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing third", "e");
  auto view = NewTreeView();
  auto* tree = view.get();
  auto scroll = std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled);
  auto* scroll_view = scroll.get();
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll->SetContents(std::move(view));
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 3 * SidebarTreeRowView::kRowHeight));
  widget->SetContentsView(std::move(scroll));
  widget->Show();
  auto scroll_synchronizer = scroll_view->EnableScrollSynchronization();
  ASSERT_TRUE(scroll_synchronizer);
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(
      std::nullopt,
      {folder, page, trailing_first, trailing_second, trailing_third}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, MakeMotionChildren(workspace, folder)));
  SettleTreeMotion(tree);
  views::test::RunScheduledLayout(widget.get());
  tree->SynchronizeRowsForTesting(scroll_view->GetVisibleRect());
  const gfx::Rect viewport_before = scroll_view->GetVisibleRect();
  ASSERT_GT(viewport_before.height(), 0);
  ASSERT_GT(tree->height(), viewport_before.height());
  ASSERT_GE(tree->height(), 5 * SidebarTreeRowView::kRowHeight);

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(scroll_view->GetVisibleRect());
  auto* moving_row = tree->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(moving_row);
  ASSERT_TRUE(tree->row_bounds_animation_running_for_testing());
  ASSERT_TRUE(viewport_before.Contains(moving_row->bounds()));
  ASSERT_GT(5 * SidebarTreeRowView::kRowHeight, viewport_before.bottom());

  ASSERT_TRUE(controller_->SelectNode(page.id));

  EXPECT_EQ(page.id, model.selected_node_id());
  EXPECT_EQ(viewport_before.origin(), scroll_view->GetVisibleRect().origin());
  SettleTreeMotion(tree);
  views::test::RunScheduledLayout(widget.get());
  RunPendingMessages();
  views::test::RunScheduledLayout(widget.get());
  RunPendingMessages();

  auto* selected_row = tree->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(selected_row);
  EXPECT_EQ(page.id, model.selected_node_id());
  EXPECT_TRUE(scroll_view->GetVisibleRect().Contains(selected_row->bounds()));
  EXPECT_GT(scroll_view->GetVisibleRect().y(), viewport_before.y());
}

TEST_F(SidebarTreeViewTest,
       InterveningLayerScrollAwayAndBackCancelsDeferredSelectionReveal) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto page = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"After", "b");
  const auto trailing_first =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing first", "c");
  const auto trailing_second =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing second", "d");
  const auto trailing_third =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"Trailing third", "e");
  auto view = NewTreeView();
  auto* tree = view.get();
  auto scroll = std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled);
  auto* scroll_view = scroll.get();
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll->SetContents(std::move(view));
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 3 * SidebarTreeRowView::kRowHeight));
  widget->SetContentsView(std::move(scroll));
  widget->Show();
  auto scroll_synchronizer = scroll_view->EnableScrollSynchronization();
  ASSERT_TRUE(scroll_synchronizer);
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(
      std::nullopt,
      {folder, page, trailing_first, trailing_second, trailing_third}));
  ASSERT_TRUE(
      model.ReplaceChildren(folder.id, MakeMotionChildren(workspace, folder)));
  SettleTreeMotion(tree);
  views::test::RunScheduledLayout(widget.get());
  RunPendingMessages();
  tree->SynchronizeRowsForTesting(scroll_view->GetVisibleRect());
  const gfx::Rect viewport_before = scroll_view->GetVisibleRect();
  ASSERT_GE(tree->height(), 5 * SidebarTreeRowView::kRowHeight);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(scroll_view->GetVisibleRect());
  auto* moving_row = tree->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(moving_row);
  ASSERT_TRUE(viewport_before.Contains(moving_row->bounds()));
  ASSERT_TRUE(controller_->SelectNode(page.id));
  ASSERT_TRUE(tree->row_bounds_animation_running_for_testing());
  ASSERT_EQ(viewport_before.origin(), scroll_view->GetVisibleRect().origin());

  // No task/layout is drained between these calls. Merely comparing the final
  // origin would miss this user scroll; the scroll callback must cancel it.
  const int away_y = viewport_before.y() + SidebarTreeRowView::kRowHeight / 2;
  ASSERT_LE(away_y, tree->height() - viewport_before.height());
  scroll_view->ScrollToOffset(gfx::PointF(0, away_y));
  ASSERT_EQ(away_y, scroll_view->GetVisibleRect().y());
  scroll_view->ScrollToOffset(gfx::PointF(viewport_before.origin()));
  ASSERT_EQ(viewport_before.origin(), scroll_view->GetVisibleRect().origin());

  SettleTreeMotion(tree);
  views::test::RunScheduledLayout(widget.get());
  RunPendingMessages();
  views::test::RunScheduledLayout(widget.get());
  RunPendingMessages();

  EXPECT_EQ(page.id, model.selected_node_id());
  EXPECT_EQ(viewport_before.origin(), scroll_view->GetVisibleRect().origin());
  auto* selected_row = tree->GetMaterializedRowForTesting(page.id);
  ASSERT_TRUE(selected_row);
  EXPECT_EQ(4 * SidebarTreeRowView::kRowHeight, selected_row->y());
  EXPECT_FALSE(scroll_view->GetVisibleRect().Contains(selected_row->bounds()));
}

TEST_F(SidebarTreeViewTest, FolderChildrenFoldSymmetricallyAndReopenInPlace) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto after =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage,
               u"After", "b");
  const auto children = MakeMotionChildren(workspace, folder);
  auto view = NewTreeView();
  auto* tree = view.get();
  const auto height_container = BindHeightTestContainer(tree);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, after}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, children));
  const gfx::Rect viewport(0, 0, 240, 320);
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);

  std::vector<SidebarTreeRowView*> rows;
  for (const auto& child : children) {
    auto* row = tree->GetMaterializedRowForTesting(child.id);
    ASSERT_TRUE(row);
    EXPECT_EQ(SidebarTreeRowView::kRowHeight, row->y());
    EXPECT_EQ(0, row->height());
    rows.push_back(row);
  }
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration / 3);
  for (size_t i = 0; i < rows.size(); ++i) {
    EXPECT_GT(rows[i]->height(), 0);
    EXPECT_LT(rows[i]->height(), SidebarTreeRowView::kRowHeight);
    EXPECT_GE(rows[i]->y(), SidebarTreeRowView::kRowHeight);
    if (i + 1 < rows.size()) {
      // Independent integer Rect interpolation can round adjacent edges by 1.
      EXPECT_LE(rows[i]->bounds().bottom(), rows[i + 1]->y() + 1);
    }
  }
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetSelectedNode(children[1].id));
  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  tree->SynchronizeRowsForTesting(viewport);
  EXPECT_EQ(folder.id, model.selected_node_id());
  ui::AXActionData activate;
  activate.action = ax::mojom::Action::kDoDefault;
  for (size_t i = 0; i < rows.size(); ++i) {
    EXPECT_EQ(rows[i], tree->GetMaterializedRowForTesting(children[i].id));
    EXPECT_TRUE(rows[i]->is_exiting());
    EXPECT_EQ(views::View::FocusBehavior::NEVER, rows[i]->GetFocusBehavior());
    EXPECT_FALSE(rows[i]->HandleAccessibleAction(activate));
    ui::MouseEvent press(ui::EventType::kMousePressed, gfx::Point(30, 10),
                         gfx::Point(30, 10), base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
    EXPECT_FALSE(rows[i]->OnMousePressed(press));
  }
  AdvanceTreeMotion(tree, height_container.get(),
                    visual_style::kTreeMotionDuration / 3);
  const gfx::Rect intermediate = rows.front()->bounds();
  EXPECT_GT(intermediate.height(), 0);
  EXPECT_LT(intermediate.height(), SidebarTreeRowView::kRowHeight);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);
  EXPECT_EQ(intermediate, rows.front()->bounds());
  for (size_t i = 0; i < rows.size(); ++i) {
    EXPECT_EQ(rows[i], tree->GetMaterializedRowForTesting(children[i].id));
    EXPECT_FALSE(rows[i]->is_exiting());
  }
  SettleTreeMotion(tree);

  // Finish a collapse, but reopen before its queued cleanup executes.
  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);
  RunPendingMessages();
  for (size_t i = 0; i < rows.size(); ++i) {
    EXPECT_EQ(rows[i], tree->GetMaterializedRowForTesting(children[i].id));
  }
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  RunPendingMessages();
  for (const auto& child : children) {
    EXPECT_EQ(nullptr, tree->GetMaterializedRowForTesting(child.id));
  }
}

TEST_F(SidebarTreeViewTest, ReducedMotionCollapseDoesNotRetainExitRows) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  const auto workspace = MakeWorkspace();
  const auto folder =
      MakeNode(workspace, std::nullopt, tab_tree::TreeNodeType::kFolder,
               u"Project", "a");
  const auto children = MakeMotionChildren(workspace, folder);
  auto view = NewTreeView();
  auto* tree = view.get();
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 240, 320));
  widget->SetContentsView(std::move(view));
  widget->Show();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, children));
  const gfx::Rect viewport(0, 0, 240, 320);
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  tree->SynchronizeRowsForTesting(viewport);
  SettleTreeMotion(tree);
  render_mode.reset();
  const auto reduced = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  ASSERT_TRUE(reduced);
  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  tree->SynchronizeRowsForTesting(viewport);
  EXPECT_FALSE(tree->row_bounds_animation_running_for_testing());
  EXPECT_FALSE(tree->height_animation_for_testing()->is_animating());
  EXPECT_EQ(SidebarTreeRowView::kRowHeight, tree->GetPreferredSize().height());
  for (const auto& child : children) {
    EXPECT_EQ(nullptr, tree->GetMaterializedRowForTesting(child.id));
  }
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
