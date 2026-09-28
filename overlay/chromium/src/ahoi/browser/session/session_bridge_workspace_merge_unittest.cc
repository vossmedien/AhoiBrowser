// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_unittest_support.h"
#include "ahoi/browser/session/session_prefs.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// ADR 0012 / handoff 080: "Zusammenführen mit …" through the session bridge.
namespace ahoi {
namespace {

using test_support::MakeSavedPage;
using test_support::SessionBridgeTest;
using Result = tab_tree::TabTreeStore::Result;

class SessionBridgeWorkspaceMergeTest : public SessionBridgeTest {
 protected:
  Result Merge(const base::Uuid& source, const base::Uuid& target,
               bool into_folder = true) {
    base::test::TestFuture<Result> done;
    bridge_->MergeWorkspace(source, target, into_folder, done.GetCallback());
    return done.Get();
  }

  bool Lists(const base::Uuid& workspace_id) {
    for (const tab_tree::Workspace& workspace :
         workspace_service_->ordered_workspaces()) {
      if (workspace.id == workspace_id) {
        return true;
      }
    }
    return false;
  }
};

TEST_F(SessionBridgeWorkspaceMergeTest, SharedMergeKeepsTheLiveTabAndUndoes) {
  const base::Uuid target_id =
      workspace_service_->ordered_workspaces().front().id;
  const std::optional<base::Uuid> source_id =
      bridge_->CreateWorkspace(u"Research", u"R", std::nullopt);
  ASSERT_TRUE(source_id.has_value());
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->CreateNode(MakeSavedPage(
                             *source_id, GURL("https://example.test/paper"))));
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), *source_id, WorkspaceActivationSource::kKeyboard));
  AddTab(browser(), GURL("https://example.test/open"));
  task_environment()->RunUntilIdle();
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetActiveTab();
  ASSERT_TRUE(tab);
  ASSERT_EQ(bridge_->GetWorkspaceForTab(tab), *source_id);

  // A routing rule that named the source follows it into the target.
  navigation::RoutingSettings routing;
  routing.rules.push_back({.id = base::Uuid::GenerateRandomV4(),
                           .host = "example.test",
                           .target_workspace_id = *source_id});
  ASSERT_TRUE(navigation::WriteRoutingSettings(profile()->GetPrefs(), routing));

  EXPECT_TRUE(bridge_->SharesWebContext(*source_id, target_id));
  ASSERT_EQ(Result::kOk, Merge(*source_id, target_id));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(Lists(*source_id));
  EXPECT_EQ(bridge_->GetActiveWorkspaceForWindow(browser()), target_id);
  EXPECT_EQ(bridge_->GetWorkspaceForTab(tab), target_id);
  EXPECT_EQ(navigation::ReadRoutingSettings(*profile()->GetPrefs())
                .rules.front()
                .target_workspace_id,
            target_id);

  // WS-MERGE-03: undo brings the source and its open tab back.
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->UndoLastMutation());
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(Lists(*source_id));
  EXPECT_EQ(bridge_->GetWorkspaceForTab(tab), *source_id);
  // Crest 134: the rule this merge retargeted names the source again.
  EXPECT_EQ(navigation::ReadRoutingSettings(*profile()->GetPrefs())
                .rules.front()
                .target_workspace_id,
            *source_id);
  EXPECT_TRUE(profile()
                  ->GetPrefs()
                  ->GetDict(session::kWorkspaceMergeRoutingReceiptsPref)
                  .empty());
}

TEST_F(SessionBridgeWorkspaceMergeTest, EmptySourceUndoRestoresOnlyItsRouting) {
  const base::Uuid target_id =
      workspace_service_->ordered_workspaces().front().id;
  const std::optional<base::Uuid> source_id =
      bridge_->CreateWorkspace(u"Empty", u"E", std::nullopt);
  ASSERT_TRUE(source_id.has_value());
  navigation::RoutingSettings routing;
  const base::Uuid moved_rule = base::Uuid::GenerateRandomV4();
  const base::Uuid own_rule = base::Uuid::GenerateRandomV4();
  routing.rules.push_back({.id = moved_rule,
                           .host = "moved.test",
                           .target_workspace_id = *source_id});
  routing.rules.push_back({.id = own_rule,
                           .host = "target.test",
                           .target_workspace_id = target_id});
  routing.default_route.target_workspace_id = *source_id;
  ASSERT_TRUE(navigation::WriteRoutingSettings(profile()->GetPrefs(), routing));

  ASSERT_EQ(Result::kOk, Merge(*source_id, target_id));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(Lists(*source_id));
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->UndoLastMutation());
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(Lists(*source_id));
  const navigation::RoutingSettings restored =
      navigation::ReadRoutingSettings(*profile()->GetPrefs());
  ASSERT_EQ(2u, restored.rules.size());
  EXPECT_EQ(*source_id, restored.rules[0].target_workspace_id);
  // A rule that already named the target before the merge stays there.
  EXPECT_EQ(target_id, restored.rules[1].target_workspace_id);
  EXPECT_EQ(*source_id, restored.default_route.target_workspace_id);
  EXPECT_EQ(Result::kNothingToUndo,
            bridge_->tab_tree_store()->UndoLastMutation());
}

TEST_F(SessionBridgeWorkspaceMergeTest, OwnSessionsAreRetiredWithoutUndo) {
  const base::Uuid target_id =
      workspace_service_->ordered_workspaces().front().id;
  const std::optional<base::Uuid> source_id = bridge_->CreateWorkspace(
      u"Client", u"C", std::nullopt, /*own_website_sessions=*/true);
  ASSERT_TRUE(source_id.has_value());
  const tab_tree::TreeNode page =
      MakeSavedPage(*source_id, GURL("https://example.test/client"));
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->CreateNode(page));
  EXPECT_FALSE(bridge_->SharesWebContext(*source_id, target_id));

  // No open page of the source: nothing to ask, the merge commits at once.
  ASSERT_EQ(Result::kOk, Merge(*source_id, target_id, /*into_folder=*/false));
  EXPECT_FALSE(Lists(*source_id));
  EXPECT_FALSE(bridge_->HasOwnWebsiteSessions(*source_id));
  EXPECT_FALSE(
      session::FindWebsiteSessionBinding(profile()->GetPrefs(), *source_id)
          .has_value());
  tab_tree::TreeNode moved;
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->GetNode(page.id, &moved));
  EXPECT_EQ(moved.workspace_id, target_id);
  // Its sessions are gone, so the merge left no undo entry: the next undo is
  // the page's creation.
  ASSERT_EQ(Result::kOk, bridge_->tab_tree_store()->UndoLastMutation());
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(Lists(*source_id));
}

TEST_F(SessionBridgeWorkspaceMergeTest, RejectsSelfAndUnknownTargets) {
  const base::Uuid only_id =
      workspace_service_->ordered_workspaces().front().id;
  EXPECT_EQ(Result::kInvalidArgument, Merge(only_id, only_id));
  EXPECT_EQ(Result::kInvalidArgument,
            Merge(only_id, base::Uuid::GenerateRandomV4()));
  EXPECT_TRUE(Lists(only_id));
}

}  // namespace
}  // namespace ahoi
