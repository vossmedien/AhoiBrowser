// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

tab_tree::Workspace Workspace(base::Uuid id, std::u16string name) {
  return {
      .id = id,
      .name = std::move(name),
      .sort_key = "0000000000",
      .created_at = base::Time::UnixEpoch(),
      .modified_at = base::Time::UnixEpoch(),
  };
}

tab_tree::TreeNode Folder(base::Uuid id,
                          base::Uuid workspace_id,
                          std::u16string title,
                          std::optional<base::Uuid> parent_id = std::nullopt) {
  return {
      .id = id,
      .workspace_id = workspace_id,
      .parent_id = parent_id,
      .type = tab_tree::TreeNodeType::kFolder,
      .title = std::move(title),
      .icon = u"folder",
      .sort_key = "0000000000",
      .created_at = base::Time::UnixEpoch(),
      .modified_at = base::Time::UnixEpoch(),
  };
}

tab_tree::TreeNode Page(base::Uuid id,
                        base::Uuid workspace_id,
                        std::u16string title,
                        std::optional<base::Uuid> parent_id = std::nullopt) {
  return {
      .id = id,
      .workspace_id = workspace_id,
      .parent_id = parent_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = std::move(title),
      .url = GURL("https://import.test/"),
      .sort_key = "0000000001",
      .created_at = base::Time::UnixEpoch(),
      .modified_at = base::Time::UnixEpoch(),
  };
}

ArcImportPlan ImportPlan(std::u16string workspace_name = u"Arc") {
  const base::Uuid workspace_id = Id("11111111-1111-5111-8111-111111111111");
  const base::Uuid folder_id = Id("22222222-2222-5222-8222-222222222222");
  const base::Uuid first_page_id = Id("33333333-3333-5333-8333-333333333333");
  const base::Uuid second_page_id = Id("44444444-4444-5444-8444-444444444444");
  ArcImportPlan plan;
  plan.tree.workspaces.push_back(
      Workspace(workspace_id, std::move(workspace_name)));
  plan.tree.nodes.push_back(Folder(folder_id, workspace_id, u"Split"));
  plan.tree.nodes.push_back(
      Page(first_page_id, workspace_id, u"First", folder_id));
  plan.tree.nodes.push_back(
      Page(second_page_id, workspace_id, u"Second", folder_id));
  plan.splits.push_back({
      .folder_node_id = folder_id,
      .member_node_ids = {first_page_id, second_page_id},
      .orientation = ArcSplitOrientation::kHorizontal,
      .focused_member_node_id = second_page_id,
      .normalized_ratios = {0.4, 0.6},
  });
  plan.stats.imported_workspace_count = 1;
  plan.stats.imported_folder_count = 1;
  plan.stats.imported_page_count = 2;
  plan.stats.imported_split_count = 1;
  return plan;
}

tab_tree::TabTreeSnapshot ExistingTree(std::u16string workspace_name) {
  const base::Uuid workspace_id = Id("aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaaa");
  tab_tree::TabTreeSnapshot tree;
  tree.workspaces.push_back(Workspace(workspace_id, std::move(workspace_name)));
  tree.nodes.push_back(Page(Id("bbbbbbbb-bbbb-5bbb-8bbb-bbbbbbbbbbbb"),
                            workspace_id, u"Existing"));
  return tree;
}

TEST(ArcImportTransactionTest, RenameIsAdditiveAndLeavesInputUntouched) {
  const tab_tree::TabTreeSnapshot current = ExistingTree(u"Arc");
  const tab_tree::TabTreeSnapshot unchanged = current;

  const ArcImportMergeResult merged = MergeArcImportPlan(
      current, ImportPlan(u"Arc"), ArcConflictResolution::kRename);

  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.merged_tree.has_value());
  ASSERT_TRUE(merged.applied_plan.has_value());
  EXPECT_EQ(unchanged, current);
  EXPECT_EQ(2u, merged.merged_tree->workspaces.size());
  EXPECT_EQ(u"Arc (Arc)", merged.merged_tree->workspaces.back().name);
  EXPECT_EQ(4u, merged.merged_tree->nodes.size());
  EXPECT_EQ(1u, merged.renamed_workspace_count);
  EXPECT_EQ(1u, merged.applied_plan->splits.size());
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_folder_count);
  EXPECT_EQ(2u, merged.applied_plan->stats.imported_page_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_split_count);
}

