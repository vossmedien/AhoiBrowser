// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/quick_window.h"

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::quick_window {
namespace {

class QuickWindowBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(QuickWindowBrowserTest,
                       CreatesEphemeralPopupWithSharedRegularProfile) {
  Profile* const profile = browser()->profile();
  TabStripModel* const normal_tabs = browser()->tab_strip_model();
  content::WebContents* const original_contents =
      normal_tabs->GetActiveWebContents();
  const int original_tab_count = normal_tabs->count();
  SessionBridge* const bridge = SessionBridgeFactory::GetForProfile(profile);
  ASSERT_TRUE(bridge);
  ASSERT_TRUE(bridge->is_operational());
  base::RunLoop bridge_ready;
  bridge->RunWhenReadyForTesting(bridge_ready.QuitClosure());
  bridge_ready.Run();
  ASSERT_TRUE(bridge->is_ready());
  ASSERT_EQ(original_contents,
            bridge->FindWebContentsForTab(
                bridge->FindTabByWebContents(original_contents)));
  const size_t original_tracked_windows = bridge->tracked_window_count();
  const size_t original_tracked_tabs = bridge->tracked_tab_count();

  const gfx::Rect anchor_bounds = browser()->window()->GetBounds();
  Browser* const quick_browser =
      CreateAndShowQuickWindow(profile, anchor_bounds);
  ASSERT_TRUE(quick_browser);
  EXPECT_TRUE(quick_browser->is_type_popup());
  EXPECT_EQ(profile, quick_browser->profile());
  EXPECT_TRUE(quick_browser->profile()->IsRegularProfile());
  EXPECT_FALSE(quick_browser->profile()->IsOffTheRecord());
  EXPECT_TRUE(quick_browser->is_trusted_source());
  EXPECT_TRUE(quick_browser->omit_from_session_restore());
  EXPECT_FALSE(quick_browser->should_trigger_session_restore());
  EXPECT_EQ(CalculateQuickWindowBounds(anchor_bounds),
            quick_browser->create_params().initial_bounds);
  EXPECT_EQ(Browser::ValueSpecified::kSpecified,
            quick_browser->create_params().initial_origin_specified);
  EXPECT_EQ(1, quick_browser->tab_strip_model()->count());
  EXPECT_EQ(original_tracked_windows, bridge->tracked_window_count());
  EXPECT_EQ(original_tracked_tabs, bridge->tracked_tab_count());

  CloseBrowserSynchronously(quick_browser);
  EXPECT_EQ(original_tab_count, normal_tabs->count());
  EXPECT_EQ(original_contents, normal_tabs->GetActiveWebContents());
  EXPECT_EQ(original_tracked_windows, bridge->tracked_window_count());
  EXPECT_EQ(original_tracked_tabs, bridge->tracked_tab_count());
  EXPECT_EQ(normal_tabs, bridge->FindTabStripModelForTab(
                             bridge->FindTabByWebContents(original_contents)));
}

IN_PROC_BROWSER_TEST_F(QuickWindowBrowserTest,
                       MovesExactPageIntoNormalWindowAndClosesPopup) {
  Profile* const profile = browser()->profile();
  TabStripModel* const normal_tabs = browser()->tab_strip_model();
  content::WebContents* const original_contents =
      normal_tabs->GetActiveWebContents();
  const int original_tab_count = normal_tabs->count();
  SessionBridge* const bridge = SessionBridgeFactory::GetForProfile(profile);
  ASSERT_TRUE(bridge);
  ASSERT_TRUE(bridge->is_operational());
  base::RunLoop bridge_ready;
  bridge->RunWhenReadyForTesting(bridge_ready.QuitClosure());
  bridge_ready.Run();
  ASSERT_TRUE(bridge->is_ready());
  const size_t original_tracked_tabs = bridge->tracked_tab_count();

  Browser* const quick_browser =
      CreateAndShowQuickWindow(profile, browser()->window()->GetBounds());
  ASSERT_TRUE(quick_browser);
  const GURL transfer_url(
      "data:text/html,<title>Ahoi%20Quick%20Window</title>transfer-state");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(quick_browser, transfer_url));
  content::WebContents* const moved_contents =
      quick_browser->tab_strip_model()->GetActiveWebContents();
  tabs::TabInterface* const moved_tab =
      quick_browser->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(moved_contents);
  ASSERT_TRUE(moved_tab);
  EXPECT_EQ(nullptr, bridge->FindTabByWebContents(moved_contents));

  ui_test_utils::BrowserDestroyedObserver popup_closed(quick_browser);
  ASSERT_TRUE(MoveActiveTabToNormalWindow(quick_browser));
  popup_closed.Wait();

  EXPECT_EQ(original_tab_count + 1, normal_tabs->count());
  EXPECT_NE(TabStripModel::kNoTab,
            normal_tabs->GetIndexOfWebContents(original_contents));
  EXPECT_EQ(moved_contents, normal_tabs->GetActiveWebContents());
  EXPECT_EQ(transfer_url, moved_contents->GetLastCommittedURL());
  EXPECT_EQ(moved_tab, bridge->FindTabByWebContents(moved_contents));
  EXPECT_EQ(normal_tabs, bridge->FindTabStripModelForTab(moved_tab));
  EXPECT_EQ(original_tracked_tabs + 1, bridge->tracked_tab_count());
}

}  // namespace
}  // namespace ahoi::quick_window
