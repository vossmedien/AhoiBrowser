// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/tab_group_sidebar_adapter.h"

#include <utility>

#include "ahoi/browser/session/session_bridge_unittest_support.h"
#include "ahoi/browser/session/website_session_context.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/testing_profile.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {
namespace {

// Uses the real SessionBridge/TabStripModel fixture, including asynchronous
// binding and the adapter installed by the production TrackBrowser path.
class TabGroupSidebarAdapterTest : public test_support::SessionBridgeTest {
 protected:
  tabs::TabInterface* AddPage(const char* url) {
    AddTab(browser(), GURL(url));
    auto* tab = browser()->GetTabStripModel()->GetActiveTab();
    task_environment()->RunUntilIdle();
    return tab;
  }
  TabGroupSidebarAdapter* adapter() {
    return bridge_->GetTabGroupSidebarAdapter(browser());
  }
  base::Uuid workspace() {
    return *bridge_->GetActiveWorkspaceForWindow(browser());
  }
};

TEST_F(TabGroupSidebarAdapterTest, NativeAndSidebarEditsRetainSavedTreeAndTabs) {
  auto* first = AddPage("https://example.test/first");
  auto* second = AddPage("https://example.test/second");
  const auto saved = bridge_->SaveTabAtWorkspaceRoot(browser(), first);
  ASSERT_TRUE(saved);
  auto folder = test_support::MakeSavedPage(workspace(), GURL());
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = u"Unrelated saved folder";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->MoveNode(
                *saved, workspace(), folder.id, "a", base::Time::Now()));
  task_environment()->RunUntilIdle();
  tab_tree::TabTreeSnapshot before;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&before));
  const auto* contents = first->GetContents();
  const auto temporary = bridge_->FindSharedTreeNodeIdForTab(second);
  auto* model = browser()->GetTabStripModel();
  const auto id = model->AddToNewGroup({0, 1});
  model->ChangeTabGroupVisuals(id, tab_groups::TabGroupVisualData(
      u"Extension group", tab_groups::TabGroupColorId::kBlue, true));
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(adapter());
  const auto groups = adapter()->ReadGroups(workspace());
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(id, groups.front().id);
  EXPECT_EQ(2u, groups.front().members.size());
  EXPECT_EQ(u"Extension group", groups.front().visuals.title());
  EXPECT_TRUE(groups.front().visuals.is_collapsed());
  ASSERT_TRUE(adapter()->SetVisuals(id, tab_groups::TabGroupVisualData(
      u"Sidebar rename", tab_groups::TabGroupColorId::kRed, false)));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(u"Sidebar rename", model->group_model()->GetTabGroup(id)
                                    ->visual_data()->title());
  EXPECT_EQ(tab_groups::TabGroupColorId::kRed,
            model->group_model()->GetTabGroup(id)->visual_data()->color());
  ASSERT_TRUE(adapter()->Ungroup(id));
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(adapter()->ReadGroups(workspace()).empty());
  EXPECT_FALSE(adapter()->Ungroup(id));  // Stale UI command, no resurrection.
  EXPECT_EQ(contents, first->GetContents());
  EXPECT_EQ(saved, bridge_->FindTreeNodeIdForTab(first));
  EXPECT_EQ(temporary, bridge_->FindSharedTreeNodeIdForTab(second));
  tab_tree::TabTreeSnapshot after;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&after));
  EXPECT_EQ(before, after);  // Also proves no group folder/undo/sync row inserted.
  EXPECT_EQ(2, model->count());
}

TEST_F(TabGroupSidebarAdapterTest, AddingAndRemovingOnePaneMovesWholeSplit) {
  auto* first = AddPage("https://example.test/pane1");
  auto* second = AddPage("https://example.test/pane2");
  auto* target = AddPage("https://example.test/target");
  auto* model = browser()->GetTabStripModel();
  // AddTab prepends, so the two panes occupy indices 1 and 2.
  const auto split = model->AddToNewSplit(
      {1, 2}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kExtensionsApi);
  const auto group = model->AddToNewGroup({model->GetIndexOfTab(target)});
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(adapter()->AddTab(group, first));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(group, first->GetGroup());
  EXPECT_EQ(group, second->GetGroup());
  EXPECT_EQ(split, first->GetSplit());
  EXPECT_EQ(split, second->GetSplit());
  ASSERT_TRUE(adapter()->RemoveTab(first));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(first->GetGroup());
  EXPECT_FALSE(second->GetGroup());
  EXPECT_EQ(split, first->GetSplit());
  EXPECT_EQ(split, second->GetSplit());
  EXPECT_EQ(group, target->GetGroup());
}

TEST_F(TabGroupSidebarAdapterTest, MixedWorkspacesBecomeSeparateNativeGroups) {
  auto* first = AddPage("https://example.test/workspace1");
  const auto first_workspace = workspace();
  const auto second_workspace = bridge_->CreateWorkspace(
      u"Second", u"folder", std::nullopt);
  ASSERT_TRUE(second_workspace);
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), *second_workspace, WorkspaceActivationSource::kKeyboard));
  auto* second = AddPage("https://example.test/workspace2");
  const auto first_node = bridge_->FindSharedTreeNodeIdForTab(first);
  const auto second_node = bridge_->FindSharedTreeNodeIdForTab(second);
  const auto* contents = second->GetContents();
  auto* model = browser()->GetTabStripModel();
  const auto mixed = model->AddToNewGroup({0, 1});
  model->ChangeTabGroupVisuals(mixed, tab_groups::TabGroupVisualData(
      u"Mixed extension request", tab_groups::TabGroupColorId::kGreen));
  task_environment()->RunUntilIdle();
  ASSERT_NE(first->GetGroup(), second->GetGroup());
  EXPECT_EQ(1u, adapter()->ReadGroups(first_workspace).size());
  EXPECT_EQ(1u, adapter()->ReadGroups(*second_workspace).size());
  ASSERT_TRUE(first->GetGroup());
  EXPECT_FALSE(adapter()->CanAddTab(*first->GetGroup(), second));
  EXPECT_FALSE(adapter()->AddTab(*first->GetGroup(), second));
  EXPECT_EQ(contents, second->GetContents());
  EXPECT_EQ(first_node, bridge_->FindSharedTreeNodeIdForTab(first));
  EXPECT_EQ(second_node, bridge_->FindSharedTreeNodeIdForTab(second));
}