TEST(ArcImportTransactionTest, RenamedWorkspaceSecondMergeIsNoOp) {
  const tab_tree::TabTreeSnapshot current = ExistingTree(u"Arc");
  const ArcImportPlan plan = ImportPlan(u"Arc");
  const ArcImportMergeResult first =
      MergeArcImportPlan(current, plan, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, first.status);
  ASSERT_TRUE(first.merged_tree.has_value());

  const ArcImportMergeResult second = MergeArcImportPlan(
      *first.merged_tree, plan, ArcConflictResolution::kRename);

  ASSERT_EQ(ArcImportStatus::kNoChanges, second.status);
  ASSERT_TRUE(second.merged_tree.has_value());
  EXPECT_EQ(*first.merged_tree, *second.merged_tree);
  EXPECT_FALSE(second.changed);
  EXPECT_EQ(0u, second.renamed_workspace_count);
  ASSERT_TRUE(second.applied_plan.has_value());
  EXPECT_EQ(0u, second.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(0u, second.applied_plan->stats.imported_folder_count);
  EXPECT_EQ(0u, second.applied_plan->stats.imported_page_count);
  EXPECT_EQ(0u, second.applied_plan->stats.imported_split_count);
  EXPECT_EQ(1u, second.applied_plan->stats.deduplicated_workspace_count);
  EXPECT_EQ(3u, second.applied_plan->stats.deduplicated_item_count);
  EXPECT_EQ(1u, second.applied_plan->stats.deduplicated_split_count);
}

TEST(ArcImportTransactionTest, SkipConflictProducesNoMutation) {
  const tab_tree::TabTreeSnapshot current = ExistingTree(u"Arc");
  const ArcImportMergeResult merged = MergeArcImportPlan(
      current, ImportPlan(u"Arc"), ArcConflictResolution::kSkip);

  ASSERT_EQ(ArcImportStatus::kNoChanges, merged.status);
  ASSERT_TRUE(merged.merged_tree.has_value());
  EXPECT_EQ(current, *merged.merged_tree);
  EXPECT_EQ(1u, merged.skipped_workspace_count);
  ASSERT_TRUE(merged.applied_plan.has_value());
  EXPECT_TRUE(merged.applied_plan->tree.workspaces.empty());
  EXPECT_TRUE(merged.applied_plan->tree.nodes.empty());
  EXPECT_TRUE(merged.applied_plan->splits.empty());
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_folder_count);
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_page_count);
  EXPECT_EQ(0u, merged.applied_plan->stats.deduplicated_split_count);
}

TEST(ArcImportTransactionTest, MergeTargetsExistingWorkspaceWithoutOverwrite) {
  const tab_tree::TabTreeSnapshot current = ExistingTree(u"Arc");
  const base::Uuid existing_workspace_id = current.workspaces.front().id;
  const ArcImportMergeResult merged = MergeArcImportPlan(
      current, ImportPlan(u"Arc"), ArcConflictResolution::kMerge);

  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.merged_tree.has_value());
  EXPECT_EQ(1u, merged.merged_tree->workspaces.size());
  EXPECT_EQ(4u, merged.merged_tree->nodes.size());
  EXPECT_EQ(current.nodes.front(), merged.merged_tree->nodes.front());
  EXPECT_EQ(1u, merged.merged_workspace_count);
  for (size_t index = current.nodes.size();
       index < merged.merged_tree->nodes.size(); ++index) {
    EXPECT_EQ(existing_workspace_id,
              merged.merged_tree->nodes[index].workspace_id);
  }
}

TEST(ArcImportTransactionTest,
     MergeNoOpRetainsRemappedRuntimeSplitAndDeduplicationReceipt) {
  const tab_tree::TabTreeSnapshot current = ExistingTree(u"Arc");
  const base::Uuid target_workspace_id = current.workspaces.front().id;
  const ArcImportPlan plan = ImportPlan(u"Arc");
  const ArcImportMergeResult first =
      MergeArcImportPlan(current, plan, ArcConflictResolution::kMerge);
  ASSERT_EQ(ArcImportStatus::kOk, first.status);
  ASSERT_TRUE(first.merged_tree.has_value());

  const ArcImportMergeResult replay = MergeArcImportPlan(
      *first.merged_tree, plan, ArcConflictResolution::kMerge);

  ASSERT_EQ(ArcImportStatus::kNoChanges, replay.status);
  ASSERT_TRUE(replay.merged_tree.has_value());
  ASSERT_TRUE(replay.applied_plan.has_value());
  EXPECT_EQ(*first.merged_tree, *replay.merged_tree);
  EXPECT_FALSE(replay.changed);
  ASSERT_EQ(1u, replay.applied_plan->tree.workspaces.size());
  EXPECT_EQ(target_workspace_id,
            replay.applied_plan->tree.workspaces.front().id);
  ASSERT_EQ(3u, replay.applied_plan->tree.nodes.size());
  for (const tab_tree::TreeNode& node : replay.applied_plan->tree.nodes) {
    EXPECT_EQ(target_workspace_id, node.workspace_id);
  }
  ASSERT_EQ(1u, replay.applied_plan->splits.size());
  EXPECT_EQ(1u, replay.applied_plan->stats.deduplicated_workspace_count);
  EXPECT_EQ(3u, replay.applied_plan->stats.deduplicated_item_count);
  EXPECT_EQ(1u, replay.applied_plan->stats.deduplicated_split_count);
}

