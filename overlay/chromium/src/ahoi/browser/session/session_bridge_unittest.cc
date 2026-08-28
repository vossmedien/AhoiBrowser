// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/command_service_factory.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi {

namespace {

tab_tree::Workspace MakeWorkspace(std::u16string name, std::string sort_key) {
  const base::Time now = base::Time::Now();
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = std::move(name);
  workspace.sort_key = std::move(sort_key);
  workspace.created_at = now;
  workspace.modified_at = now;
  return workspace;
}

tab_tree::TreeNode MakeSavedPage(const base::Uuid& workspace_id,
                                 const GURL& url) {
  const base::Time now = base::Time::Now();
  tab_tree::TreeNode node;
  node.id = base::Uuid::GenerateRandomV4();
  node.workspace_id = workspace_id;
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.title = u"Production";
  node.url = url;
  node.sort_key = "a";
  node.created_at = now;
  node.modified_at = now;
  return node;
}

class SessionBridgeTest : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    workspace_service_ = WorkspaceServiceFactory::GetForProfile(profile());
    bridge_ = SessionBridgeFactory::GetForProfile(profile());
    ASSERT_TRUE(workspace_service_);
    ASSERT_TRUE(bridge_);
    ASSERT_TRUE(bridge_->is_operational());
    base::RunLoop ready;
    bridge_->RunWhenReadyForTesting(ready.QuitClosure());
    ready.Run();
    ASSERT_TRUE(bridge_->is_ready());
  }

 protected:
  void FlushPersistence() {
    base::RunLoop flushed;
    bridge_->FlushPersistenceForTesting(flushed.QuitClosure());
    flushed.Run();
  }

  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  raw_ptr<SessionBridge> bridge_ = nullptr;
};

TEST_F(SessionBridgeTest, FactoriesRejectOffTheRecordWithoutRedirection) {
  EXPECT_TRUE(CommandServiceFactory::GetForProfile(profile()));
  EXPECT_TRUE(WorkspaceServiceFactory::GetForProfile(profile()));
  EXPECT_EQ(bridge_, SessionBridgeFactory::GetForProfile(profile()));

  TestingProfile* otr_profile =
      TestingProfile::Builder().BuildIncognito(profile());
  ASSERT_TRUE(otr_profile);
  ASSERT_TRUE(otr_profile->IsOffTheRecord());
  EXPECT_EQ(nullptr, CommandServiceFactory::GetForProfile(otr_profile));
  EXPECT_EQ(nullptr, WorkspaceServiceFactory::GetForProfile(otr_profile));
  EXPECT_EQ(nullptr, SessionBridgeFactory::GetForProfile(otr_profile));
}

TEST_F(SessionBridgeTest, PersistsAndRebindsNestedPageAfterTabRecreation) {
  const GURL url("https://example.test/persistent-nested");
  std::vector<tab_tree::Workspace> workspaces;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetWorkspaces(&workspaces));
  ASSERT_FALSE(workspaces.empty());

  tab_tree::TreeNode folder = MakeSavedPage(workspaces.front().id, url);
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = u"Persistent project";
  folder.url = GURL();
  folder.sort_key = "folder-a";
  tab_tree::TreeNode page = MakeSavedPage(workspaces.front().id, url);
  page.parent_id = folder.id;
  page.sort_key = "nested-a";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(page));

  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  TabStripModel* model = browser()->tab_strip_model();
  tabs::TabInterface* original_tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(original_tab);
  EXPECT_EQ(page.id, bridge_->FindTreeNodeIdForTab(original_tab));

  model->DetachAndDeleteWebContentsAt(model->GetIndexOfTab(original_tab));
  ASSERT_EQ(0u, bridge_->tracked_tab_count());
  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* restored_tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(restored_tab);
  EXPECT_EQ(page.id, bridge_->FindTreeNodeIdForTab(restored_tab));

  const base::FilePath database_path =
      profile()->GetPath().AppendASCII(kTabTreeDatabaseFilename);
  FlushPersistence();
  EXPECT_TRUE(base::PathExists(database_path));
  bridge_->Shutdown();

  tab_tree::TabTreeStore reopened_store;
  ASSERT_TRUE(reopened_store.Initialize(database_path));
  tab_tree::TreeNode persisted_folder;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            reopened_store.GetNode(folder.id, &persisted_folder));
  EXPECT_EQ(folder, persisted_folder);
  tab_tree::TreeNode persisted_page;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            reopened_store.GetNode(page.id, &persisted_page));
  ASSERT_TRUE(persisted_page.parent_id.has_value());
  EXPECT_EQ(folder.id, *persisted_page.parent_id);
}

