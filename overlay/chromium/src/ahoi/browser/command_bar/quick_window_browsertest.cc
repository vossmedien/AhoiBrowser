// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/quick_window.h"

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/session_prefs.h"
#include "base/test/scoped_feature_list.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_init_state.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/window_feature_controller/window_feature_controller.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::quick_window {
namespace {

class QuickWindowBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(QuickWindowBrowserTest,
                       CreatesEphemeralPopupWithSharedRegularProfile) {
  Profile* const profile = browser()->GetProfile();
  TabStripModel* const normal_tabs = browser()->GetTabStripModel();
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

  const gfx::Rect anchor_bounds = browser()->GetWindow()->GetBounds();
  Browser* const quick_browser =
      CreateAndShowQuickWindow(profile, anchor_bounds);
  ASSERT_TRUE(quick_browser);
  EXPECT_EQ(BrowserWindowInterface::TYPE_POPUP, quick_browser->GetType());
  EXPECT_EQ(profile, quick_browser->GetProfile());
  EXPECT_TRUE(quick_browser->GetProfile()->IsRegularProfile());
  EXPECT_FALSE(quick_browser->GetProfile()->IsOffTheRecord());
  const BrowserInitState* const init_state =
      BrowserInitState::From(quick_browser);
  const WindowFeatureController* const feature_controller =
      WindowFeatureController::From(quick_browser);
  ASSERT_TRUE(init_state);
  ASSERT_TRUE(feature_controller);
  EXPECT_TRUE(feature_controller->IsTrustedSource());
  EXPECT_TRUE(init_state->omit_from_session_restore());
  EXPECT_FALSE(init_state->should_trigger_session_restore());
  EXPECT_EQ(CalculateQuickWindowBounds(anchor_bounds),
            init_state->create_params().initial_bounds);
  EXPECT_EQ(BrowserWindowCreateParams::ValueSpecified::kSpecified,
            init_state->create_params().initial_origin_specified);
  EXPECT_EQ(1, quick_browser->GetTabStripModel()->count());
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
  Profile* const profile = browser()->GetProfile();
  TabStripModel* const normal_tabs = browser()->GetTabStripModel();
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
      CreateAndShowQuickWindow(profile, browser()->GetWindow()->GetBounds());
  ASSERT_TRUE(quick_browser);
  const GURL transfer_url(
      "data:text/html,<title>Ahoi%20Quick%20Window</title>transfer-state");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(quick_browser, transfer_url));
  content::WebContents* const moved_contents =
      quick_browser->GetTabStripModel()->GetActiveWebContents();
  tabs::TabInterface* const moved_tab =
      quick_browser->GetTabStripModel()->GetActiveTab();
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

class QuickWindowWebsiteSessionBrowserTest : public InProcessBrowserTest {
 public:
  QuickWindowWebsiteSessionBrowserTest() {
    features_.InitAndEnableFeature(session::kAhoiWorkspaceWebsiteSessions);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// Handoff 011 S7: a shared-session Quick Window page is reopened, not moved,
// when the target window shows a Workspace with its own website sessions.
IN_PROC_BROWSER_TEST_F(QuickWindowWebsiteSessionBrowserTest,
                       ReopensInsteadOfMovingIntoOwnWebsiteSessionWorkspace) {
  Profile* const profile = browser()->GetProfile();
  SessionBridge* const bridge = SessionBridgeFactory::GetForProfile(profile);
  ASSERT_TRUE(bridge);
  base::RunLoop bridge_ready;
  bridge->RunWhenReadyForTesting(bridge_ready.QuitClosure());
  bridge_ready.Run();
  const std::optional<base::Uuid> own_sessions =
      bridge->CreateWorkspace(u"Kunde", u"K", std::nullopt,
                              /*own_website_sessions=*/true);
  ASSERT_TRUE(own_sessions.has_value());
  ASSERT_TRUE(bridge->HasOwnWebsiteSessions(*own_sessions));
  ASSERT_TRUE(bridge->SetActiveWorkspaceForWindow(
      browser(), *own_sessions, WorkspaceActivationSource::kKeyboard));

  Browser* const quick_browser =
      CreateAndShowQuickWindow(profile, browser()->GetWindow()->GetBounds());
  ASSERT_TRUE(quick_browser);
  const GURL url("data:text/html,<title>Reopen</title>reopen-state");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(quick_browser, url));
  content::WebContents* const quick_contents =
      quick_browser->GetTabStripModel()->GetActiveWebContents();

  ui_test_utils::BrowserDestroyedObserver popup_closed(quick_browser);
  ASSERT_TRUE(MoveActiveTabToNormalWindow(quick_browser));
  popup_closed.Wait();

  TabStripModel* const normal_tabs = browser()->GetTabStripModel();
  content::WebContents* const reopened = normal_tabs->GetActiveWebContents();
  ASSERT_TRUE(reopened);
  EXPECT_NE(quick_contents, reopened);
  EXPECT_EQ(url, reopened->GetVisibleURL());
  tabs::TabInterface* const tab = normal_tabs->GetActiveTab();
  EXPECT_EQ(bridge->GetWorkspaceForTab(tab), *own_sessions);
}

// Handoff 146 #6: while `beforeunload` keeps the reopened Quick Window page
// open, repeating the adoption must not open it a second time.
IN_PROC_BROWSER_TEST_F(QuickWindowWebsiteSessionBrowserTest,
                       RepeatedAdoptionDoesNotReopenTwice) {
  Profile* const profile = browser()->GetProfile();
  SessionBridge* const bridge = SessionBridgeFactory::GetForProfile(profile);
  ASSERT_TRUE(bridge);
  base::RunLoop bridge_ready;
  bridge->RunWhenReadyForTesting(bridge_ready.QuitClosure());
  bridge_ready.Run();
  const std::optional<base::Uuid> own_sessions =
      bridge->CreateWorkspace(u"Kunde", u"K", std::nullopt,
                              /*own_website_sessions=*/true);
  ASSERT_TRUE(own_sessions.has_value());
  ASSERT_TRUE(bridge->SetActiveWorkspaceForWindow(
      browser(), *own_sessions, WorkspaceActivationSource::kKeyboard));

  Browser* const quick_browser =
      CreateAndShowQuickWindow(profile, browser()->GetWindow()->GetBounds());
  ASSERT_TRUE(quick_browser);
  const GURL url(
      "data:text/html,<script>onbeforeunload=e=>{e.preventDefault();"
      "return 'x'}</script>guarded");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(quick_browser, url));
  content::PrepContentsForBeforeUnloadTest(
      quick_browser->GetTabStripModel()->GetActiveWebContents());
  TabStripModel* const normal_tabs = browser()->GetTabStripModel();
  const int before = normal_tabs->count();

  ASSERT_TRUE(MoveActiveTabToNormalWindow(quick_browser));
  javascript_dialogs::AppModalDialogController* const dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  EXPECT_FALSE(CanMoveActiveTabToNormalWindow(quick_browser));
  EXPECT_FALSE(MoveActiveTabToNormalWindow(quick_browser));
  EXPECT_EQ(before + 1, normal_tabs->count());

  ui_test_utils::BrowserDestroyedObserver popup_closed(quick_browser);
  dialog->view()->AcceptAppModalDialog();
  popup_closed.Wait();
  EXPECT_EQ(before + 1, normal_tabs->count());
}

}  // namespace
}  // namespace ahoi::quick_window
