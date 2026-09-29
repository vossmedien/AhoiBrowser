// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
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
  const ArcImportPlan plan = ImportPlan(u"Arc");

  const ArcImportMergeResult merged = MergeArcImportPlan(
      current, plan, ArcConflictResolution::kRename);

  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.merged_tree.has_value());
  ASSERT_TRUE(merged.applied_plan.has_value());
  EXPECT_EQ(unchanged, current);
  EXPECT_EQ(2u, merged.merged_tree->workspaces.size());
  const auto imported_workspace = std::ranges::find(
      merged.merged_tree->workspaces, plan.tree.workspaces.front().id,
      &tab_tree::Workspace::id);
  ASSERT_NE(merged.merged_tree->workspaces.end(), imported_workspace);
  EXPECT_EQ(u"Arc (Arc)", imported_workspace->name);
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
  const auto existing_node = std::ranges::find(
      merged.merged_tree->nodes, current.nodes.front().id,
      &tab_tree::TreeNode::id);
  ASSERT_NE(merged.merged_tree->nodes.end(), existing_node);
  EXPECT_EQ(current.nodes.front(), *existing_node);
  EXPECT_EQ(1u, merged.merged_workspace_count);
  ASSERT_TRUE(merged.applied_plan.has_value());
  EXPECT_EQ(0u, merged.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(1u, merged.applied_plan->stats.deduplicated_workspace_count);
  for (const auto& node : merged.merged_tree->nodes) {
    EXPECT_EQ(existing_workspace_id, node.workspace_id);
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

TEST(ArcImportTransactionTest,
     CanonicalMergeMatchesStoreReadbackAndPreservesUndoAndSourceOrdering) {
  using Store = tab_tree::TabTreeStore;
  const base::Uuid existing_workspace_id =
      Id("aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaaa");
  const base::Uuid existing_folder_id =
      Id("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeeee");
  // This node sorts BETWEEN the imported members, not before/after the entire
  // source plan. The existing folder sorts after its child, exercising the
  // store's parent-safe replacement independently of export ordering.
  const base::Uuid existing_page_id =
      Id("35000000-0000-5000-8000-000000000000");
  auto workspace = Workspace(existing_workspace_id, u"Existing workspace");
  workspace.sort_key = "z-existing";
  workspace.icon = u"W";
  workspace.accent_argb = 0xff4682b4u;
  auto folder =
      Folder(existing_folder_id, existing_workspace_id, u"Existing folder");
  folder.icon = u"code";
  folder.accent_argb = 0xff77aaccu;
  const auto original_page = Page(existing_page_id, existing_workspace_id,
                                  u"Before rename", existing_folder_id);
  tab_tree::TabTreeSnapshot seed;
  seed.workspaces = {workspace};
  seed.nodes = {folder, original_page};
  Store store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_EQ(Store::Result::kOk, store.ReplaceWithSnapshot(seed));
  ASSERT_EQ(Store::Result::kOk,
            store.RenameNode(existing_page_id, u"After rename",
                             base::Time::UnixEpoch() + base::Seconds(30)));
  tab_tree::TabTreeSnapshot current;
  ASSERT_EQ(Store::Result::kOk, store.ExportSnapshot(&current));
  ASSERT_EQ(1u, current.undo_operations.size());
  const tab_tree::TabTreeSnapshot unchanged_current = current;

  ArcImportPlan plan = ImportPlan(u"Imported workspace");
  plan.tree.workspaces.front().sort_key = "a-imported";
  // Source projection order and split member order are separate from the
  // canonical persistence vector; neither may be silently sorted in-place.
  std::ranges::reverse(plan.tree.nodes);
  std::ranges::reverse(plan.splits.front().member_node_ids);
  std::ranges::reverse(plan.splits.front().normalized_ratios);
  const ArcImportPlan unchanged_plan = plan;
  const ArcImportMergeResult merged =
      MergeArcImportPlan(current, plan, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_TRUE(merged.merged_tree);
  ASSERT_TRUE(merged.applied_plan);
  EXPECT_EQ(unchanged_current, current);
  EXPECT_EQ(unchanged_plan, plan);
  EXPECT_EQ(plan.tree, merged.applied_plan->tree);
  EXPECT_EQ(plan.splits, merged.applied_plan->splits);
  EXPECT_EQ(current.undo_operations, merged.merged_tree->undo_operations);
  ASSERT_EQ(2u, merged.merged_tree->workspaces.size());
  EXPECT_EQ(plan.tree.workspaces.front(), merged.merged_tree->workspaces.front());
  EXPECT_EQ(workspace, merged.merged_tree->workspaces.back());
  ASSERT_EQ(5u, merged.merged_tree->nodes.size());
  EXPECT_EQ(existing_page_id, merged.merged_tree->nodes[2].id);
  for (const auto& node : current.nodes) {
    const auto actual = std::ranges::find(merged.merged_tree->nodes, node.id,
                                          &tab_tree::TreeNode::id);
    ASSERT_NE(merged.merged_tree->nodes.end(), actual);
    EXPECT_EQ(node, *actual);
  }
  for (const auto& node : plan.tree.nodes) {
    const auto actual = std::ranges::find(merged.merged_tree->nodes, node.id,
                                          &tab_tree::TreeNode::id);
    ASSERT_NE(merged.merged_tree->nodes.end(), actual);
    EXPECT_EQ(node, *actual);
  }

  ASSERT_EQ(Store::Result::kOk, store.ReplaceWithSnapshot(*merged.merged_tree));
  tab_tree::TabTreeSnapshot durable;
  ASSERT_EQ(Store::Result::kOk, store.ExportSnapshot(&durable));
  EXPECT_EQ(*merged.merged_tree, durable);
  const ArcImportMergeResult replay =
      MergeArcImportPlan(durable, plan, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kNoChanges, replay.status);
  ASSERT_TRUE(replay.merged_tree);
  ASSERT_TRUE(replay.applied_plan);
  EXPECT_FALSE(replay.changed);
  EXPECT_EQ(durable, *replay.merged_tree);
  EXPECT_EQ(plan.splits, replay.applied_plan->splits);

  // The preserved undo operation still restores only the original local page,
  // without deleting or rewriting any of the imported rows.
  ASSERT_EQ(Store::Result::kOk, store.UndoLastMutation());
  tab_tree::TreeNode restored;
  ASSERT_EQ(Store::Result::kOk, store.GetNode(existing_page_id, &restored));
  EXPECT_EQ(original_page, restored);
  ASSERT_EQ(Store::Result::kOk, store.ExportSnapshot(&durable));
  EXPECT_EQ(5u, durable.nodes.size());
  for (const auto& node : plan.tree.nodes) {
    ASSERT_EQ(Store::Result::kOk, store.GetNode(node.id, &restored));
    EXPECT_EQ(node, restored);
  }
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