TEST_F(SessionBridgeTest, NewTabRemainsTemporaryAndIsAddressableByCommandBar) {
  CommandService* command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);
  const GURL url("https://example.test/temporary-open-tab");

  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  EXPECT_TRUE(bridge_->GetWorkspaceForTab(tab).has_value());

  const std::vector<RankedCommand> results =
      command_service->Query(u"temporary-open-tab", 10);
  const auto result =
      std::ranges::find_if(results, [&url](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kOpenTab &&
               ranked.item.url == url &&
               ranked.item.stable_id.starts_with("runtime:");
      });
  ASSERT_NE(result, results.end());
  EXPECT_EQ(tab, bridge_->FindTabForOpenTabStableId(result->item.stable_id));
}

TEST_F(SessionBridgeTest, NewTabPageNeverRebindsToSavedGenericPage) {
  ASSERT_FALSE(workspace_service_->ordered_workspaces().empty());
  const base::Uuid workspace_id =
      workspace_service_->ordered_workspaces().front().id;
  const GURL new_tab_url(chrome::kChromeUINewTabURL);
  const tab_tree::TreeNode saved_new_tab =
      MakeSavedPage(workspace_id, new_tab_url);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(saved_new_tab));

  AddTab(browser(), new_tab_url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  EXPECT_EQ(workspace_id, bridge_->GetWorkspaceForTab(tab));
}

TEST_F(SessionBridgeTest,
       ExplicitSavedNewTabBindingSurvivesDeferredGenericMatching) {
  ASSERT_FALSE(workspace_service_->ordered_workspaces().empty());
  const base::Uuid workspace_id =
      workspace_service_->ordered_workspaces().front().id;
  const tab_tree::TreeNode saved_new_tab =
      MakeSavedPage(workspace_id, GURL(chrome::kChromeUINewTabURL));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(saved_new_tab));

  AddTab(browser(), GURL(chrome::kChromeUINewTabURL));
  tabs::TabInterface* const tab =
      browser()->tab_strip_model()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(saved_new_tab, tab));
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(saved_new_tab.id));
  EXPECT_EQ(saved_new_tab.id, bridge_->FindTreeNodeIdForTab(tab));
}

TEST_F(SessionBridgeTest,
       CreatesUpdatesSwitchesAndDeletesWorkspaceWithoutLosingLiveTab) {
  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 1u);
  const base::Uuid fallback_id =
      workspace_service_->ordered_workspaces().front().id;
  const std::optional<base::Uuid> created_id =
      bridge_->CreateWorkspace(u"Client work", u"C", 0xFF4F8DE8u);
  ASSERT_TRUE(created_id.has_value());
  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 2u);
  EXPECT_EQ(workspace_service_->ordered_workspaces().back().id, *created_id);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->UpdateWorkspacePresentation(*created_id, u"Development",
                                                 u"D", 0xFF54A96Bu));
  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 2u);
  EXPECT_EQ(workspace_service_->ordered_workspaces().back().name,
            u"Development");
  EXPECT_EQ(workspace_service_->ordered_workspaces().back().icon, u"D");
  EXPECT_EQ(workspace_service_->ordered_workspaces().back().accent_argb,
            0xFF54A96Bu);

  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), *created_id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/client-work"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(tab);
  EXPECT_EQ(bridge_->GetWorkspaceForTab(tab), *created_id);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->DeleteWorkspace(*created_id));
  task_environment()->RunUntilIdle();
  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 1u);
  EXPECT_EQ(workspace_service_->ordered_workspaces().front().id, fallback_id);
  EXPECT_EQ(bridge_->GetActiveWorkspaceForWindow(browser()), fallback_id);
  EXPECT_EQ(bridge_->GetWorkspaceForTab(tab), fallback_id);
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kInvalidArgument,
            bridge_->DeleteWorkspace(fallback_id));
}

