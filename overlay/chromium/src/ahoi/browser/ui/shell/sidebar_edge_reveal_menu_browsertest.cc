// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_menu.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_shelf_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

using Mode = ahoi::sidebar::SidebarPresentationMode;

void RunFor(base::TimeDelta delay) {
  base::RunLoop run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), delay);
  run_loop.Run();
}

ahoi::sidebar::SidebarBookmarkShelfView* FindShelf(views::View* view) {
  if (auto* shelf =
          views::AsViewClass<ahoi::sidebar::SidebarBookmarkShelfView>(view)) {
    return shelf;
  }
  for (views::View* child : view->children()) {
    if (auto* shelf = FindShelf(child)) {
      return shelf;
    }
  }
  return nullptr;
}

}  // namespace

class AhoiSidebarEdgeRevealMenuBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 protected:
  VerticalTabStripRegionView* region_view() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->vertical_tab_strip_region_view_for_testing();
  }

  views::View* sidebar() { return region_view()->ahoi_sidebar_tree_view(); }

  // Hides the sidebar, reveals it at the window edge and moves the real
  // pointer and keyboard focus out of it, so only a menu may hold it open.
  void RevealWithPointerAndFocusOutside() {
    BrowserView& browser_view =
        *BrowserView::GetBrowserViewForBrowser(browser());
    ASSERT_TRUE(browser_view.IsAhoiBrowserSurface());
    VerticalTabStripRegionView* const region = region_view();
    ASSERT_TRUE(region);
    ASSERT_TRUE(sidebar());
    views::Widget* const widget = browser_view.GetWidget();
    ASSERT_TRUE(widget);

    ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kHidden));
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return !sidebar()->GetVisible(); }));
    region->SetAhoiSidebarEdgeRevealed(true);
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return sidebar()->GetVisible(); }));
    ASSERT_TRUE(region->IsAhoiSidebarEdgeRevealed());

    const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
    if (region->GetBoundsInScreen().Contains(cursor)) {
      gfx::Rect window_bounds = widget->GetWindowBoundsInScreen();
      window_bounds.set_x(cursor.x() + 1);
      widget->SetBounds(window_bounds);
    }
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return !region->IsMouseHovered(); }));
    ClearFocus();
  }

  void ClearFocus() {
    if (views::FocusManager* const focus = region_view()->GetFocusManager()) {
      focus->ClearFocus();
    }
  }

  // The pointer leaves the sidebar for the open menu, which arms the hide
  // check; the sidebar must stay through three hide delays.
  void ExpectOpenMenuHoldsSidebar() {
    VerticalTabStripRegionView* const region = region_view();
    ASSERT_TRUE(ahoi::sidebar::IsBrowserSidebarMenuRunning(sidebar()));
    region->OnMouseExited(
        ui::MouseEvent(ui::EventType::kMouseExited, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
    RunFor(3 * ahoi::visual_style::kSidebarEdgeRevealHideDelay);
    EXPECT_TRUE(ahoi::sidebar::IsBrowserSidebarMenuRunning(sidebar()));
    EXPECT_FALSE(region->IsMouseHovered());
    EXPECT_TRUE(region->IsAhoiSidebarEdgeRevealed());
    EXPECT_TRUE(sidebar()->GetVisible());
  }

  // Call after closing the menu. No further pointer event: the hide check
  // that the open menu kept re-arming retracts the sidebar now.
  void ExpectSidebarRetractsAfterMenu() {
    VerticalTabStripRegionView* const region = region_view();
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return !ahoi::sidebar::IsBrowserSidebarMenuRunning(sidebar());
    }));
    // A closing menu may hand focus back to its anchor inside the sidebar;
    // focus is not what this test is about.
    ClearFocus();
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return !region->IsAhoiSidebarEdgeRevealed(); }));
    EXPECT_TRUE(region->IsAhoiSidebarHidden());
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return !sidebar()->GetVisible(); }));
  }
};

// Crest fc93f389: an edge-revealed hidden sidebar stays open while a context
// menu opened from it is showing, even with the pointer outside the sidebar
// (over the part of the menu that extends past it), and retracts on the
// first hide check after the menu has closed.
IN_PROC_BROWSER_TEST_F(AhoiSidebarEdgeRevealMenuBrowserTest,
                       RevealedSidebarStaysOpenUnderItsContextMenu) {
  ASSERT_NO_FATAL_FAILURE(RevealWithPointerAndFocusOutside());
  views::View* const host = sidebar();
  views::Widget* const widget = host->GetWidget();

  // The model outlives the runner the host owns; the scoped release below
  // runs first, also when an assertion ends the test early.
  ui::SimpleMenuModel model(/*delegate=*/nullptr);
  model.AddItem(1, u"Ahoi");
  auto runner =
      std::make_unique<views::MenuRunner>(&model, views::MenuRunner::NO_FLAGS);
  views::MenuRunner* const menu = runner.get();
  ahoi::sidebar::SetBrowserSidebarContextMenuRunnerForTesting(
      host, std::move(runner));
  base::ScopedClosureRunner release_menu(base::BindOnce(
      [](views::View* sidebar_host) {
        ahoi::sidebar::SetBrowserSidebarContextMenuRunnerForTesting(
            sidebar_host, nullptr);
      },
      host));
  menu->RunMenuAt(
      widget, /*button_controller=*/nullptr, host->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(menu->IsRunning());

  ASSERT_NO_FATAL_FAILURE(ExpectOpenMenuHoldsSidebar());
  EXPECT_TRUE(menu->IsRunning());

  menu->Cancel();
  ASSERT_NO_FATAL_FAILURE(ExpectSidebarRetractsAfterMenu());
}

// The bookmark folder menu runs in its own menu window outside the sidebar
// and is owned by the bookmark shelf, not the host's context menu runner. It
// holds the edge-revealed sidebar open the same way.
IN_PROC_BROWSER_TEST_F(AhoiSidebarEdgeRevealMenuBrowserTest,
                       RevealedSidebarStaysOpenUnderBookmarkFolderMenu) {
  bookmarks::BookmarkModel* const model =
      BookmarkModelFactory::GetForBrowserContext(browser()->profile());
  ASSERT_TRUE(model);
  bookmarks::test::WaitForBookmarkModelToLoad(model);
  const bookmarks::BookmarkNode* const folder =
      model->AddFolder(model->bookmark_bar_node(), 0, u"Ahoi");
  model->AddURL(folder, 0, u"Docs", GURL("https://example.test/docs"));

  ASSERT_NO_FATAL_FAILURE(RevealWithPointerAndFocusOutside());
  ahoi::sidebar::SidebarBookmarkShelfView* const shelf = FindShelf(sidebar());
  ASSERT_TRUE(shelf);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return shelf->bookmark_item_count_for_testing() > 0; }));
  auto* const button =
      views::AsViewClass<views::Button>(shelf->bookmark_item_at_for_testing(0));
  ASSERT_TRUE(button);

  views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  ASSERT_TRUE(shelf->folder_menu_for_testing());
  ASSERT_TRUE(shelf->folder_menu_for_testing()->menu_for_testing());

  ASSERT_NO_FATAL_FAILURE(ExpectOpenMenuHoldsSidebar());
  ASSERT_TRUE(shelf->folder_menu_for_testing());

  shelf->folder_menu_for_testing()->Cancel();
  ASSERT_NO_FATAL_FAILURE(ExpectSidebarRetractsAfterMenu());
}
