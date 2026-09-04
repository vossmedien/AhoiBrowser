// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_runtime.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {
namespace {

tab_tree::Workspace ImportedWorkspace(const base::Uuid& id,
                                      const base::Time& now) {
  return {
      .id = id,
      .name = u"Imported workspace",
      .icon = u"I",
      .sort_key = "zzzzzzzz",
      .created_at = now,
      .modified_at = now,
  };
}

tab_tree::TreeNode SplitFolder(const base::Uuid& id,
                               const base::Uuid& workspace_id,
                               size_t split_index,
                               const base::Time& now) {
  return {
      .id = id,
      .workspace_id = workspace_id,
      .type = tab_tree::TreeNodeType::kFolder,
      .title = u"Imported split",
      .icon = u"folder",
      .sort_key = base::StringPrintf("folder-%02zu", split_index),
      .created_at = now,
      .modified_at = now,
  };
}

tab_tree::TreeNode SplitMember(const base::Uuid& id,
                               const base::Uuid& workspace_id,
                               const base::Uuid& folder_id,
                               size_t split_index,
                               size_t member_index,
                               const base::Time& now) {
  return {
      .id = id,
      .workspace_id = workspace_id,
      .parent_id = folder_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = u"Imported page",
      .url = GURL(base::StringPrintf(
          "https://arc-runtime-%zu-%zu.example.test/", split_index,
          member_index)),
      .sort_key = base::StringPrintf("member-%02zu", member_index),
      .created_at = now,
      .modified_at = now,
  };
}

ArcImportPlan RealSplitShapePlan() {
  constexpr std::array<size_t, 3> kMemberCounts = {2u, 2u, 3u};
  constexpr size_t kImportedFolderCount = 36u;
  constexpr size_t kImportedPageCount = 133u;
  const base::Time now = base::Time::Now();
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();

  ArcImportPlan plan;
  plan.tree.workspaces.push_back(ImportedWorkspace(workspace_id, now));
  for (size_t split_index = 0; split_index < kMemberCounts.size();
       ++split_index) {
    const base::Uuid folder_id = base::Uuid::GenerateRandomV4();
    plan.tree.nodes.push_back(
        SplitFolder(folder_id, workspace_id, split_index, now));

    ArcSplitDescriptor split;
    split.folder_node_id = folder_id;
    split.orientation = ArcSplitOrientation::kHorizontal;
    for (size_t member_index = 0;
         member_index < kMemberCounts[split_index]; ++member_index) {
      const base::Uuid member_id = base::Uuid::GenerateRandomV4();
      plan.tree.nodes.push_back(SplitMember(member_id, workspace_id, folder_id,
                                            split_index, member_index, now));
      split.member_node_ids.push_back(member_id);
    }
    split.focused_member_node_id = split.member_node_ids.front();
    if (split_index == 0u) {
      split.normalized_ratios = {0.476821400582, 0.523178599418};
    } else {
      split.normalized_ratios.assign(kMemberCounts[split_index],
                                     1.0 / kMemberCounts[split_index]);
    }
    plan.splits.push_back(std::move(split));
  }

  // Match the complete real Arc preview shape without materializing unrelated
  // pages as live tabs. The runtime must reconstruct only the seven split
  // members even when the imported tree contains all 36 folders and 133 pages.
  const size_t split_folder_count = plan.splits.size();
  const size_t split_member_count = 7u;
  const base::Uuid filler_folder_id = base::Uuid::GenerateRandomV4();
  for (size_t folder_index = split_folder_count;
       folder_index < kImportedFolderCount; ++folder_index) {
    const base::Uuid folder_id = folder_index == split_folder_count
                                     ? filler_folder_id
                                     : base::Uuid::GenerateRandomV4();
    plan.tree.nodes.push_back(
        SplitFolder(folder_id, workspace_id, folder_index, now));
  }
  for (size_t page_index = split_member_count; page_index < kImportedPageCount;
       ++page_index) {
    plan.tree.nodes.push_back({
        .id = base::Uuid::GenerateRandomV4(),
        .workspace_id = workspace_id,
        .parent_id = filler_folder_id,
        .type = tab_tree::TreeNodeType::kSavedPage,
        .title = u"Imported non-split page",
        .url = GURL(base::StringPrintf(
            "https://arc-non-split-%zu.example.test/", page_index)),
        .sort_key = base::StringPrintf("page-%03zu", page_index),
        .created_at = now,
        .modified_at = now,
    });
  }
  plan.stats.imported_workspace_count = 1u;
  plan.stats.imported_folder_count = kImportedFolderCount;
  plan.stats.imported_page_count = kImportedPageCount;
  plan.stats.imported_split_count = plan.splits.size();
  return plan;
}

class ArcSplitRuntimeBrowserTest : public InProcessBrowserTest {
 public:
  ArcSplitRuntimeBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(tabs::kSplitViewHorizontal);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(ArcSplitRuntimeBrowserTest,
                       ReconstructsRealShapeWhileImportWindowIsBackgrounded) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ASSERT_TRUE(browser_view->IsAhoiBrowserSurface());