TEST_F(SessionBridgeTest, DuplicatesWorkspaceTreeAndPlacesItAfterSource) {
  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 1u);
  const base::Uuid source_id =
      workspace_service_->ordered_workspaces().front().id;
  const GURL source_url("https://example.test/duplicated-workspace");
  const tab_tree::TreeNode source_page = MakeSavedPage(source_id, source_url);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(source_page));

  const std::optional<base::Uuid> duplicate_id = bridge_->DuplicateWorkspace(
      source_id, u"Copied workspace", u"C", 0xFF4F8DE8u);
  ASSERT_TRUE(duplicate_id.has_value());
  ASSERT_NE(*duplicate_id, source_id);

  ASSERT_EQ(workspace_service_->ordered_workspaces().size(), 2u);
  const tab_tree::Workspace& source =
      workspace_service_->ordered_workspaces().front();
  const tab_tree::Workspace& duplicate =
      workspace_service_->ordered_workspaces().back();
  EXPECT_EQ(source.id, source_id);
  EXPECT_EQ(duplicate.id, *duplicate_id);
  EXPECT_EQ(duplicate.name, u"Copied workspace");
  EXPECT_EQ(duplicate.icon, u"C");
  EXPECT_EQ(duplicate.accent_argb, 0xFF4F8DE8u);
  EXPECT_EQ(duplicate.sort_key, source.sort_key + '@');
  EXPECT_GE(duplicate.created_at, source.created_at);
  EXPECT_GE(duplicate.modified_at, source.modified_at);

  std::vector<tab_tree::TreeNode> duplicate_nodes;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetChildren(*duplicate_id, std::nullopt,
                                                   &duplicate_nodes));
  ASSERT_EQ(duplicate_nodes.size(), 1u);
  EXPECT_EQ(duplicate_nodes.front().workspace_id, *duplicate_id);
  EXPECT_EQ(duplicate_nodes.front().url, source_url);

  tab_tree::TreeNode persisted_source_page;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetNode(source_page.id,
                                               &persisted_source_page));
  EXPECT_EQ(persisted_source_page, source_page);
  EXPECT_FALSE(bridge_->DuplicateWorkspace(base::Uuid(), u"Invalid", u"I",
                                           std::nullopt));
}

