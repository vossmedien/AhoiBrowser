// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

class SidebarRuntimeDropTargetsTest : public views::ViewsTestBase {};

TEST_F(SidebarRuntimeDropTargetsTest,
       SplitPaneSelfDropOnlyDetachesAtItsOuterEdges) {
  EXPECT_TRUE(
      CanDetachRuntimeSplitPaneOnSelfDrop(true, OpenTabDropPosition::kBefore));
  EXPECT_TRUE(
      CanDetachRuntimeSplitPaneOnSelfDrop(true, OpenTabDropPosition::kAfter));
  EXPECT_FALSE(
      CanDetachRuntimeSplitPaneOnSelfDrop(true, OpenTabDropPosition::kSplit));
  EXPECT_FALSE(
      CanDetachRuntimeSplitPaneOnSelfDrop(false, OpenTabDropPosition::kBefore));
  EXPECT_FALSE(
      CanDetachRuntimeSplitPaneOnSelfDrop(false, OpenTabDropPosition::kAfter));
}

TEST_F(SidebarRuntimeDropTargetsTest,
       CompositePaneDragKeepsSavedOrRuntimeIdentity) {
  const base::Uuid saved_node_id = base::Uuid::GenerateRandomV4();
  ui::OSExchangeData saved_data;
  WriteOpenTabDragPayload(&saved_data, saved_node_id, 17, u"Saved");
  const std::optional<drag::SidebarTabDragPayload> saved_payload =
      drag::ReadSidebarTabDragPayload(saved_data);
  ASSERT_TRUE(saved_payload.has_value());
  EXPECT_EQ(saved_node_id, saved_payload->saved_node_id);
  EXPECT_FALSE(saved_payload->runtime_tab_handle.has_value());

  ui::OSExchangeData runtime_data;
  WriteOpenTabDragPayload(&runtime_data, std::nullopt, 17, u"Temporary");
  const std::optional<drag::SidebarTabDragPayload> runtime_payload =
      drag::ReadSidebarTabDragPayload(runtime_data);
  ASSERT_TRUE(runtime_payload.has_value());
  EXPECT_FALSE(runtime_payload->saved_node_id.has_value());
  EXPECT_EQ(17, runtime_payload->runtime_tab_handle);
}

TEST_F(SidebarRuntimeDropTargetsTest,
       OpenTabsTargetPaintsOnlyWhileHoveredAndDoesNotAffectNewGroup) {
  auto target = CreateOpenTabsDropTargetView(
      base::BindRepeating([](const base::Uuid&) { return true; }));
  auto new_group = CreateNewGroupDropTargetViewForTesting(u"Neue Gruppe");
  SetNewGroupDropTargetVisible(new_group.get(), true);

  EXPECT_FALSE(IsOpenTabsDropTargetAcceptingTabForTesting(target.get()));
  EXPECT_FALSE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_EQ(nullptr, target->GetBackground());
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            target->GetPreferredSize().height());

  SetOpenTabsDropTargetAcceptingTab(target.get(), true);
  EXPECT_TRUE(IsOpenTabsDropTargetAcceptingTabForTesting(target.get()));
  EXPECT_FALSE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_EQ(nullptr, target->GetBackground());
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            target->GetPreferredSize().height());

  ui::OSExchangeData drag_data;
  ui::DropTargetEvent drag_event(drag_data, gfx::PointF(), gfx::PointF(),
                                 ui::DragDropTypes::DRAG_MOVE);
  target->OnDragEntered(drag_event);
  EXPECT_TRUE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_NE(nullptr, target->GetBackground());
  target->OnDragExited();
  EXPECT_FALSE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_EQ(nullptr, target->GetBackground());

  SetOpenTabsDropTargetAcceptingTab(target.get(), false);
  EXPECT_FALSE(IsOpenTabsDropTargetAcceptingTabForTesting(target.get()));
  EXPECT_FALSE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_EQ(nullptr, target->GetBackground());
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            target->GetPreferredSize().height());

  // The saved/open targets have independent presentation state. Toggling this
  // surface must not hide or disable the New Group hit target during the same
  // native drag.
  EXPECT_TRUE(new_group->GetVisible());
  EXPECT_TRUE(new_group->GetCanProcessEventsWithinSubtree());
}

