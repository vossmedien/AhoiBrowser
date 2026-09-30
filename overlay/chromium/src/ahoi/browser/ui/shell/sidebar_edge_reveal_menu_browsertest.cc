// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/test/base/in_process_browser_test.h"
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
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

using Mode = ahoi::sidebar::SidebarPresentationMode;

void RunFor(base::TimeDelta delay) {
  base::RunLoop run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), delay);
  run_loop.Run();
}

}  // namespace

class AhoiSidebarEdgeRevealMenuBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 protected:
  VerticalTabStripRegionView* region_view() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->vertical_tab_strip_region_view_for_testing();
  }
};

// Crest fc93f389: an edge-revealed hidden sidebar stays open while a context
// menu opened from it is showing, even with the pointer outside the sidebar
// (over the part of the menu that extends past it), and retracts on the
// first hide check after the menu has closed.
IN_PROC_BROWSER_TEST_F(AhoiSidebarEdgeRevealMenuBrowserTest,
                       RevealedSidebarStaysOpenUnderItsContextMenu) {
  BrowserView& browser_view = *BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view.IsAhoiBrowserSurface());
  VerticalTabStripRegionView* const region = region_view();
  ASSERT_TRUE(region);
  views::View* const sidebar = region->ahoi_sidebar_tree_view();
  ASSERT_TRUE(sidebar);
  views::Widget* const widget = browser_view.GetWidget();
  ASSERT_TRUE(widget);

  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kHidden));
  ASSERT_TRUE(base::test::RunUntil([&]() { return !sidebar->GetVisible(); }));
  region->SetAhoiSidebarEdgeRevealed(true);
  ASSERT_TRUE(base::test::RunUntil([&]() { return sidebar->GetVisible(); }));
  ASSERT_TRUE(region->IsAhoiSidebarEdgeRevealed());

  // Only the menu may hold the sidebar open: the real pointer must not rest
  // on the revealed sidebar and keyboard focus must be elsewhere.
  const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
  if (region->GetBoundsInScreen().Contains(cursor)) {
    gfx::Rect window_bounds = widget->GetWindowBoundsInScreen();
    window_bounds.set_x(cursor.x() + 1);
    widget->SetBounds(window_bounds);
  }
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !region->IsMouseHovered(); }));
  if (views::FocusManager* const focus = region->GetFocusManager()) {
    focus->ClearFocus();
  }

  // The model outlives the runner the host owns; the scoped release below
  // runs first, also when an assertion ends the test early.
  ui::SimpleMenuModel model(/*delegate=*/nullptr);
  model.AddItem(1, u"Ahoi");
  auto runner =
      std::make_unique<views::MenuRunner>(&model, views::MenuRunner::NO_FLAGS);
  views::MenuRunner* const menu = runner.get();
  ahoi::sidebar::SetBrowserSidebarContextMenuRunnerForTesting(
      sidebar, std::move(runner));
  base::ScopedClosureRunner release_menu(base::BindOnce(
      [](views::View* host) {
        ahoi::sidebar::SetBrowserSidebarContextMenuRunnerForTesting(host,
                                                                    nullptr);
      },
      sidebar));
  menu->RunMenuAt(
      widget, /*button_controller=*/nullptr, sidebar->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(menu->IsRunning());
  ASSERT_TRUE(ahoi::sidebar::IsBrowserSidebarMenuRunning(sidebar));

  // The pointer leaves the sidebar for the menu: this arms the hide check.
  region->OnMouseExited(
      ui::MouseEvent(ui::EventType::kMouseExited, gfx::Point(), gfx::Point(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  RunFor(3 * ahoi::visual_style::kSidebarEdgeRevealHideDelay);
  EXPECT_TRUE(menu->IsRunning());
  EXPECT_FALSE(region->IsMouseHovered());
  EXPECT_TRUE(region->IsAhoiSidebarEdgeRevealed());
  EXPECT_TRUE(sidebar->GetVisible());

  menu->Cancel();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !ahoi::sidebar::IsBrowserSidebarMenuRunning(sidebar); }));
  // No further pointer event: the hide check that the open menu kept
  // re-arming retracts the sidebar now.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !region->IsAhoiSidebarEdgeRevealed(); }));
  EXPECT_TRUE(region->IsAhoiSidebarHidden());
  ASSERT_TRUE(base::test::RunUntil([&]() { return !sidebar->GetVisible(); }));
}
