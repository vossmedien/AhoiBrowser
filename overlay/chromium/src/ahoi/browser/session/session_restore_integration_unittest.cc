// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_restore_integration.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/session/workspace_session_metadata.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::session {

namespace {

tab_tree::TreeNode MakeSavedPage(const base::Uuid& workspace_id,
                                 const GURL& url) {
  const base::Time now = base::Time::Now();
  return {
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = workspace_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = u"Restored page",
      .url = url,
      .sort_key = "a",
      .created_at = now,
      .modified_at = now,
  };
}

class SessionRestoreIntegrationTest : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    workspace_service_ = WorkspaceServiceFactory::GetForProfile(profile());
    bridge_ = SessionBridgeFactory::GetForProfile(profile());
    ASSERT_TRUE(workspace_service_);
    ASSERT_TRUE(bridge_);
    base::RunLoop ready;
    bridge_->RunWhenReadyForTesting(ready.QuitClosure());
    ready.Run();
    ASSERT_TRUE(bridge_->is_ready());
    ASSERT_FALSE(workspace_service_->ordered_workspaces().empty());
  }

 protected:
  base::Uuid CreateSecondWorkspace() {
    const std::optional<base::Uuid> workspace_id =
        bridge_->CreateWorkspace(u"Restored workspace", u"R", std::nullopt);
    EXPECT_TRUE(workspace_id.has_value());
    return workspace_id.value_or(base::Uuid());
  }

  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  raw_ptr<SessionBridge> bridge_ = nullptr;
};

TEST_F(SessionRestoreIntegrationTest, ExtraDataRoundTripsWindowAndTabState) {
  const base::Uuid second_workspace = CreateSecondWorkspace();
  ASSERT_TRUE(second_workspace.is_valid());
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), second_workspace, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/session-roundtrip"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(tab);

  std::map<std::string, std::string> window_extra_data;
  std::map<std::string, std::string> tab_extra_data;
  ASSERT_TRUE(PopulateWindowSessionExtraData(browser(), &window_extra_data));
  ASSERT_TRUE(PopulateTabSessionExtraData(browser(), tab, &tab_extra_data));

  WindowSessionMetadata decoded_window;
  ASSERT_EQ(SessionMetadataDecodeResult::kSuccess,
            DecodeWindowSessionMetadata(
                window_extra_data.at(kWindowSessionMetadataExtraDataKey),
                &decoded_window));
  EXPECT_EQ(second_workspace, decoded_window.active_workspace_id);
  TabSessionMetadata decoded_tab;
  ASSERT_EQ(
      SessionMetadataDecodeResult::kSuccess,
      DecodeTabSessionMetadata(
          tab_extra_data.at(kTabSessionMetadataExtraDataKey), &decoded_tab));
  EXPECT_EQ(second_workspace, decoded_tab.workspace_id);
  EXPECT_FALSE(decoded_tab.tree_node_id.has_value());
  EXPECT_TRUE(decoded_tab.last_active_in_workspace);

  const base::Uuid first_workspace =
      workspace_service_->ordered_workspaces().front().id;
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), first_workspace, WorkspaceActivationSource::kKeyboard));
  bridge_->UnbindTreeNodeFromTab(tab);
  EXPECT_TRUE(RestoreWindowSessionExtraData(browser(), window_extra_data));
  EXPECT_TRUE(RestoreTabSessionExtraData(browser(), tab, tab_extra_data));
  EXPECT_EQ(second_workspace, bridge_->GetActiveWorkspaceForWindow(browser()));
  EXPECT_EQ(second_workspace, bridge_->GetWorkspaceForTab(tab));
}

TEST_F(SessionRestoreIntegrationTest,
       RestoredNativeContextMustBelongToLocalProfile) {
  const base::Uuid existing =
      workspace_service_->ordered_workspaces().front().id;
  const std::array<base::Uuid, 1> existing_ids = {existing};
  ASSERT_TRUE(InitializeWebsiteSessionBindings(profile()->GetPrefs(),
                                                base::span(existing_ids)));
  const base::Uuid new_workspace = base::Uuid::GenerateRandomV4();
  const auto binding = GetOrCreateWebsiteSessionBinding(profile()->GetPrefs(),
                                                        new_workspace);
  ASSERT_TRUE(binding);
  ASSERT_FALSE(binding->is_default());

  TabSessionMetadata metadata{
      .workspace_id = new_workspace,
      .website_session_context_id = binding->context_id,
  };
  std::map<std::string, std::string> extra_data;
  const auto encoded = EncodeTabSessionMetadata(metadata);
  ASSERT_TRUE(encoded);
  extra_data[kTabSessionMetadataExtraDataKey] = *encoded;
  EXPECT_EQ(binding,
            ReadRestoredWebsiteSessionBinding(profile(), extra_data));

  metadata.website_session_context_id = base::Uuid::GenerateRandomV4();
  const auto foreign = EncodeTabSessionMetadata(metadata);
  ASSERT_TRUE(foreign);
  extra_data[kTabSessionMetadataExtraDataKey] = *foreign;
  EXPECT_FALSE(ReadRestoredWebsiteSessionBinding(profile(), extra_data));
  extra_data[kTabSessionMetadataExtraDataKey] = "not-json";
  EXPECT_FALSE(ReadRestoredWebsiteSessionBinding(profile(), extra_data));
  extra_data.clear();
  const auto legacy = ReadRestoredWebsiteSessionBinding(profile(), extra_data);
  ASSERT_TRUE(legacy);
  EXPECT_TRUE(legacy->is_default());
}

