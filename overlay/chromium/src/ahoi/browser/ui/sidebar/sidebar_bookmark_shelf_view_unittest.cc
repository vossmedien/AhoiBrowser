// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_shelf_view.h"

#include <memory>

#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_context_menu.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_menu.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace ahoi::sidebar {
namespace {

class CapturedBookmarkNavigation : public bookmarks::BookmarkNavigationWrapper {
 public:
  CapturedBookmarkNavigation() { SetInstanceForTesting(this); }
  ~CapturedBookmarkNavigation() override { SetInstanceForTesting(nullptr); }
  base::WeakPtr<content::NavigationHandle> NavigateTo(
      NavigateParams* params) override {
    urls.push_back(params->url);
    dispositions.push_back(params->disposition);
    return nullptr;
  }
  std::vector<GURL> urls;
  std::vector<WindowOpenDisposition> dispositions;
};

class SidebarBookmarkShelfViewTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    ui::TestClipboard::CreateForCurrentThread();

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
    standalone_shelf_ =
        std::make_unique<SidebarBookmarkShelfView>(browser_.get());
    shelf_ = standalone_shelf_.get();
  }

  void TearDown() override {
    shelf_ = nullptr;
    host_ = nullptr;
    widget_.reset();
    standalone_shelf_.reset();
    if (browser_) {
      browser_->GetWindow()->Close();
    }
    browser_.reset();
    bookmark_service_ = nullptr;
    profile_.reset();
    BookmarkContextMenu::InstallPreRunCallback({});
    ui::Clipboard::DestroyClipboardForCurrentThread();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  bookmarks::BookmarkModel* bookmark_model() {
    return BookmarkModelFactory::GetForBrowserContext(profile_.get());
  }

  void RunPendingModelUpdates() { base::RunLoop().RunUntilIdle(); }

  void MountShelf() {
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->SetBounds(gfx::Rect(100, 100, 240, 320));
    host_ = widget_->SetContentsView(std::make_unique<views::View>());
    host_->AddChildView(std::move(standalone_shelf_));
    shelf_->SetBounds(0, 40, 240, visual_style::kBookmarkShelfHeight);
    widget_->Show();
    views::test::RunScheduledLayout(widget_.get());
  }

  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<Browser> browser_;
  raw_ptr<BookmarkMergedSurfaceService> bookmark_service_ = nullptr;
  std::unique_ptr<SidebarBookmarkShelfView> standalone_shelf_;
  raw_ptr<SidebarBookmarkShelfView> shelf_ = nullptr;
  raw_ptr<views::View> host_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
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
  EXPECT_EQ(u"Docs", url_button->GetText());

  bookmark_model()->AddURL(bookmark_model()->other_node(), 0, u"Archiv",
                           GURL("https://example.test/archive"));
  RunPendingModelUpdates();
  EXPECT_EQ(3u, shelf_->bookmark_item_count_for_testing());
}

TEST_F(SidebarBookmarkShelfViewTest, KeepsButtonIdentityOnRenameAndReorder) {
  const auto* first = bookmark_model()->AddFolder(
      bookmark_model()->bookmark_bar_node(), 0, u"First");
  const auto* second = bookmark_model()->AddFolder(
      bookmark_model()->bookmark_bar_node(), 1, u"Second");
  RunPendingModelUpdates();
  auto* first_button = shelf_->bookmark_item_at_for_testing(0);
  auto* second_button = shelf_->bookmark_item_at_for_testing(1);

  bookmark_model()->SetTitle(first, u"Renamed",
                             bookmarks::metrics::BookmarkEditSource::kOther);
  bookmark_model()->Move(second, bookmark_model()->bookmark_bar_node(), 0);
  RunPendingModelUpdates();

  EXPECT_EQ(second_button, shelf_->bookmark_item_at_for_testing(0));
  EXPECT_EQ(first_button, shelf_->bookmark_item_at_for_testing(1));
  EXPECT_EQ(u"Renamed",
            views::AsViewClass<views::LabelButton>(first_button)->GetText());
}

TEST_F(SidebarBookmarkShelfViewTest,
       FaviconUpdatePreservesFocusedOverflowItem) {
  const bookmarks::BookmarkNode* last = nullptr;
  for (size_t i = 0; i < 12; ++i) {
    last = bookmark_model()->AddURL(
        bookmark_model()->bookmark_bar_node(), i,
        u"Bookmark " + base::NumberToString16(i),
        GURL("https://example.test/" + base::NumberToString(i)));
  }
  RunPendingModelUpdates();
  MountShelf();
  auto* last_button = shelf_->bookmark_item_at_for_testing(11);
  last_button->RequestFocus();
  views::test::RunScheduledLayout(widget_.get());
  ASSERT_EQ(last_button, widget_->GetFocusManager()->GetFocusedView());
  const int offset = shelf_->scroll_view_for_testing()->GetVisibleRect().x();
  ASSERT_GT(offset, 0);

  shelf_->BookmarkNodeFaviconChanged(last);
  RunPendingModelUpdates();
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_EQ(last_button, shelf_->bookmark_item_at_for_testing(11));
  EXPECT_EQ(last_button, widget_->GetFocusManager()->GetFocusedView());
  EXPECT_EQ(offset, shelf_->scroll_view_for_testing()->GetVisibleRect().x());
}