TEST_F(SessionBridgeTest, SavesTemporaryTabAtWorkspaceRootIdempotently) {
  CommandService* command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);
  const GURL url("https://example.test/save-current-tab");

  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  TabStripModel* model = browser()->tab_strip_model();
  tabs::TabInterface* tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  ASSERT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  size_t presentation_change_count = 0;
  base::CallbackListSubscription presentation_subscription =
      bridge_->AddRuntimePresentationChangedCallback(base::BindRepeating(
          [](size_t* count) { ++*count; }, &presentation_change_count));
  ASSERT_TRUE(presentation_subscription);

  const std::optional<base::Uuid> saved_id =
      bridge_->SaveTabAtWorkspaceRoot(browser(), tab);
  ASSERT_TRUE(saved_id.has_value());
  EXPECT_EQ(presentation_change_count, 1u);
  EXPECT_EQ(saved_id, bridge_->FindTreeNodeIdForTab(tab));

  tab_tree::TreeNode saved;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetNode(*saved_id, &saved));
  EXPECT_EQ(saved.type, tab_tree::TreeNodeType::kSavedPage);
  EXPECT_EQ(saved.url, url);
  EXPECT_EQ(saved.parent_id, std::nullopt);

  EXPECT_EQ(saved_id, bridge_->SaveTabAtWorkspaceRoot(browser(), tab));
  EXPECT_EQ(presentation_change_count, 1u);
  std::vector<tab_tree::TreeNode> root_nodes;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetChildren(saved.workspace_id,
                                                   std::nullopt, &root_nodes));
  ASSERT_EQ(root_nodes.size(), 1u);
  EXPECT_EQ(root_nodes.front().id, *saved_id);

  const std::vector<RankedCommand> results =
      command_service->Query(u"save-current-tab", 10);
  EXPECT_EQ(std::ranges::count_if(results,
                                  [&saved_id](const RankedCommand& ranked) {
                                    return ranked.item.stable_id ==
                                           saved_id->AsLowercaseString();
                                  }),
            1);
  const auto result =
      std::ranges::find_if(results, [&saved_id](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kOpenTab &&
               ranked.item.stable_id == saved_id->AsLowercaseString();
      });
  EXPECT_NE(result, results.end());

  model->DetachAndDeleteWebContentsAt(model->GetIndexOfTab(tab));
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(*saved_id));
  const std::vector<RankedCommand> closed_results =
      command_service->Query(u"save-current-tab", 10);
  ASSERT_EQ(std::ranges::count_if(closed_results,
                                  [&saved_id](const RankedCommand& ranked) {
                                    return ranked.item.stable_id ==
                                           saved_id->AsLowercaseString();
                                  }),
            1);
  EXPECT_NE(std::ranges::find_if(closed_results,
                                 [&saved_id](const RankedCommand& ranked) {
                                   return ranked.item.stable_id ==
                                              saved_id->AsLowercaseString() &&
                                          ranked.item.type ==
                                              CommandItemType::kSavedPage;
                                 }),
            closed_results.end());
  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* reopened = model->GetTabAtIndex(0);
  ASSERT_TRUE(reopened);
  EXPECT_EQ(*saved_id, bridge_->FindTreeNodeIdForTab(reopened));
}

TEST_F(SessionBridgeTest, PublishesNestedSavedPagesToCommandBarIndex) {
  CommandService* command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);

  std::vector<tab_tree::Workspace> workspaces;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->GetWorkspaces(&workspaces));
  ASSERT_FALSE(workspaces.empty());

  tab_tree::TreeNode folder =
      MakeSavedPage(workspaces.front().id, GURL("https://unused.test/"));
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = u"Command projects";
  folder.url = GURL();
  folder.sort_key = "command-folder";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(folder));

  const GURL page_url("https://preview.example.test/nested/page");
  tab_tree::TreeNode page = MakeSavedPage(workspaces.front().id, page_url);
  page.parent_id = folder.id;
  page.title = u"Ahoi command preview";
  page.sort_key = "command-page";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(page));

  const std::vector<RankedCommand> folder_results =
      command_service->Query(u"@tree command projects", 10);
  const auto folder_result = std::ranges::find_if(
      folder_results, [&folder](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kFolder &&
               ranked.item.stable_id == folder.id.AsLowercaseString();
      });
  ASSERT_NE(folder_result, folder_results.end());
  EXPECT_NE(folder_result->item.secondary_text.find(u"Command projects"),
            std::u16string::npos);

  const std::vector<RankedCommand> results =
      command_service->Query(u"command preview", 10);
  const auto result =
      std::ranges::find_if(results, [&page](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kSavedPage &&
               ranked.item.stable_id == page.id.AsLowercaseString();
      });
  ASSERT_NE(result, results.end());
  EXPECT_EQ(result->item.url, page_url);
  EXPECT_EQ(result->item.secondary_text, base::UTF8ToUTF16(page_url.spec()));
  ASSERT_EQ(result->item.keywords.size(), 2u);
  EXPECT_NE(result->item.keywords[1].find(u"Command projects"),
            std::u16string::npos);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->DeleteNode(page.id, base::Time::Now()));
  const std::vector<RankedCommand> after_delete =
      command_service->Query(u"command preview", 10);
  EXPECT_EQ(std::ranges::find_if(
                after_delete,
                [&page](const RankedCommand& ranked) {
                  return ranked.item.type == CommandItemType::kSavedPage &&
                         ranked.item.stable_id == page.id.AsLowercaseString();
                }),
            after_delete.end());
}

