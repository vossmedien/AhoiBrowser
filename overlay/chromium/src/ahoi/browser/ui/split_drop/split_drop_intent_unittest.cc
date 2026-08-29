// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"

#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::split_drop {
namespace {

drag::SidebarTabDragPayload RuntimePayload(int handle) {
  return {.runtime_tab_handle = handle};
}

drag::SidebarTabDragPayload SavedPayload() {
  return {.saved_node_id = base::Uuid::GenerateRandomV4()};
}

SplitDropTabState Unsplit(int handle) {
  return {.tab_handle = handle, .split_order = {handle}};
}

SplitDropTabState Split(int handle,
                        split_tabs::SplitTabId split_id,
                        std::vector<int> order,
                        split_tabs::SplitTabLayout layout) {
  return {.tab_handle = handle,
          .split_id = split_id,
          .split_order = std::move(order),
          .split_layout = layout};
}

std::vector<SplitDropPane> TwoPanes() {
  return {{.pane_index = 0, .bounds = gfx::Rect(0, 0, 200, 300)},
          {.pane_index = 1, .bounds = gfx::Rect(200, 0, 200, 300)}};
}

std::vector<SplitDropPane> TwoStackedPanes() {
  return {{.pane_index = 0, .bounds = gfx::Rect(0, 0, 400, 150)},
          {.pane_index = 1, .bounds = gfx::Rect(0, 150, 400, 150)}};
}

std::vector<int> ExistingOrder(const DropIntent& intent) {
  std::vector<int> result;
  for (const DropOrderEntry& entry : intent.desired_order) {
    result.push_back(entry.is_source ? -1 : entry.existing_tab_handle);
  }
  return result;
}

TEST(SplitDropIntentTest, ClosedSavedTabMapsToOrderedTwoPaneSplit) {
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 400, 300)}};
  const auto intent = CalculateDropIntent(
      SavedPayload(), std::nullopt, Unsplit(10), 0, gfx::Point(2, 150), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropAction::kCreateOrAddToSplit, intent->action);
  EXPECT_EQ(DropZone::kLeft, intent->zone);
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide, intent->layout);
  EXPECT_EQ((std::vector<int>{-1, 10}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, SameTwoPaneRuntimeDropReordersToTargetSlot) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  const auto intent = CalculateDropIntent(
      RuntimePayload(1),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide), 1,
      gfx::Point(398, 150), TwoPanes());

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropAction::kReorderInSplit, intent->action);
  EXPECT_EQ((std::vector<int>{2, 1}), ExistingOrder(*intent));
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide, intent->layout);
  EXPECT_EQ(gfx::Rect(336, 4, 60, 292), intent->highlight_bounds);
}

TEST(SplitDropIntentTest, CrossSplitSourceIsRejected) {
  const split_tabs::SplitTabId source_split =
      split_tabs::SplitTabId::GenerateNew();
  const split_tabs::SplitTabId target_split =
      split_tabs::SplitTabId::GenerateNew();
  EXPECT_FALSE(
      CalculateDropIntent(RuntimePayload(1),
                          Split(1, source_split, {1, 2},
                                split_tabs::SplitTabLayout::kSideBySide),
                          Split(3, target_split, {3, 4},
                                split_tabs::SplitTabLayout::kSideBySide),
                          0, gfx::Point(100, 150), TwoPanes())
          .has_value());
}

TEST(SplitDropIntentTest, CrossAxisDropOnTwoPanesBuildsThreePaneMainLayout) {
  const split_tabs::SplitTabId target_split =
      split_tabs::SplitTabId::GenerateNew();
  const auto intent =
      CalculateDropIntent(RuntimePayload(9), Unsplit(9),
                          Split(11, target_split, {10, 11},
                                split_tabs::SplitTabLayout::kSideBySide),
                          1, gfx::Point(300, 2), TwoPanes());

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropZone::kTop, intent->zone);
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide, intent->layout);
  EXPECT_EQ(split_tabs::SplitTabArrangement::kMainStart, intent->arrangement);
  EXPECT_EQ((std::vector<int>{10, -1, 11}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, ThreePaneDropMapsToFourthGridSlot) {
  const split_tabs::SplitTabId target_split =
      split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 130, 300)},
      {.pane_index = 1, .bounds = gfx::Rect(130, 0, 140, 300)},
      {.pane_index = 2, .bounds = gfx::Rect(270, 0, 130, 300)}};
  const auto intent =
      CalculateDropIntent(RuntimePayload(29), Unsplit(29),
                          Split(21, target_split, {20, 21, 22},
                                split_tabs::SplitTabLayout::kSideBySide),
                          1, gfx::Point(200, 298), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropZone::kBottom, intent->zone);
  EXPECT_EQ((std::vector<int>{20, 21, -1, 22}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, FourPaneGridDropReordersExactSlot) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 200, 150)},
      {.pane_index = 1, .bounds = gfx::Rect(200, 0, 200, 150)},
      {.pane_index = 2, .bounds = gfx::Rect(0, 150, 200, 150)},
      {.pane_index = 3, .bounds = gfx::Rect(200, 150, 200, 150)}};
  const auto intent = CalculateDropIntent(
      RuntimePayload(4),
      Split(4, split_id, {1, 2, 3, 4}, split_tabs::SplitTabLayout::kSideBySide),
      Split(1, split_id, {1, 2, 3, 4}, split_tabs::SplitTabLayout::kSideBySide),
      0, gfx::Point(2, 75), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropAction::kReorderInSplit, intent->action);
  EXPECT_EQ((std::vector<int>{4, 1, 2, 3}), ExistingOrder(*intent));
  EXPECT_EQ(gfx::Rect(4, 4, 60, 142), intent->highlight_bounds);
}