TEST_F(SessionRestoreIntegrationTest,
       TypedNavigationDoesNotTargetHiddenWorkspaceTab) {
  const base::Uuid first_workspace =
      workspace_service_->ordered_workspaces().front().id;
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), first_workspace, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/first-workspace"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* first = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(first);
  const auto first_is_active = IsCurrentTabInActiveWorkspaceForNavigation(
      browser(), first->GetContents());
  ASSERT_TRUE(first_is_active);
  EXPECT_TRUE(*first_is_active);

  const base::Uuid second_workspace = CreateSecondWorkspace();
  ASSERT_TRUE(second_workspace.is_valid());
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), second_workspace, WorkspaceActivationSource::kKeyboard));
  const auto hidden_source = IsCurrentTabInActiveWorkspaceForNavigation(
      browser(), first->GetContents());
  ASSERT_TRUE(hidden_source);
  EXPECT_FALSE(*hidden_source);

  AddTab(browser(), GURL("https://example.test/second-workspace"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* second = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(second);
  ASSERT_NE(first, second);
  const auto second_is_active = IsCurrentTabInActiveWorkspaceForNavigation(
      browser(), second->GetContents());
  ASSERT_TRUE(second_is_active);
  EXPECT_TRUE(*second_is_active);
}

TEST_F(SessionRestoreIntegrationTest,
       RestoredTemporaryTabDoesNotAutoBindByUrl) {
  const base::Uuid second_workspace = CreateSecondWorkspace();
  ASSERT_TRUE(second_workspace.is_valid());
  const GURL url("https://example.test/same-url-temporary");
  const tab_tree::TreeNode saved_page = MakeSavedPage(second_workspace, url);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(saved_page));

  AddTab(browser(), url);
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(tab);
  const TabSessionMetadata restored{
      .workspace_id = second_workspace,
      .tree_node_id = std::nullopt,
      .last_active_in_workspace = true,
  };
  std::map<std::string, std::string> extra_data;
  extra_data.emplace(kTabSessionMetadataExtraDataKey,
                     EncodeTabSessionMetadata(restored).value());
  ASSERT_TRUE(RestoreTabSessionExtraData(browser(), tab, extra_data));

  task_environment()->RunUntilIdle();
  EXPECT_EQ(second_workspace, bridge_->GetWorkspaceForTab(tab));
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());
}

TEST_F(SessionRestoreIntegrationTest,
       LastActiveFlagFollowsChromiumTabSelection) {
  const base::Uuid workspace_id =
      workspace_service_->ordered_workspaces().front().id;
  AddTab(browser(), GURL("https://example.test/first-active"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* first = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(first);

  AddTab(browser(), GURL("https://example.test/second-active"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* second = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(second);
  ASSERT_NE(first, second);

  const std::optional<TabSessionMetadata> first_metadata =
      bridge_->GetTabSessionMetadata(first);
  const std::optional<TabSessionMetadata> second_metadata =
      bridge_->GetTabSessionMetadata(second);
  ASSERT_TRUE(first_metadata.has_value());
  ASSERT_TRUE(second_metadata.has_value());
  EXPECT_FALSE(first_metadata->last_active_in_workspace);
  EXPECT_TRUE(second_metadata->last_active_in_workspace);
  EXPECT_EQ(second,
            bridge_->GetLastActiveTabForWorkspace(browser(), workspace_id));

  browser()->GetTabStripModel()->ActivateTabAt(
      browser()->GetTabStripModel()->GetIndexOfTab(first));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(first,
            bridge_->GetLastActiveTabForWorkspace(browser(), workspace_id));
}

TEST_F(SessionRestoreIntegrationTest,
       StaleWorkspaceFallsBackAndConflictingNodeIsIgnored) {
  const base::Uuid first_workspace =
      workspace_service_->ordered_workspaces().front().id;
  const base::Uuid second_workspace = CreateSecondWorkspace();
  ASSERT_TRUE(second_workspace.is_valid());
  const GURL url("https://example.test/stale-workspace");
  const tab_tree::TreeNode saved_page = MakeSavedPage(second_workspace, url);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge_->tab_tree_store()->CreateNode(saved_page));

  AddTab(browser(), url);
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(tab);
  const TabSessionMetadata stale{
      .workspace_id = base::Uuid::GenerateRandomV4(),
      .tree_node_id = saved_page.id,
      .last_active_in_workspace = true,
  };
  std::map<std::string, std::string> extra_data;
  extra_data.emplace(kTabSessionMetadataExtraDataKey,
                     EncodeTabSessionMetadata(stale).value());
  ASSERT_TRUE(RestoreTabSessionExtraData(browser(), tab, extra_data));

  task_environment()->RunUntilIdle();
  EXPECT_EQ(first_workspace, bridge_->GetWorkspaceForTab(tab));
  EXPECT_FALSE(bridge_->FindTreeNodeIdForTab(tab).has_value());

  extra_data[kTabSessionMetadataExtraDataKey] = "not-json";
  EXPECT_FALSE(RestoreTabSessionExtraData(browser(), tab, extra_data));
  EXPECT_EQ(first_workspace, bridge_->GetWorkspaceForTab(tab));
}

}  // namespace

}  // namespace ahoi::session
