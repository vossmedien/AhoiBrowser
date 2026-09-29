// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_workspace_header_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "components/vector_icons/vector_icons.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

class SidebarActionViewsTest : public views::ViewsTestBase {};

TEST_F(SidebarActionViewsTest, HeaderActionIsSquareSemanticButton) {
  const std::u16string accessible_name = u"Sidebar ausblenden";
  std::unique_ptr<views::View> button = CreateSidebarHeaderActionButton(
      base::BindRepeating([](const ui::Event&) {}), vector_icons::kCloseIcon,
      accessible_name);

  EXPECT_EQ(gfx::Size(visual_style::kSidebarHeaderActionSize,
                      visual_style::kSidebarHeaderActionSize),
            button->GetPreferredSize());
  EXPECT_EQ(nullptr, button->GetBorder());

  ui::AXNodeData accessibility;
  button->GetViewAccessibility().GetAccessibleNodeData(&accessibility);
  EXPECT_EQ(ax::mojom::Role::kButton, accessibility.role);
  EXPECT_EQ(accessible_name, accessibility.GetString16Attribute(
                                 ax::mojom::StringAttribute::kName));
}

TEST_F(SidebarActionViewsTest, HeaderActionTogglePublishesCheckedState) {
  std::unique_ptr<views::View> button = CreateSidebarHeaderActionButton(
      base::BindRepeating([](const ui::Event&) {}),
      vector_icons::kCloseIcon, u"Schwebende Sidebar");

  SetSidebarHeaderActionToggleState(button.get(), false);
  ui::AXNodeData docked_accessibility;
  button->GetViewAccessibility().GetAccessibleNodeData(
      &docked_accessibility);
  EXPECT_EQ(ax::mojom::Role::kToggleButton, docked_accessibility.role);
  EXPECT_EQ(ax::mojom::CheckedState::kFalse,
            docked_accessibility.GetCheckedState());

  SetSidebarHeaderActionToggleState(button.get(), true);
  ui::AXNodeData floating_accessibility;
  button->GetViewAccessibility().GetAccessibleNodeData(
      &floating_accessibility);
  EXPECT_EQ(ax::mojom::Role::kToggleButton, floating_accessibility.role);
  EXPECT_EQ(ax::mojom::CheckedState::kTrue,
            floating_accessibility.GetCheckedState());
}

TEST_F(SidebarActionViewsTest, SectionDividerUsesCompactSemanticGeometry) {
  const std::u16string action_name = u"Alle entfernen";
  std::unique_ptr<views::View> divider = CreateSidebarSectionDivider(
      base::BindRepeating([](const ui::Event&) {}), action_name);

  EXPECT_EQ(gfx::Size(0, visual_style::kSidebarSectionDividerHeight),
            divider->GetPreferredSize());
  ASSERT_EQ(2u, divider->children().size());
  EXPECT_NE(nullptr,
            views::AsViewClass<views::Separator>(divider->children()[0]));
  auto* action = views::AsViewClass<views::LabelButton>(divider->children()[1]);
  ASSERT_NE(nullptr, action);
  EXPECT_EQ(action_name, action->GetText());
  EXPECT_EQ(gfx::Insets::VH(
                0, visual_style::kSidebarSectionDividerActionHorizontalInset),
            action->GetInsets());
  EXPECT_EQ(nullptr, action->GetBackground());
}


// Default sidebar: 212 content width, one search action and the two
// presentation actions that the selector's menu also offers.
WorkspaceHeaderMetrics DefaultHeaderMetrics(int selector_width) {
  return {.available_width = 212,
          .selector_width = selector_width,
          .indicators_width = 44,
          .essential_actions = 1,
          .collapsible_actions = 2,
          .action_width = visual_style::kSidebarHeaderActionSize,
          .action_spacing = visual_style::kSidebarHeaderActionSpacing,
          .selector_spacing = visual_style::kSidebarFooterSpacing};
}