TEST(SplitDropIntentTest, CenterIsNeutralUntilAnEdgeZoneIsEntered) {
  const gfx::Rect pane(0, 0, 200, 300);
  EXPECT_FALSE(ClassifyDropZone(gfx::Point(100, 150), pane).has_value());
  EXPECT_EQ(DropZone::kLeft, ClassifyDropZone(gfx::Point(2, 150), pane));
}

TEST(SplitDropIntentTest, RetainedZoneUsesHysteresisAndThenClears) {
  const gfx::Rect pane(0, 0, 200, 300);
  EXPECT_FALSE(ClassifyDropZone(gfx::Point(74, 150), pane).has_value());
  EXPECT_EQ(DropZone::kLeft,
            ClassifyDropZone(gfx::Point(74, 150), pane, DropZone::kLeft));
  EXPECT_FALSE(
      ClassifyDropZone(gfx::Point(90, 150), pane, DropZone::kLeft).has_value());
}

TEST(SplitDropIntentTest, SameTwoPaneTopEdgeChangesLayoutToStacked) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  const auto intent = CalculateDropIntent(
      RuntimePayload(1),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide), 1,
      gfx::Point(300, 2), TwoPanes());

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropZone::kTop, intent->zone);
  EXPECT_EQ(split_tabs::SplitTabLayout::kStacked, intent->layout);
  EXPECT_EQ((std::vector<int>{1, 2}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, SameTwoPaneFourEdgesHaveDistinctSemantics) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  const SplitDropTabState source =
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide);
  const SplitDropTabState target =
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide);

  const auto left = CalculateDropIntent(RuntimePayload(1), source, target, 1,
                                        gfx::Point(202, 150), TwoPanes());
  const auto right = CalculateDropIntent(RuntimePayload(1), source, target, 1,
                                         gfx::Point(398, 150), TwoPanes());
  const auto top = CalculateDropIntent(RuntimePayload(1), source, target, 1,
                                       gfx::Point(300, 2), TwoPanes());
  const auto bottom = CalculateDropIntent(RuntimePayload(1), source, target, 1,
                                          gfx::Point(300, 298), TwoPanes());
  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(right.has_value());
  ASSERT_TRUE(top.has_value());
  ASSERT_TRUE(bottom.has_value());

  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide, left->layout);
  EXPECT_EQ((std::vector<int>{1, 2}), ExistingOrder(*left));
  EXPECT_EQ(split_tabs::SplitTabLayout::kSideBySide, right->layout);
  EXPECT_EQ((std::vector<int>{2, 1}), ExistingOrder(*right));
  EXPECT_EQ(split_tabs::SplitTabLayout::kStacked, top->layout);
  EXPECT_EQ((std::vector<int>{1, 2}), ExistingOrder(*top));
  EXPECT_EQ(split_tabs::SplitTabLayout::kStacked, bottom->layout);
  EXPECT_EQ((std::vector<int>{2, 1}), ExistingOrder(*bottom));
}

TEST(SplitDropIntentTest,
     LeadingAndTrailingPanesUseTheSameFullDetachTargetSurface) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  const auto left = CalculateDropIntent(
      RuntimePayload(1),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide), 0,
      gfx::Point(60, 150), TwoPanes());
  const auto right = CalculateDropIntent(
      RuntimePayload(2),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kSideBySide), 1,
      gfx::Point(340, 150), TwoPanes());

  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(right.has_value());
  EXPECT_EQ(DropAction::kDetachFromSplit, left->action);
  EXPECT_EQ(DropAction::kDetachFromSplit, right->action);
  EXPECT_EQ(DropZone::kLeft, left->zone);
  EXPECT_EQ(DropZone::kRight, right->zone);
  EXPECT_EQ(left->highlight_bounds.size(), right->highlight_bounds.size());
  EXPECT_TRUE(left->highlight_bounds.Contains(gfx::Point(60, 150)));
  EXPECT_TRUE(right->highlight_bounds.Contains(gfx::Point(340, 150)));
}

