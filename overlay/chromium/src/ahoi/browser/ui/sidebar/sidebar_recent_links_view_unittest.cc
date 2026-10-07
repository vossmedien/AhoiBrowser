// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"

#include <string>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "components/vector_icons/vector_icons.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

views::Button* FindButton(views::View* view,
                          const std::u16string& name_prefix) {
  if (auto* button = views::AsViewClass<views::Button>(view)) {
    ui::AXNodeData data;
    button->GetViewAccessibility().GetAccessibleNodeData(&data);
    if (data.GetString16Attribute(ax::mojom::StringAttribute::kName)
            .starts_with(name_prefix)) {
      return button;
    }
  }
  for (views::View* child : view->children()) {
    if (auto* button = FindButton(child, name_prefix)) {
      return button;
    }
  }
  return nullptr;
}

std::vector<RecentGroupLink> UnvisitedPages(size_t count) {
  std::vector<RecentGroupLink> pages;
  for (size_t index = 0; index < count; ++index) {
    pages.push_back({.node_id = base::Uuid::GenerateRandomV4(),
                     .title = base::UTF8ToUTF16("Page " +
                                               std::to_string(index + 1)),
                     .url = GURL("https://example.test/same-url")});
  }
  return pages;
}

class SidebarRecentLinksViewTest : public views::ViewsTestBase {};

TEST_F(SidebarRecentLinksViewTest,
       UnvisitedDuplicateUrlsKeepIdentityAcrossPagesAndFaviconUpdates) {
  auto pages = UnvisitedPages(7);
  const base::Uuid seventh_id = pages.back().node_id;
  base::Uuid activated;
  size_t icon_requests = 0;
  auto view = CreateGroupRecentLinksView(
      std::move(pages),
      base::BindLambdaForTesting(
          [&](const base::Uuid& id) { activated = id; }),
      base::BindLambdaForTesting([](bool) {}),
      base::BindLambdaForTesting([&](const RecentGroupLink&) {
        ++icon_requests;
        return ui::ImageModel();
      }));
  EXPECT_EQ(6u, icon_requests);
  EXPECT_EQ(nullptr, FindButton(view.get(), u"Page 7,"));
  auto* next = FindButton(view.get(), u"Next");
  if (!next) {
    next = FindButton(view.get(), u"Weiter");
  }
  ASSERT_NE(nullptr, next);
  ASSERT_TRUE(next->GetEnabled());
  views::test::ButtonTestApi(next).NotifyDefaultMouseClick();
  EXPECT_EQ(7u, icon_requests);
  auto* seventh = FindButton(view.get(), u"Page 7, example.test");
  ASSERT_NE(nullptr, seventh);
  UpdateGroupRecentLinkFavicon(
      view.get(), GURL("https://example.test/same-url"),
      ui::ImageModel::FromVectorIcon(vector_icons::kSearchIcon));
  EXPECT_EQ(seventh, FindButton(view.get(), u"Page 7, example.test"));
  EXPECT_EQ(7u, icon_requests);
  views::test::ButtonTestApi(seventh).NotifyDefaultMouseClick();
  EXPECT_EQ(seventh_id, activated);
}

TEST_F(SidebarRecentLinksViewTest, LargeFolderRequestsOnlyVisibleIcons) {
  size_t icon_requests = 0;
  auto view = CreateGroupRecentLinksView(
      UnvisitedPages(10000),
      base::BindLambdaForTesting([](const base::Uuid&) {}),
      base::BindLambdaForTesting([](bool) {}),
      base::BindLambdaForTesting([&](const RecentGroupLink&) {
        ++icon_requests;
        return ui::ImageModel();
      }));
  EXPECT_EQ(6u, icon_requests);
  EXPECT_NE(nullptr, FindButton(view.get(), u"Page 6,"));
  EXPECT_EQ(nullptr, FindButton(view.get(), u"Page 7,"));
}

}  // namespace
}  // namespace ahoi::sidebar