TEST_F(SidebarBookmarkShelfViewTest,
       EmptyAndRemovedCollectionsKeepManagerAccess) {
  EXPECT_EQ(0u, shelf_->bookmark_item_count_for_testing());
  EXPECT_FALSE(shelf_->scroll_view_for_testing()->GetVisible());
  EXPECT_TRUE(shelf_->manager_button_for_testing()->GetVisible());
  const auto* folder = bookmark_model()->AddFolder(
      bookmark_model()->bookmark_bar_node(), 0, u"");
  RunPendingModelUpdates();
  ASSERT_EQ(1u, shelf_->bookmark_item_count_for_testing());
  auto* button = views::AsViewClass<views::LabelButton>(
      shelf_->bookmark_item_at_for_testing(0));
  ASSERT_TRUE(button);
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_UNNAMED_BOOKMARK_FOLDER),
            button->GetText());

  bookmark_model()->Remove(
      folder, bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  RunPendingModelUpdates();
  EXPECT_EQ(0u, shelf_->bookmark_item_count_for_testing());
  EXPECT_FALSE(shelf_->scroll_view_for_testing()->GetVisible());
  EXPECT_TRUE(shelf_->manager_button_for_testing()->GetVisible());
}

TEST_F(SidebarBookmarkShelfViewTest,
       BookmarkScrollingCannotStartWorkspaceSwipe) {
  MountShelf();
  EXPECT_FALSE(CanStartBrowserWorkspaceGesture(
      host_, shelf_->GetBoundsInScreen().CenterPoint()));
  gfx::Point below = host_->GetBoundsInScreen().CenterPoint();
  EXPECT_TRUE(CanStartBrowserWorkspaceGesture(host_, below));
  EXPECT_FALSE(CanStartBrowserWorkspaceGesture(host_, gfx::Point(-100, -100)));
  EXPECT_FALSE(CanStartBrowserWorkspaceGesture(nullptr, below));
}

TEST_F(SidebarBookmarkShelfViewTest,
       DefaultActivationDoesNotNavigateASavedTab) {
  const GURL destination("https://example.test/bookmark");
  bookmark_model()->AddURL(bookmark_model()->bookmark_bar_node(), 0,
                           u"Bookmark", destination);
  RunPendingModelUpdates();
  CapturedBookmarkNavigation navigation;
  auto* button = views::AsViewClass<views::LabelButton>(
      shelf_->bookmark_item_at_for_testing(0));
  ASSERT_TRUE(button);

  views::test::ButtonTestApi(button).NotifyDefaultMouseClick();

  ASSERT_EQ(1u, navigation.urls.size());
  EXPECT_EQ(destination, navigation.urls[0]);
  EXPECT_EQ(WindowOpenDisposition::NEW_FOREGROUND_TAB,
            navigation.dispositions[0]);

  ui::MouseEvent background(ui::EventType::kMouseReleased, gfx::PointF(),
                            gfx::PointF(), base::TimeTicks::Now(),
                            ui::EF_MIDDLE_MOUSE_BUTTON,
                            ui::EF_MIDDLE_MOUSE_BUTTON);
  views::test::ButtonTestApi(button).NotifyClick(background);
  ASSERT_EQ(2u, navigation.urls.size());
  EXPECT_EQ(WindowOpenDisposition::NEW_BACKGROUND_TAB,
            navigation.dispositions[1]);
}

TEST_F(SidebarBookmarkShelfViewTest, OverflowCuesAndFocusFollowAReorderedItem) {
  const bookmarks::BookmarkNode* first = nullptr;
  for (size_t i = 0; i < 12; ++i) {
    const auto* node =
        bookmark_model()->AddFolder(bookmark_model()->bookmark_bar_node(), i,
                                    u"Folder " + base::NumberToString16(i));
    if (!first) {
      first = node;
    }
  }
  RunPendingModelUpdates();
  MountShelf();
  ASSERT_TRUE(shelf_->trailing_overflow_for_testing()->IsDrawn());
  EXPECT_FALSE(shelf_->leading_overflow_for_testing()->IsDrawn());
  auto* focused = shelf_->bookmark_item_at_for_testing(0);
  focused->RequestFocus();

  bookmark_model()->Move(first, bookmark_model()->bookmark_bar_node(), 12);
  RunPendingModelUpdates();
  views::test::RunScheduledLayout(widget_.get());

  // A second layout can arrive before the first frame callback. A stale
  // callback must not consume the new request with its older layer geometry.
  bookmark_model()->SetTitle(
      bookmark_model()->bookmark_bar_node()->children()[0].get(),
      u"A much longer folder title before the focused item",
      bookmarks::metrics::BookmarkEditSource::kOther);
  RunPendingModelUpdates();
  views::test::RunScheduledLayout(widget_.get());

  ASSERT_TRUE(base::test::RunUntil([&] {
    return shelf_->scroll_view_for_testing()->GetVisibleRect().Contains(
        focused->bounds());
  }));
  EXPECT_EQ(focused, shelf_->bookmark_item_at_for_testing(11));
  EXPECT_EQ(focused, widget_->GetFocusManager()->GetFocusedView());
  EXPECT_TRUE(shelf_->leading_overflow_for_testing()->IsDrawn());
  EXPECT_FALSE(shelf_->trailing_overflow_for_testing()->IsDrawn());
}