TEST(ArcImportTransactionTest,
     PartialIdempotentSplitKeepsCompleteRuntimeStructure) {
  const ArcImportPlan plan = ImportPlan();
  tab_tree::TabTreeSnapshot current;
  current.workspaces.push_back(plan.tree.workspaces.front());
  current.nodes.push_back(plan.tree.nodes[0]);
  current.nodes.push_back(plan.tree.nodes[1]);

  const ArcImportMergeResult merged =
      MergeArcImportPlan(current, plan, ArcConflictResolution::kRename);

  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.merged_tree.has_value());
  ASSERT_TRUE(merged.applied_plan.has_value());
  EXPECT_EQ(3u, merged.merged_tree->nodes.size());
  EXPECT_EQ(1u, merged.applied_plan->tree.workspaces.size());
  EXPECT_EQ(3u, merged.applied_plan->tree.nodes.size());
  EXPECT_EQ(1u, merged.applied_plan->splits.size());
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_folder_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_page_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_split_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.deduplicated_workspace_count);
  EXPECT_EQ(2u, merged.applied_plan->stats.deduplicated_item_count);
  EXPECT_EQ(0u, merged.applied_plan->stats.deduplicated_split_count);
  for (const tab_tree::TreeNode& node : merged.applied_plan->tree.nodes) {
    EXPECT_EQ(plan.tree.workspaces.front().id, node.workspace_id);
  }
}

TEST(ArcImportTransactionTest,
     PartialSplitMergedIntoExistingWorkspaceUsesRemappedRuntimeNodes) {
  const ArcImportPlan plan = ImportPlan(u"Arc");
  const ArcImportMergeResult complete = MergeArcImportPlan(
      ExistingTree(u"Arc"), plan, ArcConflictResolution::kMerge);
  ASSERT_EQ(ArcImportStatus::kOk, complete.status);
  ASSERT_TRUE(complete.merged_tree.has_value());
  tab_tree::TabTreeSnapshot partial = *complete.merged_tree;
  const base::Uuid missing_id = plan.tree.nodes.back().id;
  std::erase_if(partial.nodes, [&](const tab_tree::TreeNode& node) {
    return node.id == missing_id;
  });

  const ArcImportMergeResult merged =
      MergeArcImportPlan(partial, plan, ArcConflictResolution::kMerge);

  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.applied_plan.has_value());
  ASSERT_EQ(1u, merged.applied_plan->tree.workspaces.size());
  ASSERT_EQ(3u, merged.applied_plan->tree.nodes.size());
  const base::Uuid target_workspace_id = partial.workspaces.front().id;
  EXPECT_EQ(target_workspace_id,
            merged.applied_plan->tree.workspaces.front().id);
  for (const tab_tree::TreeNode& node : merged.applied_plan->tree.nodes) {
    EXPECT_EQ(target_workspace_id, node.workspace_id);
  }
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_page_count);
  EXPECT_EQ(2u, merged.applied_plan->stats.deduplicated_item_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.imported_split_count);
}

TEST(ArcImportTransactionTest, IdentityConflictReturnsNoReplacementSnapshot) {
  tab_tree::TabTreeSnapshot current = ExistingTree(u"Existing");
  ArcImportPlan plan = ImportPlan(u"Arc");
  tab_tree::TreeNode conflicting = plan.tree.nodes.front();
  conflicting.workspace_id = current.workspaces.front().id;
  conflicting.title = u"Different existing row";
  current.nodes.push_back(std::move(conflicting));
  const tab_tree::TabTreeSnapshot unchanged = current;

  const ArcImportMergeResult merged =
      MergeArcImportPlan(current, plan, ArcConflictResolution::kRename);

  EXPECT_EQ(ArcImportStatus::kConflict, merged.status);
  EXPECT_FALSE(merged.merged_tree.has_value());
  EXPECT_FALSE(merged.applied_plan.has_value());
  EXPECT_EQ(unchanged, current);
}

TEST(ArcImportTransactionTest, SameSnapshotSecondMergeIsDeterministicNoOp) {
  const ArcImportPlan plan = ImportPlan();
  const ArcImportMergeResult first =
      MergeArcImportPlan({}, plan, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, first.status);
  ASSERT_TRUE(first.merged_tree.has_value());

  const ArcImportMergeResult second = MergeArcImportPlan(
      *first.merged_tree, plan, ArcConflictResolution::kRename);

  ASSERT_EQ(ArcImportStatus::kNoChanges, second.status);
  ASSERT_TRUE(second.merged_tree.has_value());
  EXPECT_EQ(*first.merged_tree, *second.merged_tree);
  EXPECT_FALSE(second.changed);
}

TEST(ArcImportTransactionTest, InvalidPlanFailsClosedWithoutCandidateTree) {
  ArcImportPlan plan = ImportPlan();
  plan.tree.nodes.back().parent_id = Id("dddddddd-dddd-5ddd-8ddd-dddddddddddd");

  const ArcImportMergeResult merged = MergeArcImportPlan(
      ExistingTree(u"Existing"), plan, ArcConflictResolution::kRename);

  EXPECT_EQ(ArcImportStatus::kTransactionFailed, merged.status);
  EXPECT_FALSE(merged.merged_tree.has_value());
  EXPECT_FALSE(merged.applied_plan.has_value());
}

}  // namespace

}  // namespace ahoi::importer::arc