TEST_F(SidebarActionViewsTest, WorkspaceHeaderGivesTheNameRoomFirst) {
  // A short name fits beside the dots and every action.
  EXPECT_EQ((WorkspaceHeaderPlan{0, true, true}),
            PlanWorkspaceHeader(DefaultHeaderMetrics(40)));
  // Then the floating toggle moves into the menu...
  EXPECT_EQ((WorkspaceHeaderPlan{1, true, true}),
            PlanWorkspaceHeader(DefaultHeaderMetrics(90)));
  // ...then the dots hide...
  EXPECT_EQ((WorkspaceHeaderPlan{1, false, true}),
            PlanWorkspaceHeader(DefaultHeaderMetrics(120)));
  // ...then the hide action; search always stays.
  EXPECT_EQ((WorkspaceHeaderPlan{2, false, true}),
            PlanWorkspaceHeader(DefaultHeaderMetrics(160)));
  // Only a name wider than all that elides, in the most compact row.
  EXPECT_EQ((WorkspaceHeaderPlan{2, false, false}),
            PlanWorkspaceHeader(DefaultHeaderMetrics(400)));
}

TEST_F(SidebarActionViewsTest, WorkspaceHeaderWithoutCollapsibleActions) {
  WorkspaceHeaderMetrics metrics = DefaultHeaderMetrics(150);
  metrics.collapsible_actions = 0;
  EXPECT_EQ((WorkspaceHeaderPlan{0, false, true}),
            PlanWorkspaceHeader(metrics));
  metrics.selector_width = 300;
  EXPECT_EQ((WorkspaceHeaderPlan{0, false, false}),
            PlanWorkspaceHeader(metrics));
}

TEST_F(SidebarActionViewsTest, WorkspaceHeaderLaysOutTheFullName) {
  SidebarWorkspaceHeaderView header;
  auto* host = header.AddChildView(std::make_unique<views::View>());
  views::Button* const selector = host->AddChildView(
      CreateWorkspaceSelectorButton(views::Button::PressedCallback()));
  const auto add_action = [&header]() {
    return header.AddChildView(CreateSidebarHeaderActionButton(
        base::BindRepeating([](const ui::Event&) {}),
        vector_icons::kCloseIcon, u"Aktion"));
  };
  add_action();
  views::View* const floating = add_action();
  views::View* const hide = add_action();
  header.SetSelector(host, selector);
  header.MarkCollapsibleAction(floating);
  header.MarkCollapsibleAction(hide);

  // A wide sidebar fits a short name beside every action.
  SetWorkspaceSelectorPresentation(selector, u"Ahoi", u"A", std::nullopt);
  header.SetBounds(0, 0, 320, visual_style::kSidebarActionCellHeight);
  views::test::RunScheduledLayout(&header);
  EXPECT_TRUE(header.plan_for_testing().name_fits);
  EXPECT_TRUE(floating->GetVisible());
  EXPECT_TRUE(hide->GetVisible());
  EXPECT_GE(host->width(), GetWorkspaceSelectorPreferredWidth(
                               selector, /*with_indicators=*/false));
  EXPECT_TRUE(selector->GetTooltipText().empty());

  const std::u16string long_name =
      u"Kundenprojekte und Recherche für das gesamte Jahr 2026";
  SetWorkspaceSelectorPresentation(selector, long_name, u"K", std::nullopt);
  views::test::RunScheduledLayout(&header);
  EXPECT_FALSE(header.plan_for_testing().name_fits);
  EXPECT_FALSE(floating->GetVisible());
  EXPECT_FALSE(hide->GetVisible());
  // The elided name stays available as tooltip and accessible name.
  EXPECT_EQ(long_name, selector->GetTooltipText());
  ui::AXNodeData accessibility;
  selector->GetViewAccessibility().GetAccessibleNodeData(&accessibility);
  EXPECT_NE(std::u16string::npos,
            accessibility
                .GetString16Attribute(ax::mojom::StringAttribute::kName)
                .find(long_name));
}

}  // namespace
}  // namespace ahoi::sidebar
