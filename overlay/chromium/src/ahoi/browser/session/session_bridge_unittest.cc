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

  // Saved pages bind by exact identity (activation or restore metadata),
  // never because an unrelated tab shows the same URL.
  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* original_tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(original_tab);
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(original_tab).has_value());
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(page, original_tab));
  EXPECT_EQ(page.id, bridge_->FindTreeNodeIdForTab(original_tab));

  model->DetachAndDeleteWebContentsAt(model->GetIndexOfTab(original_tab));
  ASSERT_EQ(0u, bridge_->tracked_tab_count());
  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* restored_tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(restored_tab);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(page, restored_tab));
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

TEST_F(SessionBridgeTest, BackupFlushPersistsSecondNestedTreeMutation) {
  const auto workspaces = workspace_service_->ordered_workspaces();
  ASSERT_FALSE(workspaces.empty());
  auto *const store = bridge_->tab_tree_store();
  tab_tree::TreeNode folder = MakeSavedPage(workspaces.front().id, GURL());
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = u"Persistent project";
  tab_tree::TreeNode page = MakeSavedPage(
      workspaces.front().id, GURL("https://example.test/backup-flush"));
  page.parent_id = folder.id;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store->CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store->CreateNode(page));

  tab_tree::TabTreeStore::PersistenceSnapshot applied;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store->ExportPersistenceSnapshot(&applied));
  applied.sync_baseline_receipt = "applied-before-local-edit";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store->ReplacePersistenceSnapshot(applied));

  base::test::TestFuture<bool> first_flush;
  bridge_->FlushPersistenceForBackup(first_flush.GetCallback());
  ASSERT_TRUE(first_flush.Get());

  ASSERT_EQ(
      tab_tree::TabTreeStore::Result::kOk,
      store->RenameNode(page.id, u"Second persisted title", base::Time::Now()));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store->UpdateWorkspacePresentation(
                workspaces.front().id, u"Second persisted workspace", u"code",
                std::nullopt, base::Time::Now()));
  tab_tree::TabTreeSnapshot expected;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&expected));

  // Observe the production bool; FlushPersistenceForTesting discards failures.
  base::test::TestFuture<bool> second_flush;
  bridge_->FlushPersistenceForBackup(second_flush.GetCallback());
  ASSERT_TRUE(second_flush.Get());

  const base::FilePath database_path =
      profile()->GetPath().AppendASCII(kTabTreeDatabaseFilename);
  tab_tree::TabTreeStore reloaded;
  ASSERT_TRUE(reloaded.Initialize(database_path));
  tab_tree::TabTreeStore::PersistenceSnapshot persisted;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            reloaded.ExportPersistenceSnapshot(&persisted));
  EXPECT_EQ(expected, persisted.tree);
  EXPECT_EQ(applied.sync_baseline_receipt, persisted.sync_baseline_receipt);
  tab_tree::TreeNode persisted_page;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            reloaded.GetNode(page.id, &persisted_page));
  EXPECT_EQ(u"Second persisted title", persisted_page.title);
  ASSERT_TRUE(persisted_page.parent_id.has_value());
  EXPECT_EQ(folder.id, *persisted_page.parent_id);
}

TEST_F(SessionBridgeTest, NewTabRemainsTemporaryAndIsAddressableByCommandBar) {
  CommandService *command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);
  const GURL url("https://example.test/temporary-open-tab");

  AddTab(browser(), url);
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  EXPECT_TRUE(bridge_->GetWorkspaceForTab(tab).has_value());

  const std::vector<RankedCommand> results =
      command_service->Query(u"temporary-open-tab", 10);
  const auto result =
      std::ranges::find_if(results, [&url](const RankedCommand& ranked) {
        return ranked.item.type == CommandItemType::kOpenTab &&
               ranked.item.url == url &&
               // Temporary pages carry a stable shared node ID now.
               base::Uuid::ParseLowercase(ranked.item.stable_id).is_valid();
      });
  ASSERT_NE(result, results.end()) << [&results] {
    std::string ids;
    for (const RankedCommand& ranked : results) {
      ids += " " + std::to_string(static_cast<int>(ranked.item.type)) + ":" +
             ranked.item.stable_id + "@" +
             (ranked.item.url ? ranked.item.url->spec() : std::string("-"));
    }
    return "results:" + ids;
  }();
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
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetTabAtIndex(0);
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
      browser()->GetTabStripModel()->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(bridge_->BindTreeNodeToTab(saved_new_tab, tab));
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, browser()->GetTabStripModel()->count());
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(saved_new_tab.id));
  EXPECT_EQ(saved_new_tab.id, bridge_->FindTreeNodeIdForTab(tab));
}

TEST_F(SessionBridgeTest, ComputedPartitionPathMatchesTheLoadedPartition) {
  // Handoff 010 R2: startup cleanup computes the directory instead of loading
  // the partition; this pins the computation to content's own layout.
  const session::WebsiteSessionBinding binding{
      .context_id = base::Uuid::GenerateRandomV4()};
  content::StoragePartition* partition = profile()->GetStoragePartition(
      session::StoragePartitionConfigForWebsiteSession(profile(), binding));
  ASSERT_TRUE(partition);
  EXPECT_EQ(partition->GetPath(),
            session::WebsiteSessionPartitionPath(profile()->GetPath(), binding));
  EXPECT_TRUE(session::WebsiteSessionPartitionPath(
                  profile()->GetPath(), session::WebsiteSessionBinding())
                  .empty());
}