TEST_F(SidebarRuntimeDropTargetsTest,
       OpenTabsTargetAcceptsOnlyValidatedRuntimeSplitPayload) {
  auto target = CreateOpenTabsDropTargetView(
      base::BindRepeating([](const drag::SidebarTabDragPayload& payload) {
        return payload.runtime_tab_handle == 17;
      }),
      base::BindRepeating(
          [](const drag::SidebarTabDragPayload& payload) {
            return payload.runtime_tab_handle == 17;
          }));
  SetOpenTabsDropTargetAcceptingTab(target.get(), true);

  ui::OSExchangeData accepted_data;
  WriteOpenTabDragPayload(&accepted_data, std::nullopt, 17, u"Split pane");
  ui::DropTargetEvent accepted_event(
      accepted_data, gfx::PointF(), gfx::PointF(),
      ui::DragDropTypes::DRAG_MOVE);
  EXPECT_TRUE(target->CanDrop(accepted_data));
  target->OnDragEntered(accepted_event);
  EXPECT_TRUE(IsOpenTabsDropTargetHighlightedForTesting(target.get()));
  EXPECT_EQ(ui::DragDropTypes::DRAG_MOVE,
            target->OnDragUpdated(accepted_event));
  EXPECT_FALSE(target->GetDropCallback(accepted_event).is_null());

  ui::OSExchangeData rejected_data;
  WriteOpenTabDragPayload(&rejected_data, std::nullopt, 23, u"Single tab");
  ui::DropTargetEvent rejected_event(
      rejected_data, gfx::PointF(), gfx::PointF(),
      ui::DragDropTypes::DRAG_MOVE);
  EXPECT_FALSE(target->CanDrop(rejected_data));
  EXPECT_EQ(ui::DragDropTypes::DRAG_NONE,
            target->OnDragUpdated(rejected_event));
  EXPECT_TRUE(target->GetDropCallback(rejected_event).is_null());
}

TEST_F(SidebarRuntimeDropTargetsTest,
       NewGroupFadeLayerIsTransparentAndBorderless) {
  auto target = CreateNewGroupDropTargetViewForTesting(u"Neue Gruppe");

  ASSERT_NE(nullptr, target->layer());
  EXPECT_FALSE(target->layer()->fills_bounds_opaquely());
  EXPECT_EQ(nullptr, target->GetBorder());
  EXPECT_FALSE(target->GetVisible());
  EXPECT_FALSE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());

  SetNewGroupDropTargetVisible(target.get(), true);
  EXPECT_TRUE(target->GetVisible());
  EXPECT_TRUE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(nullptr, target->GetBorder());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());

  SetNewGroupDropTargetVisible(target.get(), false);
  EXPECT_FALSE(target->GetVisible());
  EXPECT_FALSE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(0.0f, target->layer()->opacity());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());
}

TEST_F(SidebarRuntimeDropTargetsTest,
       NewGroupOverlaysWorkspaceWithoutChangingGeometry) {
  views::View host;
  host.SetBounds(0, 0, 320, visual_style::kSidebarActionCellHeight);
  host.SetLayoutManager(std::make_unique<views::FillLayout>());

  auto workspace = std::make_unique<views::View>();
  workspace->SetPreferredSize(
      gfx::Size(0, visual_style::kSidebarActionCellHeight));
  views::View* const workspace_ptr = host.AddChildView(std::move(workspace));
  auto target = CreateNewGroupDropTargetViewForTesting(u"Neue Gruppe");
  views::View* const target_ptr = target.get();
  host.AddChildView(std::move(target));
  host.DeprecatedLayoutImmediately();

  const gfx::Rect stable_workspace_bounds = workspace_ptr->bounds();
  EXPECT_EQ(gfx::Rect(0, 0, 320, visual_style::kSidebarActionCellHeight),
            stable_workspace_bounds);
  SetNewGroupDropTargetVisible(target_ptr, true);
  host.DeprecatedLayoutImmediately();

  EXPECT_TRUE(workspace_ptr->GetVisible());
  EXPECT_TRUE(workspace_ptr->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(stable_workspace_bounds, workspace_ptr->bounds());
  EXPECT_EQ(stable_workspace_bounds, target_ptr->bounds());
  EXPECT_TRUE(target_ptr->GetCanProcessEventsWithinSubtree());

  SetNewGroupDropTargetVisible(target_ptr, false);
  EXPECT_FALSE(target_ptr->GetVisible());
  EXPECT_EQ(stable_workspace_bounds, workspace_ptr->bounds());
}

}  // namespace ahoi::sidebar