TEST_F(SidebarBookmarkShelfViewTest, NativeContextActionCanDeleteItsOwnButton) {
  bookmark_model()->AddURL(bookmark_model()->bookmark_bar_node(), 0,
                           u"Bookmark", GURL("https://example.test/context"));
  RunPendingModelUpdates();
  MountShelf();
  auto* button = shelf_->bookmark_item_at_for_testing(0);
  bool opened = false;
  BookmarkContextMenu::InstallPreRunCallback(
      base::BindLambdaForTesting([&] { opened = true; }));
  shelf_->ShowContextMenuForView(button,
                                 button->GetBoundsInScreen().CenterPoint(),
                                 ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(base::test::RunUntil([&] { return opened; }));
  ASSERT_TRUE(shelf_->context_menu_for_testing());
  auto* menu = shelf_->context_menu_for_testing()->native_menu_for_testing();
  ASSERT_TRUE(menu);
  EXPECT_TRUE(menu->IsCommandEnabled(IDC_BOOKMARK_BAR_REMOVE));
  if (auto* bar_option =
          menu->menu()->GetMenuItemByID(IDC_BOOKMARK_BAR_SUBMENU)) {
    EXPECT_FALSE(bar_option->GetVisible());
  }

  menu->ExecuteCommand(IDC_BOOKMARK_BAR_REMOVE, 0);
  RunPendingModelUpdates();

  EXPECT_TRUE(bookmark_model()->bookmark_bar_node()->children().empty());
  EXPECT_EQ(0u, shelf_->bookmark_item_count_for_testing());
  EXPECT_TRUE(shelf_->manager_button_for_testing()->GetVisible());
}

TEST_F(SidebarBookmarkShelfViewTest,
       NestedContextIsConfiguredAfterClipboardCheck) {
  const auto* folder = bookmark_model()->AddFolder(
      bookmark_model()->bookmark_bar_node(), 0, u"Folder");
  bookmark_model()->AddURL(folder, 0, u"Child",
                           GURL("https://example.test/child"));
  RunPendingModelUpdates();
  MountShelf();
  auto* button = views::AsViewClass<views::LabelButton>(
      shelf_->bookmark_item_at_for_testing(0));
  views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  ASSERT_TRUE(shelf_->folder_menu_for_testing());
  auto* folder_menu = shelf_->folder_menu_for_testing();
  ASSERT_TRUE(folder_menu->menu_for_testing());
  auto items = folder_menu->menu_for_testing()->GetSubmenu()->GetMenuItems();
  ASSERT_FALSE(items.empty());
  auto* child = items.front();
  bool prepared = false;
  BookmarkContextMenu::InstallPreRunCallback(base::BindLambdaForTesting([&] {
    auto* context = folder_menu->context_menu_for_testing();
    ASSERT_TRUE(context);
    for (int id : {IDC_BOOKMARK_BAR_ALWAYS_SHOW, IDC_BOOKMARK_BAR_SUBMENU}) {
      if (auto* option = context->GetMenuItemByID(id)) {
        EXPECT_FALSE(option->GetVisible());
      }
    }
    prepared = true;
  }));
  EXPECT_TRUE(folder_menu->ShowContextMenu(
      child, child->GetCommand(), button->GetBoundsInScreen().CenterPoint(),
      ui::mojom::MenuSourceType::kKeyboard));
  ASSERT_TRUE(base::test::RunUntil([&] { return prepared; }));
  folder_menu->Cancel();
  RunPendingModelUpdates();
}

TEST_F(SidebarBookmarkShelfViewTest,
       ContextQueryRejectsANoLongerVisibleSource) {
  const auto* node = bookmark_model()->AddURL(
      bookmark_model()->bookmark_bar_node(), 0, u"Bookmark",
      GURL("https://example.test/context"));
  RunPendingModelUpdates();
  MountShelf();
  auto* button = shelf_->bookmark_item_at_for_testing(0);
  const gfx::Point point = button->GetBoundsInScreen().CenterPoint();
  shelf_->SetVisible(false);
  bool closed = false;
  SidebarBookmarkContextMenu context(
      browser_.get(), bookmark_service_,
      base::BindLambdaForTesting([&] { closed = true; }));

  context.RunAt(button, {node}, point, ui::mojom::MenuSourceType::kMouse);

  ASSERT_TRUE(base::test::RunUntil([&] { return closed; }));
  EXPECT_EQ(nullptr, context.native_menu_for_testing());
}

}  // namespace
}  // namespace ahoi::sidebar