TEST_F(SessionBridgeTest, WorkspaceLevelIsChosenAtCreationAndDuplicated) {
  const base::Uuid fallback_id =
      workspace_service_->ordered_workspaces().front().id;
  const std::optional<base::Uuid> shared_id =
      bridge_->CreateWorkspace(u"Shared", u"S", std::nullopt);
  ASSERT_TRUE(shared_id.has_value());
  EXPECT_FALSE(bridge_->HasOwnWebsiteSessions(*shared_id));

  const std::optional<base::Uuid> own_id = bridge_->CreateWorkspace(
      u"Client", u"C", std::nullopt, /*own_website_sessions=*/true);
  ASSERT_TRUE(own_id.has_value());
  EXPECT_TRUE(bridge_->HasOwnWebsiteSessions(*own_id));
  // Workspaces that existed before the first own one keep the default jar.
  EXPECT_FALSE(bridge_->HasOwnWebsiteSessions(fallback_id));
  EXPECT_FALSE(bridge_->HasOwnWebsiteSessions(*shared_id));

  const std::optional<base::Uuid> copy_id =
      bridge_->DuplicateWorkspace(*own_id, u"Client copy", u"C", std::nullopt);
  ASSERT_TRUE(copy_id.has_value());
  EXPECT_TRUE(bridge_->HasOwnWebsiteSessions(*copy_id));
  // A duplicate starts with a fresh, empty session of its own.
  EXPECT_NE(
      session::FindWebsiteSessionBinding(profile()->GetPrefs(), *own_id),
      session::FindWebsiteSessionBinding(profile()->GetPrefs(), *copy_id));

  const std::optional<base::Uuid> shared_copy = bridge_->DuplicateWorkspace(
      *shared_id, u"Shared copy", u"S", std::nullopt);
  ASSERT_TRUE(shared_copy.has_value());
  EXPECT_FALSE(bridge_->HasOwnWebsiteSessions(*shared_copy));
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
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetActiveTab();
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
  tab_tree::TreeNode source_page = MakeSavedPage(source_id, source_url);
  // The store records a saved page's Home on creation; expect it.
  tab_tree::InitializeSavedHome(&source_page);
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
  TabStripModel* model = browser()->GetTabStripModel();
  tabs::TabInterface* tab = model->GetTabAtIndex(0);
  ASSERT_TRUE(tab);
  ASSERT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  const auto temporary_id = bridge_->FindSharedTreeNodeIdForTab(tab);
  ASSERT_TRUE(temporary_id.has_value());
  size_t presentation_change_count = 0;
  base::CallbackListSubscription presentation_subscription =
      bridge_->AddRuntimePresentationChangedCallback(base::BindRepeating(
          [](size_t* count) { ++*count; }, &presentation_change_count));
  ASSERT_TRUE(presentation_subscription);

  const std::optional<base::Uuid> saved_id =
      bridge_->SaveTabAtWorkspaceRoot(browser(), tab);
  ASSERT_TRUE(saved_id.has_value());
  EXPECT_EQ(temporary_id, saved_id);
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

  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->MakeTabTemporary(tab));
  EXPECT_EQ(presentation_change_count, 2u);
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
  EXPECT_EQ(saved_id, bridge_->FindSharedTreeNodeIdForTab(tab));
  EXPECT_EQ(saved_id, bridge_->SaveTabAtWorkspaceRoot(browser(), tab));
  EXPECT_EQ(presentation_change_count, 3u);

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
  // Independently reopening the same URL is a new normal tab, not an implicit
  // activation of the saved page. Only explicit identity can reuse that page.
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(reopened).has_value());
  const auto reopened_id = bridge_->FindSharedTreeNodeIdForTab(reopened);
  ASSERT_TRUE(reopened_id.has_value());
  EXPECT_NE(saved_id, reopened_id);
  EXPECT_EQ(nullptr, bridge_->FindTabByTreeNodeId(*saved_id));
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
  CommandService *command_service =
      CommandServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(command_service);
  ASSERT_FALSE(workspace_service_->ordered_workspaces().empty());

  // These credential-only sentinels cannot be fuzzy subsequences of the safe
  // URL/path. Generic "username" can match across repeated sanitized fields.
  const GURL credential_url(
      "https://userinfo-qzx987:secret-qzx654@example.test/private-document");
  tab_tree::TreeNode page = MakeSavedPage(
      workspace_service_->ordered_workspaces().front().id, credential_url);
  page.title = base::UTF8ToUTF16(credential_url.spec());
  page.sort_key = "credential-page";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(page));

  const std::vector<RankedCommand> results =
      command_service->Query(u"private-document", 10u);
  const auto result =
      std::ranges::find_if(results, [&page](const RankedCommand &ranked) {
        return ranked.item.type == CommandItemType::kSavedPage &&
               ranked.item.stable_id == page.id.AsLowercaseString();
      });
  ASSERT_NE(result, results.end());
  const GURL safe_url("https://example.test/private-document");
  EXPECT_EQ(result->item.url, safe_url);
  EXPECT_EQ(result->item.title, base::UTF8ToUTF16(safe_url.spec()));
  EXPECT_EQ(result->item.secondary_text, base::UTF8ToUTF16(safe_url.spec()));
  ASSERT_EQ(2u, result->item.keywords.size());
  EXPECT_EQ(base::UTF8ToUTF16(safe_url.spec()), result->item.keywords[0]);
  EXPECT_EQ(workspace_service_->ordered_workspaces().front().name + u" / " +
                base::UTF8ToUTF16(safe_url.spec()),
            result->item.keywords[1]);
  EXPECT_TRUE(command_service->Query(u"userinfo-qzx987", 10u).empty());
  EXPECT_TRUE(command_service->Query(u"secret-qzx654", 10u).empty());
}

}  // namespace

}  // namespace ahoi
