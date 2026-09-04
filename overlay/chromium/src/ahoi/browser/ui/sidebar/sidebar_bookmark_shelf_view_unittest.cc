// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_shelf_view.h"

#include <memory>

#include "ahoi/browser/ui/visual_style.h"
#include "base/run_loop.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace ahoi::sidebar {
namespace {

class SidebarBookmarkShelfViewTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();

    TestingProfile::Builder profile_builder;
    profile_builder.AddTestingFactory(
        TemplateURLServiceFactory::GetInstance(),
        TemplateURLServiceTestUtil::GetTemplateURLServiceTestingFactory());
    profile_builder.AddTestingFactory(
        BookmarkModelFactory::GetInstance(),
        BookmarkModelFactory::GetDefaultFactory());
    profile_builder.AddTestingFactory(
        ManagedBookmarkServiceFactory::GetInstance(),
        ManagedBookmarkServiceFactory::GetDefaultFactory());
    profile_builder.AddTestingFactory(
        BookmarkMergedSurfaceServiceFactory::GetInstance(),
        BookmarkMergedSurfaceServiceFactory::GetDefaultFactory());
    profile_ = profile_builder.Build();

    bookmark_service_ =
        BookmarkMergedSurfaceServiceFactory::GetForProfile(profile_.get());
    ASSERT_TRUE(bookmark_service_);
    bookmark_service_->LoadForTesting({});
    bookmarks::test::WaitForBookmarkModelToLoad(bookmark_model());

    Browser::CreateParams params(profile_.get(), true);
    params.window = new TestBrowserWindow();
    browser_ = Browser::DeprecatedCreateOwnedForTesting(params);
    shelf_ = std::make_unique<SidebarBookmarkShelfView>(browser_.get());
  }

  void TearDown() override {
    shelf_.reset();
    if (browser_) {
      browser_->GetWindow()->Close();
    }
    browser_.reset();
    profile_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  bookmarks::BookmarkModel* bookmark_model() {
    return BookmarkModelFactory::GetForBrowserContext(profile_.get());
  }

  void RunPendingModelUpdates() { base::RunLoop().RunUntilIdle(); }

  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<Browser> browser_;
  raw_ptr<BookmarkMergedSurfaceService> bookmark_service_ = nullptr;
  std::unique_ptr<SidebarBookmarkShelfView> shelf_;
};

TEST_F(SidebarBookmarkShelfViewTest,
       UsesIndependentHorizontalToolbarAndFixedManagerAction) {
  ui::AXNodeData accessibility;
  shelf_->GetViewAccessibility().GetAccessibleNodeData(&accessibility);
  EXPECT_EQ(ax::mojom::Role::kToolbar, accessibility.role);
  EXPECT_EQ(
      l10n_util::GetStringUTF16(IDS_ACCNAME_BOOKMARKS),
      accessibility.GetString16Attribute(ax::mojom::StringAttribute::kName));

  EXPECT_EQ(views::ScrollView::ScrollBarMode::kHiddenButEnabled,
            shelf_->scroll_view_for_testing()->GetHorizontalScrollBarMode());
  EXPECT_EQ(views::ScrollView::ScrollBarMode::kDisabled,
            shelf_->scroll_view_for_testing()->GetVerticalScrollBarMode());
  ASSERT_TRUE(shelf_->manager_button_for_testing());
  EXPECT_EQ(gfx::Size(visual_style::kBookmarkShelfItemSize,
                      visual_style::kBookmarkShelfItemSize),
            shelf_->manager_button_for_testing()->GetPreferredSize());
}

TEST_F(SidebarBookmarkShelfViewTest,
       ProjectsBookmarkBarFoldersUrlsAndOtherBookmarks) {
  const bookmarks::BookmarkNode* const folder = bookmark_model()->AddFolder(
      bookmark_model()->bookmark_bar_node(), 0, u"Arbeit");
  bookmark_model()->AddURL(folder, 0, u"Ahoi", GURL("https://ahoibrowser.org"));
  bookmark_model()->AddURL(bookmark_model()->bookmark_bar_node(), 1, u"Docs",
                           GURL("https://example.test/docs"));
  RunPendingModelUpdates();

  ASSERT_EQ(2u, shelf_->bookmark_item_count_for_testing());
  auto* folder_button = views::AsViewClass<views::LabelButton>(
      shelf_->bookmark_item_at_for_testing(0));
  ASSERT_TRUE(folder_button);
  EXPECT_EQ(u"Arbeit", folder_button->GetText());

  auto* url_button = views::AsViewClass<views::LabelButton>(
      shelf_->bookmark_item_at_for_testing(1));
  ASSERT_TRUE(url_button);
  EXPECT_TRUE(url_button->GetText().empty());

  bookmark_model()->AddURL(bookmark_model()->other_node(), 0, u"Archiv",
                           GURL("https://example.test/archive"));
  RunPendingModelUpdates();
  EXPECT_EQ(3u, shelf_->bookmark_item_count_for_testing());
}

}  // namespace
}  // namespace ahoi::sidebar