  SessionBridge* const bridge =
      SessionBridgeFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(bridge);
  ASSERT_TRUE(bridge->is_operational());
  base::RunLoop bridge_ready;
  bridge->RunWhenReadyForTesting(bridge_ready.QuitClosure());
  bridge_ready.Run();
  ASSERT_TRUE(bridge->is_ready());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/importData")));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1,
                   /*foreground=*/false);

  TabStripModel* const model = browser()->tab_strip_model();
  ASSERT_EQ(2, model->count());
  model->ActivateTabAt(0);
  tabs::TabInterface* const original_tab = model->GetActiveTab();
  ASSERT_TRUE(original_tab);
  const std::optional<base::Uuid> original_workspace =
      bridge->GetActiveWorkspaceForWindow(browser());
  ASSERT_TRUE(original_workspace.has_value());
  ASSERT_EQ(original_workspace, bridge->GetWorkspaceForTab(original_tab));

  const ArcImportPlan plan = RealSplitShapePlan();
  ASSERT_TRUE(IsValidArcSplitStructure(plan));
  ASSERT_EQ(169u, plan.tree.nodes.size());
  tab_tree::TabTreeSnapshot merged_tree;
  ASSERT_TRUE(bridge->ExportTabTreeSnapshot(&merged_tree));
  merged_tree.workspaces.insert(merged_tree.workspaces.end(),
                                plan.tree.workspaces.begin(),
                                plan.tree.workspaces.end());
  merged_tree.nodes.insert(merged_tree.nodes.end(), plan.tree.nodes.begin(),
                           plan.tree.nodes.end());
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            bridge->ApplySyncedTabTreeSnapshot(merged_tree));
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(original_workspace,
            bridge->GetActiveWorkspaceForWindow(browser()));
  ASSERT_EQ(original_tab, model->GetActiveTab());
  ASSERT_FALSE(original_tab->GetSplit().has_value());

  Browser* const foreground_browser = CreateBrowser(browser()->GetProfile());
  ui_test_utils::WaitUntilBrowserBecomeActive(foreground_browser);
  ASSERT_FALSE(browser()->IsActive());
  ASSERT_TRUE(foreground_browser->IsActive());

  ArcSplitRuntimeResult result =
      ReconstructArcSplits(browser(), bridge, plan);

  EXPECT_FALSE(original_tab->GetSplit().has_value());
  ASSERT_EQ(ArcImportStatus::kOk, result.status);
  EXPECT_EQ(3u, result.reconstructed_split_count);
  EXPECT_EQ(7u, result.opened_tabs.size());
  EXPECT_EQ(ArcSplitVerification::kExact,
            VerifyArcSplitRuntime(browser(), bridge, plan,
                                  /*require_focus=*/true));
  EXPECT_EQ(plan.tree.workspaces.front().id,
            bridge->GetActiveWorkspaceForWindow(browser()));
  EXPECT_FALSE(browser()->IsActive());
  EXPECT_TRUE(foreground_browser->IsActive());

  for (const ArcSplitDescriptor& split : plan.splits) {
    std::vector<tabs::TabInterface*> expected_tabs;
    for (const base::Uuid& member_id : split.member_node_ids) {
      tabs::TabInterface* const member =
          bridge->FindTabByTreeNodeId(member_id);
      ASSERT_TRUE(member);
      ASSERT_TRUE(member->GetSplit().has_value());
      expected_tabs.push_back(member);
    }
    const split_tabs::SplitTabId split_id = *expected_tabs.front()->GetSplit();
    ASSERT_TRUE(model->ContainsSplit(split_id));
    ASSERT_TRUE(model->GetSplitData(split_id));
    EXPECT_EQ(expected_tabs, model->GetSplitData(split_id)->ListTabs());
  }

  EXPECT_EQ(bridge->FindTabByTreeNodeId(
                plan.splits.back().focused_member_node_id),
            model->GetActiveTab());
}

}  // namespace
}  // namespace ahoi::importer::arc
