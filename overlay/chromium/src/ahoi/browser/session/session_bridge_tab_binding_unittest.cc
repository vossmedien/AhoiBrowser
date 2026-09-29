// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// SessionBridge tests for tab activation across Workspaces and for the
// binding between native window tabs and saved pages: discard, window
// moves, dropped detached tabs and shutdown (split from
// session_bridge_unittest.cc, source line budget).

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/command_service_factory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/session_bridge_unittest_support.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/website_session_context.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi {

namespace {

using test_support::MakeSavedPage;
using test_support::MakeWorkspace;
using test_support::SessionBridgeTest;

TEST_F(SessionBridgeTest, ActivateTabInItsWorkspaceSelectsWorkspaceFirst) {
  tab_tree::Workspace primary = MakeWorkspace(u"Primary", "a");
  tab_tree::Workspace secondary = MakeWorkspace(u"Secondary", "b");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({secondary, primary}));
  task_environment()->RunUntilIdle();

  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), secondary.id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/secondary"));
  task_environment()->RunUntilIdle();
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* secondary_tab = model->GetActiveTab();
  ASSERT_TRUE(secondary_tab);
  ASSERT_EQ(bridge_->GetWorkspaceForTab(secondary_tab), secondary.id);

  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), primary.id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/primary"));
  task_environment()->RunUntilIdle();
  ASSERT_NE(model->GetActiveTab(), secondary_tab);
  ASSERT_EQ(bridge_->GetActiveWorkspaceForWindow(browser()), primary.id);

  // Consistent right after the call: no posted reconciliation has run yet.
  ASSERT_TRUE(bridge_->ActivateTabInItsWorkspace(
      secondary_tab, WorkspaceActivationSource::kKeyboard,
      /*user_gesture=*/true));
  EXPECT_EQ(model->GetActiveTab(), secondary_tab);
  EXPECT_EQ(bridge_->GetActiveWorkspaceForWindow(browser()), secondary.id);
  EXPECT_EQ(bridge_->IsTabInActiveWorkspace(browser(), secondary_tab),
            std::optional<bool>(true));

  EXPECT_FALSE(bridge_->ActivateTabInItsWorkspace(
      nullptr, WorkspaceActivationSource::kKeyboard, /*user_gesture=*/true));
}

TEST_F(SessionBridgeTest, ActivateLastUsedTabTogglesWithinTheActiveWorkspace) {
  tab_tree::Workspace primary = MakeWorkspace(u"Primary", "a");
  tab_tree::Workspace secondary = MakeWorkspace(u"Secondary", "b");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({primary, secondary}));
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), primary.id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/first"));
  task_environment()->RunUntilIdle();
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* first = model->GetActiveTab();
  AddTab(browser(), GURL("https://example.test/second"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* second = model->GetActiveTab();
  ASSERT_NE(first, second);
  ASSERT_EQ(bridge_->GetWorkspaceForTab(first), primary.id);
  ASSERT_EQ(bridge_->GetWorkspaceForTab(second), primary.id);

  // Back to the tab used before, then toggled forward again.
  ASSERT_TRUE(bridge_->ActivateLastUsedTab(browser()));
  EXPECT_EQ(model->GetActiveTab(), first);
  ASSERT_TRUE(bridge_->ActivateLastUsedTab(browser()));
  EXPECT_EQ(model->GetActiveTab(), second);

  // Never into another Workspace: alone in Secondary there is no target.
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), secondary.id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/other"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* other = model->GetActiveTab();
  ASSERT_EQ(bridge_->GetWorkspaceForTab(other), secondary.id);
  EXPECT_FALSE(bridge_->ActivateLastUsedTab(browser()));
  EXPECT_EQ(model->GetActiveTab(), other);
}

TEST_F(SessionBridgeTest, TracksNativeWindowTabContentsAndWorkspace) {
  ASSERT_EQ(1u, bridge_->tracked_window_count());
  const std::optional<base::Uuid> window_id = bridge_->GetWindowId(browser());
  ASSERT_TRUE(window_id.has_value());
  EXPECT_TRUE(window_id->is_valid());
  EXPECT_EQ(browser(), bridge_->FindWindowById(*window_id));

  tab_tree::Workspace primary = MakeWorkspace(u"Primary", "a");
  tab_tree::Workspace secondary = MakeWorkspace(u"Secondary", "b");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({secondary, primary}));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(primary.id, bridge_->GetActiveWorkspaceForWindow(browser()));
  EXPECT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), secondary.id, WorkspaceActivationSource::kKeyboard));
  EXPECT_EQ(secondary.id, bridge_->GetActiveWorkspaceForWindow(browser()));

  const GURL url("https://example.test/runtime");
  AddTab(browser(), url);
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  content::WebContents* contents = tab->GetContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(1u, bridge_->tracked_tab_count());
  EXPECT_EQ(model, bridge_->FindTabStripModelForTab(tab));
  EXPECT_EQ(contents, bridge_->FindWebContentsForTab(tab));
  EXPECT_EQ(tab, bridge_->FindTabByWebContents(contents));

  tab_tree::TreeNode node = MakeSavedPage(secondary.id, url);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(node, tab));
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(node.id));
  EXPECT_EQ(node.id, bridge_->FindTreeNodeIdForTab(tab));

  tab_tree::TreeNode duplicate = node;
  AddTab(browser(), GURL("https://example.test/other"));
  tabs::TabInterface* other_tab = model->GetTabAtIndex(0);
  ASSERT_NE(tab, other_tab);
  EXPECT_FALSE(bridge_->BindTreeNodeToTab(duplicate, other_tab));

  tab_tree::TreeNode folder = MakeSavedPage(secondary.id, url);
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.url = GURL();
  EXPECT_FALSE(bridge_->BindTreeNodeToTab(folder, other_tab));

  model->DetachAndDeleteWebContentsAt(model->GetIndexOfTab(tab));
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(node.id));
  EXPECT_EQ(1u, bridge_->tracked_tab_count());
}