TEST(SplitDropIntentTest, TopAndBottomPanesUseTheSameFullDetachTargetSurface) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  const auto top = CalculateDropIntent(
      RuntimePayload(1),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kStacked),
      Split(1, split_id, {1, 2}, split_tabs::SplitTabLayout::kStacked), 0,
      gfx::Point(200, 60), TwoStackedPanes());
  const auto bottom = CalculateDropIntent(
      RuntimePayload(2),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kStacked),
      Split(2, split_id, {1, 2}, split_tabs::SplitTabLayout::kStacked), 1,
      gfx::Point(200, 240), TwoStackedPanes());

  ASSERT_TRUE(top.has_value());
  ASSERT_TRUE(bottom.has_value());
  EXPECT_EQ(DropAction::kDetachFromSplit, top->action);
  EXPECT_EQ(DropAction::kDetachFromSplit, bottom->action);
  EXPECT_EQ(DropZone::kTop, top->zone);
  EXPECT_EQ(DropZone::kBottom, bottom->zone);
  EXPECT_EQ(top->highlight_bounds.size(), bottom->highlight_bounds.size());
  EXPECT_TRUE(top->highlight_bounds.Contains(gfx::Point(200, 60)));
  EXPECT_TRUE(bottom->highlight_bounds.Contains(gfx::Point(200, 240)));
}

TEST(SplitDropIntentTest, SameThreePaneCrossEdgeSelectsMainArrangement) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 130, 300)},
      {.pane_index = 1, .bounds = gfx::Rect(130, 0, 140, 300)},
      {.pane_index = 2, .bounds = gfx::Rect(270, 0, 130, 300)}};
  const auto intent =
      CalculateDropIntent(RuntimePayload(22),
                          Split(22, split_id, {20, 21, 22},
                                split_tabs::SplitTabLayout::kSideBySide),
                          Split(21, split_id, {20, 21, 22},
                                split_tabs::SplitTabLayout::kSideBySide),
                          1, gfx::Point(200, 2), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(split_tabs::SplitTabArrangement::kMainStart, intent->arrangement);
  EXPECT_EQ((std::vector<int>{20, 22, 21}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, SameThreePaneLinearDropMovesBeforeExactTarget) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 130, 300)},
      {.pane_index = 1, .bounds = gfx::Rect(130, 0, 140, 300)},
      {.pane_index = 2, .bounds = gfx::Rect(270, 0, 130, 300)}};
  const auto intent =
      CalculateDropIntent(RuntimePayload(22),
                          Split(22, split_id, {20, 21, 22},
                                split_tabs::SplitTabLayout::kSideBySide),
                          Split(20, split_id, {20, 21, 22},
                                split_tabs::SplitTabLayout::kSideBySide),
                          0, gfx::Point(2, 150), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropAction::kReorderInSplit, intent->action);
  EXPECT_EQ((std::vector<int>{22, 20, 21}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, FourPaneGridMovesSourceIntoExactHoveredSlot) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 200, 150)},
      {.pane_index = 1, .bounds = gfx::Rect(200, 0, 200, 150)},
      {.pane_index = 2, .bounds = gfx::Rect(0, 150, 200, 150)},
      {.pane_index = 3, .bounds = gfx::Rect(200, 150, 200, 150)}};
  const auto intent = CalculateDropIntent(
      RuntimePayload(4),
      Split(4, split_id, {1, 2, 3, 4}, split_tabs::SplitTabLayout::kSideBySide),
      Split(2, split_id, {1, 2, 3, 4}, split_tabs::SplitTabLayout::kSideBySide),
      1, gfx::Point(202, 75), panes);

  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(DropAction::kReorderInSplit, intent->action);
  EXPECT_EQ((std::vector<int>{1, 4, 2, 3}), ExistingOrder(*intent));
}

TEST(SplitDropIntentTest, FifthPaneIsRejectedWithoutChangingIntentState) {
  const split_tabs::SplitTabId split_id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SplitDropPane> panes = {
      {.pane_index = 0, .bounds = gfx::Rect(0, 0, 200, 150)},
      {.pane_index = 1, .bounds = gfx::Rect(200, 0, 200, 150)},
      {.pane_index = 2, .bounds = gfx::Rect(0, 150, 200, 150)},
      {.pane_index = 3, .bounds = gfx::Rect(200, 150, 200, 150)}};

  EXPECT_FALSE(
      CalculateDropIntent(RuntimePayload(9), Unsplit(9),
                          Split(1, split_id, {1, 2, 3, 4},
                                split_tabs::SplitTabLayout::kSideBySide),
                          0, gfx::Point(2, 75), panes)
          .has_value());
}

}  // namespace
}  // namespace ahoi::split_drop
