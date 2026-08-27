// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"

#include <string>

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

}  // namespace
}  // namespace ahoi::sidebar