TEST_F(SessionBridgeTest, BindingFollowsDiscardAndNativeWindowMove) {
  tab_tree::Workspace workspace = MakeWorkspace(u"Workspace", "a");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({workspace}));
  task_environment()->RunUntilIdle();

  const GURL url("https://example.test/movable");
  AddTab(browser(), url);
  TabStripModel* first_model = browser()->GetTabStripModel();
  tabs::TabInterface* tab = first_model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  content::WebContents* old_contents = tab->GetContents();
  tab_tree::TreeNode node = MakeSavedPage(workspace.id, url);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(node, tab));

  std::unique_ptr<content::WebContents> replacement =
      content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
  content::WebContents* replacement_ptr = replacement.get();
  std::unique_ptr<content::WebContents> discarded =
      first_model->DiscardWebContents(old_contents, std::move(replacement));
  ASSERT_EQ(old_contents, discarded.get());
  EXPECT_EQ(nullptr, bridge_->FindTabByWebContents(old_contents));
  EXPECT_EQ(tab, bridge_->FindTabByWebContents(replacement_ptr));
  EXPECT_EQ(replacement_ptr, bridge_->FindWebContentsForTab(tab));
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(node.id));

  BrowserWindowCreateParams params(profile(), /*from_user_gesture=*/true);
  std::unique_ptr<Browser> second_browser =
      CreateBrowserWithTestWindowForParams(std::move(params));
  ASSERT_TRUE(second_browser);
  ASSERT_EQ(2u, bridge_->tracked_window_count());
  const std::optional<base::Uuid> first_window_id =
      bridge_->GetWindowId(browser());
  const std::optional<base::Uuid> second_window_id =
      bridge_->GetWindowId(second_browser.get());
  ASSERT_TRUE(first_window_id.has_value());
  ASSERT_TRUE(second_window_id.has_value());
  EXPECT_NE(first_window_id, second_window_id);
  TabStripModel* second_model = second_browser->GetTabStripModel();

  std::unique_ptr<tabs::TabModel> detached =
      first_model->DetachTabAtForInsertion(0);
  ASSERT_TRUE(detached);
  second_model->InsertDetachedTabAt(0, std::move(detached),
                                    AddTabTypes::ADD_ACTIVE);

  EXPECT_EQ(tab, second_model->GetTabAtIndex(0));
  EXPECT_EQ(second_model, bridge_->FindTabStripModelForTab(tab));
  EXPECT_EQ(replacement_ptr, bridge_->FindWebContentsForTab(tab));
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(node.id));
  EXPECT_EQ(node.id, bridge_->FindTreeNodeIdForTab(tab));

  // TestBrowserWindow has no BrowserView to perform the production close
  // sequence. Empty the auxiliary window before its local Browser owner is
  // destroyed, matching Chromium's multi-window unit-test contract.
  second_model->CloseAllTabs();
}

TEST_F(SessionBridgeTest, DroppedDetachedTabIsRetiredFailClosed) {
  tab_tree::Workspace workspace = MakeWorkspace(u"Workspace", "a");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({workspace}));
  task_environment()->RunUntilIdle();

  const GURL url("https://example.test/dropped-detach");
  AddTab(browser(), url);
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  tab_tree::TreeNode node = MakeSavedPage(workspace.id, url);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(node, tab));

  std::unique_ptr<tabs::TabModel> detached = model->DetachTabAtForInsertion(0);
  ASSERT_TRUE(detached);
  EXPECT_EQ(1u, bridge_->tracked_tab_count());
  detached.reset();

  // Retirement is posted because a native cross-window move may reinsert the
  // same TabModel synchronously. Reverse lookups must nevertheless fail closed
  // as soon as the detached owner dies, rather than exposing its freed
  // TabInterface until the posted bookkeeping cleanup runs.
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(node.id));
  task_environment()->RunUntilIdle();

  EXPECT_EQ(0u, bridge_->tracked_tab_count());
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(node.id));
}

TEST_F(SessionBridgeTest, ShutdownDetachesAndFailsClosed) {
  tab_tree::Workspace workspace = MakeWorkspace(u"Workspace", "a");
  ASSERT_TRUE(workspace_service_->ReplaceWorkspaces({workspace}));
  task_environment()->RunUntilIdle();
  AddTab(browser(), GURL("https://example.test/before-shutdown"));
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  tab_tree::TreeNode node =
      MakeSavedPage(workspace.id, tab->GetContents()->GetLastCommittedURL());
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(node, tab));
  base::WeakPtr<sync::ProfileSyncUiBridge> sync_bridge =
      bridge_->GetWeakPtrForSync();
  ASSERT_TRUE(sync_bridge);

  bridge_->Shutdown();
  EXPECT_FALSE(sync_bridge);
  EXPECT_FALSE(bridge_->is_operational());
  EXPECT_EQ(0u, bridge_->tracked_window_count());
  EXPECT_EQ(0u, bridge_->tracked_tab_count());
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(node.id));
  EXPECT_EQ(std::nullopt, bridge_->GetWindowId(browser()));
  EXPECT_FALSE(bridge_->SetActiveWorkspaceForWindow(
      browser(), workspace.id, WorkspaceActivationSource::kKeyboard));

  AddTab(browser(), GURL("https://example.test/after-shutdown"));
  EXPECT_EQ(0u, bridge_->tracked_tab_count());
  EXPECT_FALSE(bridge_->BindTreeNodeToTab(
      node, browser()->GetTabStripModel()->GetTabAtIndex(0)));
}

}  // namespace

}  // namespace ahoi