TEST_F(SessionBridgeTest, RemovesUrlUserinfoBeforeCommandIndexing) {
  CommandService* command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);
  ASSERT_FALSE(workspace_service_->ordered_workspaces().empty());

  const GURL credential_url(
      "https://username:password@example.test/private-document");
  tab_tree::TreeNode page = MakeSavedPage(
      workspace_service_->ordered_workspaces().front().id, credential_url);
  page.title = base::UTF8ToUTF16(credential_url.spec());
  page.sort_key = "credential-page";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(page));

  const std::vector<RankedCommand> results =
      command_service->Query(u"private-document", 10u);
  const auto result =
      std::ranges::find_if(results, [&page](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kSavedPage &&
               ranked.item.stable_id == page.id.AsLowercaseString();
      });
  ASSERT_NE(result, results.end());
  const GURL safe_url("https://example.test/private-document");
  EXPECT_EQ(result->item.url, safe_url);
  EXPECT_EQ(result->item.title, base::UTF8ToUTF16(safe_url.spec()));
  EXPECT_EQ(result->item.secondary_text, base::UTF8ToUTF16(safe_url.spec()));
  EXPECT_TRUE(command_service->Query(u"username", 10u).empty());
  EXPECT_TRUE(command_service->Query(u"password", 10u).empty());
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
  TabStripModel* model = browser()->tab_strip_model();
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
  TabStripModel* first_model = browser()->tab_strip_model();
  tabs::TabInterface* tab = first_model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  content::WebContents* old_contents = tab->GetContents();
  tab_tree::TreeNode node = MakeSavedPage(workspace.id, url);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(node, tab));

  std::unique_ptr<content::WebContents> replacement =
      content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
  content::WebContents* replacement_ptr = replacement.get();
  std::unique_ptr<content::WebContents> discarded =
      first_model->DiscardWebContentsAt(0, std::move(replacement));
  ASSERT_EQ(old_contents, discarded.get());
  EXPECT_EQ(nullptr, bridge_->FindTabByWebContents(old_contents));
  EXPECT_EQ(tab, bridge_->FindTabByWebContents(replacement_ptr));
  EXPECT_EQ(replacement_ptr, bridge_->FindWebContentsForTab(tab));
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(node.id));

  Browser::CreateParams params(profile(), /*user_gesture=*/true);
  std::unique_ptr<Browser> second_browser =
      CreateBrowserWithTestWindowForParams(params);
  ASSERT_TRUE(second_browser);
  ASSERT_EQ(2u, bridge_->tracked_window_count());
  const std::optional<base::Uuid> first_window_id =
      bridge_->GetWindowId(browser());
  const std::optional<base::Uuid> second_window_id =
      bridge_->GetWindowId(second_browser.get());
  ASSERT_TRUE(first_window_id.has_value());
  ASSERT_TRUE(second_window_id.has_value());
  EXPECT_NE(first_window_id, second_window_id);
  TabStripModel* second_model = second_browser->tab_strip_model();

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
  TabStripModel* model = browser()->tab_strip_model();
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
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(0);
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
      node, browser()->tab_strip_model()->GetTabAtIndex(0)));
}

}  // namespace

}  // namespace ahoi