TEST_F(TabGroupSidebarAdapterTest, RestoreUsesNativeGroupAndExistingTreeIdentity) {
  auto* tab = AddPage("https://example.test/restored");
  const auto node = bridge_->SaveTabAtWorkspaceRoot(browser(), tab);
  ASSERT_TRUE(node);
  const auto metadata = bridge_->GetTabSessionMetadata(tab);
  ASSERT_TRUE(metadata);
  const auto visuals = tab_groups::TabGroupVisualData(
      u"Restored", tab_groups::TabGroupColorId::kPurple, true);
  auto* model = browser()->GetTabStripModel();
  const auto old_group = model->AddToNewGroup({0});
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(adapter()->Ungroup(old_group));
  // Actual native restore entry points, with a newly allocated local group
  // identity. There is no persisted Ahoi/native-id link to reuse incorrectly.
  const auto restored = tab_groups::TabGroupId::GenerateNew();
  model->AddToGroupForRestore({0}, restored);
  model->ChangeTabGroupVisuals(restored, visuals);
  ASSERT_TRUE(bridge_->RestoreTabSessionMetadata(tab, *metadata));
  task_environment()->RunUntilIdle();
  const auto groups = adapter()->ReadGroups(workspace());
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(restored, groups.front().id);
  EXPECT_EQ(visuals, groups.front().visuals);
  EXPECT_EQ(node, bridge_->FindTreeNodeIdForTab(tab));
  EXPECT_EQ(tab, groups.front().members.front().get());
}

TEST_F(TabGroupSidebarAdapterTest, RealWebsiteSessionRemainsSeparate) {
  auto* normal = AddPage("https://example.test/default-session");
  const GURL url("https://example.test/own-session");
  const session::WebsiteSessionBinding binding{
      .context_id = base::Uuid::GenerateRandomV4()};
  auto site = content::SiteInstance::CreateForFixedStoragePartition(
      profile(), url,
      session::StoragePartitionConfigForWebsiteSession(profile(), binding));
  auto contents =
      content::WebContentsTester::CreateTestWebContents(profile(), site);
  auto* own_contents = contents.get();
  content::WebContentsTester::For(own_contents)->NavigateAndCommit(url);
  auto* model = browser()->GetTabStripModel();
  model->AppendWebContents(std::move(contents), true);
  auto* own = model->GetActiveTab();
  task_environment()->RunUntilIdle();
  ASSERT_NE(normal, own);
  ASSERT_EQ(bridge_->GetWorkspaceForTab(normal), bridge_->GetWorkspaceForTab(own));
  ASSERT_EQ(binding, session::WebsiteSessionBindingForWebContents(
                         profile(), own_contents));
  model->AddToNewGroup({0, 1});
  task_environment()->RunUntilIdle();
  ASSERT_NE(normal->GetGroup(), own->GetGroup());
  ASSERT_TRUE(normal->GetGroup());
  EXPECT_FALSE(adapter()->AddTab(*normal->GetGroup(), own));
  EXPECT_EQ(own_contents, own->GetContents());
  EXPECT_EQ(binding, session::WebsiteSessionBindingForWebContents(
                        profile(), own->GetContents()));
}

TEST_F(TabGroupSidebarAdapterTest, DetachedTabsCannotBeAddedByStaleWindowUi) {
  auto* tab = AddPage("https://example.test/moved");
  const auto node = bridge_->FindSharedTreeNodeIdForTab(tab);
  auto* contents = tab->GetContents();
  auto* model = browser()->GetTabStripModel();
  const auto group = model->AddToNewGroup({0});
  task_environment()->RunUntilIdle();
  BrowserWindowCreateParams params(profile(), true);
  auto other = CreateBrowserWithTestWindowForParams(std::move(params));
  ASSERT_TRUE(other);
  int invalidations = 0;
  auto subscription = bridge_->AddRuntimePresentationChangedCallback(
      base::BindRepeating([](int* count) { ++*count; }, &invalidations));
  auto detached = model->DetachTabAtForInsertion(0);
  EXPECT_FALSE(adapter()->CanAddTab(group, tab));
  other->GetTabStripModel()->InsertDetachedTabAt(
      0, std::move(detached), AddTabTypes::ADD_ACTIVE);
  task_environment()->RunUntilIdle();
  auto* target = bridge_->GetTabGroupSidebarAdapter(other.get());
  ASSERT_TRUE(target);
  EXPECT_FALSE(adapter()->AddTab(group, tab));
  EXPECT_EQ(contents, tab->GetContents());
  EXPECT_EQ(node, bridge_->FindSharedTreeNodeIdForTab(tab));
  const int settled_invalidations = invalidations;
  task_environment()->RunUntilIdle();
  EXPECT_EQ(settled_invalidations, invalidations);
  // General bridge notifications must not ping-pong between the two adapters.
  other->GetTabStripModel()->CloseAllTabs();
}

}  // namespace
}  // namespace ahoi::extensions
