// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

namespace {

class RecordingObserver : public SidebarTreeViewModelObserver {
 public:
  void OnBatchUpdateStarted() override { ++batch_start_count; }
  void OnBatchUpdateEnded() override { ++batch_end_count; }
  void OnTreeReset() override { ++reset_count; }
  void OnRowsInserted(size_t, size_t count) override {
    inserted_count += count;
  }
  void OnRowsRemoved(size_t, size_t count) override { removed_count += count; }
  void OnRowsChanged(size_t, size_t count) override { changed_count += count; }
  void OnSelectionChanged(const std::optional<base::Uuid>&,
                          const std::optional<base::Uuid>&) override {
    ++selection_count;
  }

  size_t reset_count = 0;
  size_t batch_start_count = 0;
  size_t batch_end_count = 0;
  size_t inserted_count = 0;
  size_t removed_count = 0;
  size_t changed_count = 0;
  size_t selection_count = 0;
};

class SidebarTreeControllerTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(store_.Initialize(
        temp_dir_.GetPath().AppendASCII("SidebarTree.sqlite")));
  }

 protected:
  tab_tree::Workspace NewWorkspace(std::u16string name, std::string sort_key) {
    tab_tree::Workspace workspace;
    workspace.id = base::Uuid::GenerateRandomV4();
    workspace.name = std::move(name);
    workspace.icon = u"folder";
    workspace.sort_key = std::move(sort_key);
    workspace.created_at = base::Time::UnixEpoch() + base::Seconds(1);
    workspace.modified_at = workspace.created_at;
    return workspace;
  }

  tab_tree::TreeNode NewFolder(const tab_tree::Workspace& workspace,
                               std::optional<base::Uuid> parent_id,
                               std::u16string title,
                               std::string sort_key) {
    tab_tree::TreeNode node;
    node.id = base::Uuid::GenerateRandomV4();
    node.workspace_id = workspace.id;
    node.parent_id = parent_id;
    node.type = tab_tree::TreeNodeType::kFolder;
    node.title = std::move(title);
    node.sort_key = std::move(sort_key);
    node.created_at = base::Time::UnixEpoch() + base::Seconds(1);
    node.modified_at = node.created_at;
    return node;
  }

  tab_tree::TreeNode NewPage(const tab_tree::Workspace& workspace,
                             std::optional<base::Uuid> parent_id,
                             std::u16string title,
                             std::string sort_key) {
    tab_tree::TreeNode node =
        NewFolder(workspace, parent_id, std::move(title), std::move(sort_key));
    node.type = tab_tree::TreeNodeType::kSavedPage;
    node.url = GURL("https://example.test/");
    return node;
  }

  base::ScopedTempDir temp_dir_;
  tab_tree::TabTreeStore store_;
};

TEST_F(SidebarTreeControllerTest,
       StoreDeltasDriveExpandRenameDeleteSelectionAndUndo) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode folder =
      NewFolder(workspace, std::nullopt, u"Project", "a");
  tab_tree::TreeNode page =
      NewPage(workspace, folder.id, u"Issue tracker", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));

  RecordingObserver observer;
  SidebarTreeController controller(&store_);
  controller.view_model().AddObserver(&observer);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(1U, controller.view_model().rows().size());
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(folder.id));
  ASSERT_EQ(2U, controller.view_model().rows().size());
  ASSERT_TRUE(controller.SelectNode(page.id));

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.RenameNode(page.id, u"Renamed",
                                  base::Time::UnixEpoch() + base::Seconds(2)));
  ASSERT_NE(nullptr, controller.view_model().GetNode(page.id));
  EXPECT_EQ(u"Renamed", controller.view_model().GetNode(page.id)->title);
  EXPECT_GT(observer.changed_count, 0U);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.DeleteNode(folder.id,
                                  base::Time::UnixEpoch() + base::Seconds(3)));
  EXPECT_TRUE(controller.view_model().rows().empty());
  EXPECT_FALSE(controller.view_model().selected_node_id().has_value());
  EXPECT_GE(observer.removed_count, 2U);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, controller.UndoLastMutation());
  ASSERT_EQ(1U, controller.view_model().rows().size());
  EXPECT_EQ(folder.id, controller.view_model().rows().front().node_id);
  EXPECT_EQ(1U, observer.reset_count);
  EXPECT_GE(observer.batch_start_count, 2U);
  EXPECT_EQ(observer.batch_start_count, observer.batch_end_count);
  controller.view_model().RemoveObserver(&observer);
}

TEST_F(SidebarTreeControllerTest,
       SearchLoadsUncachedSplitPartnerAndPreservesTreeState) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode exact_parent =
      NewFolder(workspace, std::nullopt, u"Exact parent", "a");
  tab_tree::TreeNode context_parent =
      NewFolder(workspace, std::nullopt, u"Context parent", "b");
  tab_tree::TreeNode exact =
      NewPage(workspace, exact_parent.id, u"Matching page", "a");
  tab_tree::TreeNode split_partner =
      NewPage(workspace, context_parent.id, u"Split partner", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(exact_parent));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(context_parent));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(exact));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(split_partner));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(exact_parent.id));
  ASSERT_TRUE(controller.SelectNode(exact.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.SetSearchContextGroups({{exact.id, split_partner.id}}));
  ASSERT_EQ(nullptr, controller.view_model().GetNode(split_partner.id));

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.SetSearchMatches({exact.id}));
  ASSERT_EQ(4U, controller.view_model().rows().size());
  EXPECT_EQ(exact_parent.id, controller.view_model().rows()[0].node_id);
  EXPECT_EQ(exact.id, controller.view_model().rows()[1].node_id);
  EXPECT_EQ(context_parent.id, controller.view_model().rows()[2].node_id);
  EXPECT_EQ(split_partner.id, controller.view_model().rows()[3].node_id);
  EXPECT_TRUE(controller.view_model().IsSearchExactMatch(exact.id));
  EXPECT_FALSE(controller.view_model().IsSearchContext(exact.id));
  EXPECT_TRUE(controller.view_model().IsSearchContext(exact_parent.id));
  EXPECT_TRUE(controller.view_model().IsSearchContext(context_parent.id));
  EXPECT_TRUE(controller.view_model().IsSearchContext(split_partner.id));

  controller.ClearSearchMatches();
  ASSERT_EQ(3U, controller.view_model().rows().size());
  EXPECT_EQ(exact_parent.id, controller.view_model().rows()[0].node_id);
  EXPECT_EQ(exact.id, controller.view_model().rows()[1].node_id);
  EXPECT_EQ(context_parent.id, controller.view_model().rows()[2].node_id);
  EXPECT_TRUE(controller.view_model().IsExpanded(exact_parent.id));
  EXPECT_FALSE(controller.view_model().IsExpanded(context_parent.id));
  EXPECT_EQ(exact.id, controller.view_model().selected_node_id());
}

TEST_F(SidebarTreeControllerTest,
       SearchLoadsNewSplitContextWhenLiveGroupsChange) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode exact_parent =
      NewFolder(workspace, std::nullopt, u"Exact parent", "a");
  tab_tree::TreeNode context_parent =
      NewFolder(workspace, std::nullopt, u"Context parent", "b");
  tab_tree::TreeNode exact =
      NewPage(workspace, exact_parent.id, u"Matching page", "a");
  tab_tree::TreeNode split_partner =
      NewPage(workspace, context_parent.id, u"Split partner", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(exact_parent));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(context_parent));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(exact));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(split_partner));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.SetSearchMatches({exact.id}));
  ASSERT_EQ(2U, controller.view_model().rows().size());
  ASSERT_EQ(nullptr, controller.view_model().GetNode(split_partner.id));

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.SetSearchContextGroups({{exact.id, split_partner.id}}));
  ASSERT_EQ(4U, controller.view_model().rows().size());
  EXPECT_EQ(split_partner.id, controller.view_model().rows()[3].node_id);
  EXPECT_TRUE(controller.view_model().IsSearchContext(split_partner.id));
  EXPECT_TRUE(controller.view_model().IsSearchContext(context_parent.id));
  EXPECT_TRUE(controller.view_model().IsSearchExactMatch(exact.id));
}

TEST_F(SidebarTreeControllerTest, SearchEvictsDeletedExactMatchImmediately) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode folder =
      NewFolder(workspace, std::nullopt, u"Project", "a");
  tab_tree::TreeNode page =
      NewPage(workspace, folder.id, u"Matching page", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.SetSearchMatches({page.id}));
  ASSERT_EQ(2U, controller.view_model().rows().size());

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.DeleteNode(page.id,
                                  base::Time::UnixEpoch() + base::Seconds(2)));
  EXPECT_EQ(nullptr, controller.view_model().GetNode(page.id));
  EXPECT_TRUE(controller.view_model().rows().empty());
  EXPECT_FALSE(controller.view_model().IsSearchExactMatch(page.id));
}

TEST_F(SidebarTreeControllerTest,
       ValidatesCycleCrossWorkspaceAndBeforeAfterInsertion) {
  tab_tree::Workspace source_workspace = NewWorkspace(u"Source", "a");
  tab_tree::Workspace target_workspace = NewWorkspace(u"Target", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(source_workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(target_workspace));
  tab_tree::TreeNode source =
      NewFolder(source_workspace, std::nullopt, u"Source", "a");
  tab_tree::TreeNode child =
      NewFolder(source_workspace, source.id, u"Child", "a");
  tab_tree::TreeNode left =
      NewFolder(target_workspace, std::nullopt, u"Left", "a");
  tab_tree::TreeNode right =
      NewFolder(target_workspace, std::nullopt, u"Right", "c");
  tab_tree::TreeNode page_target =
      NewPage(target_workspace, std::nullopt, u"Page", "e");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(child));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(left));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(right));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNode(page_target));

  SidebarTreeController controller(&store_);
  SidebarTreeController::DropPlan plan;
  EXPECT_EQ(SidebarTreeController::DropValidationResult::kCycle,
            controller.ValidateDrop(
                source.id,
                {.workspace_id = source_workspace.id,
                 .target_node_id = child.id,
                 .position = SidebarTreeController::DropPosition::kInside},
                SidebarTreeController::DropOperation::kMove, &plan));
  EXPECT_EQ(SidebarTreeController::DropValidationResult::kNoOp,
            controller.ValidateDrop(
                source.id,
                {.workspace_id = source_workspace.id,
                 .target_node_id = source.id,
                 .position = SidebarTreeController::DropPosition::kBefore},
                SidebarTreeController::DropOperation::kMove, &plan));
  EXPECT_EQ(SidebarTreeController::DropValidationResult::kTargetNotFolder,
            controller.ValidateDrop(
                source.id,
                {.workspace_id = target_workspace.id,
                 .target_node_id = page_target.id,
                 .position = SidebarTreeController::DropPosition::kInside},
                SidebarTreeController::DropOperation::kCopy, &plan));

  ASSERT_EQ(SidebarTreeController::DropValidationResult::kAllowed,
            controller.ValidateDrop(
                source.id,
                {.workspace_id = target_workspace.id,
                 .target_node_id = right.id,
                 .position = SidebarTreeController::DropPosition::kBefore},
                SidebarTreeController::DropOperation::kCopy, &plan));
  EXPECT_EQ(1U, plan.insertion_index);
  EXPECT_LT(left.sort_key, plan.sort_key);
  EXPECT_LT(plan.sort_key, right.sort_key);

  SidebarTreeController::DropPlan after_plan;
  ASSERT_EQ(SidebarTreeController::DropValidationResult::kAllowed,
            controller.ValidateDrop(
                source.id,
                {.workspace_id = target_workspace.id,
                 .target_node_id = left.id,
                 .position = SidebarTreeController::DropPosition::kAfter},
                SidebarTreeController::DropOperation::kCopy, &after_plan));
  EXPECT_EQ(plan.insertion_index, after_plan.insertion_index);
  EXPECT_EQ(plan.sort_key, after_plan.sort_key);
}

TEST_F(SidebarTreeControllerTest,
       MovesAndAtomicallyCopiesSubtreeAcrossWorkspacesWithUndo) {
  tab_tree::Workspace source_workspace = NewWorkspace(u"Source", "a");
  tab_tree::Workspace target_workspace = NewWorkspace(u"Target", "b");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(source_workspace));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(target_workspace));
  tab_tree::TreeNode source =
      NewFolder(source_workspace, std::nullopt, u"Project", "a");
  tab_tree::TreeNode child = NewPage(source_workspace, source.id, u"Page", "a");
  tab_tree::TreeNode target =
      NewFolder(target_workspace, std::nullopt, u"Destination", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(child));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(target));

  SidebarTreeController controller(&store_);
  const SidebarTreeController::DropTarget drop_target = {
      .workspace_id = target_workspace.id,
      .target_node_id = target.id,
      .position = SidebarTreeController::DropPosition::kInside};
  SidebarTreeController::DropExecutionResult move = controller.PerformDrop(
      source.id, drop_target, SidebarTreeController::DropOperation::kMove,
      base::Time::UnixEpoch() + base::Seconds(2));
  ASSERT_TRUE(move.ok());
  tab_tree::TreeNode moved_source;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &moved_source));
  EXPECT_EQ(target_workspace.id, moved_source.workspace_id);
  ASSERT_TRUE(moved_source.parent_id.has_value());
  EXPECT_EQ(target.id, *moved_source.parent_id);
  tab_tree::TreeNode moved_child;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(child.id, &moved_child));
  EXPECT_EQ(target_workspace.id, moved_child.workspace_id);

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, controller.UndoLastMutation());
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &moved_source));
  EXPECT_EQ(source_workspace.id, moved_source.workspace_id);

  SidebarTreeController::DropExecutionResult copy = controller.PerformDrop(
      source.id, drop_target, SidebarTreeController::DropOperation::kCopy,
      base::Time::UnixEpoch() + base::Seconds(3));
  ASSERT_TRUE(copy.ok());
  ASSERT_TRUE(copy.copied_root_id.has_value());
  tab_tree::TreeNode copied_root;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(*copy.copied_root_id, &copied_root));
  EXPECT_NE(source.id, copied_root.id);
  EXPECT_EQ(target_workspace.id, copied_root.workspace_id);
  ASSERT_TRUE(copied_root.parent_id.has_value());
  EXPECT_EQ(target.id, *copied_root.parent_id);
  std::vector<tab_tree::TreeNode> copied_children;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(target_workspace.id, copied_root.id,
                               &copied_children));
  ASSERT_EQ(1U, copied_children.size());
  EXPECT_NE(child.id, copied_children.front().id);
  EXPECT_EQ(target_workspace.id, copied_children.front().workspace_id);

  const base::Uuid copied_root_id = *copy.copied_root_id;
  const base::Uuid copied_child_id = copied_children.front().id;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, controller.UndoLastMutation());
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kNotFound,
            store_.GetNode(copied_root_id, &copied_root));
  tab_tree::TreeNode copied_child;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kNotFound,
            store_.GetNode(copied_child_id, &copied_child));
  tab_tree::TreeNode original;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(source.id, &original));
}

TEST_F(SidebarTreeControllerTest,
       GroupedDropMovesEverySplitPaneAndOneUndoRestoresMembership) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode first_group =
      NewFolder(workspace, std::nullopt, u"First group", "a");
  tab_tree::TreeNode second_group =
      NewFolder(workspace, std::nullopt, u"Second group", "b");
  tab_tree::TreeNode destination =
      NewFolder(workspace, std::nullopt, u"Destination", "c");
  tab_tree::TreeNode first =
      NewPage(workspace, first_group.id, u"First pane", "a");
  tab_tree::TreeNode second =
      NewPage(workspace, second_group.id, u"Second pane", "a");
  for (const tab_tree::TreeNode& node :
       {first_group, second_group, destination, first, second}) {
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(node));
  }

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(first_group.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(second_group.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(destination.id));

  SidebarTreeController::DropExecutionResult move =
      controller.PerformGroupedDrop(
          {first.id, second.id},
          {.workspace_id = workspace.id,
           .target_node_id = destination.id,
           .position = SidebarTreeController::DropPosition::kInside},
          SidebarTreeController::DropOperation::kMove,
          base::Time::UnixEpoch() + base::Seconds(2));
  ASSERT_TRUE(move.ok());

  std::vector<const tab_tree::TreeNode*> destination_children;
  ASSERT_TRUE(controller.view_model().GetLoadedChildren(destination.id,
                                                        &destination_children));
  ASSERT_EQ(2u, destination_children.size());
  EXPECT_EQ(first.id, destination_children[0]->id);
  EXPECT_EQ(second.id, destination_children[1]->id);
  for (const base::Uuid& node_id : {first.id, second.id}) {
    tab_tree::TreeNode moved;
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
              store_.GetNode(node_id, &moved));
    EXPECT_EQ(destination.id, moved.parent_id);
  }

  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, controller.UndoLastMutation());
  tab_tree::TreeNode restored_first;
  tab_tree::TreeNode restored_second;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(first.id, &restored_first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(second.id, &restored_second));
  EXPECT_EQ(first_group.id, restored_first.parent_id);
  EXPECT_EQ(second_group.id, restored_second.parent_id);
  destination_children.clear();
  ASSERT_TRUE(controller.view_model().GetLoadedChildren(destination.id,
                                                        &destination_children));
  EXPECT_TRUE(destination_children.empty());
}

TEST_F(SidebarTreeControllerTest,
       AtomicBatchAcceptsChildBeforeParentAndUndoDeletesTree) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode root = NewFolder(workspace, std::nullopt, u"Root", "a");
  tab_tree::TreeNode child = NewPage(workspace, root.id, u"Child", "a");
  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  EXPECT_TRUE(controller.view_model().rows().empty());
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateNodesAtomically({child, root}));
  ASSERT_EQ(1U, controller.view_model().rows().size());
  EXPECT_EQ(root.id, controller.view_model().rows().front().node_id);
  tab_tree::TreeNode persisted;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(child.id, &persisted));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.UndoLastMutation());
  EXPECT_TRUE(controller.view_model().rows().empty());
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kNotFound,
            store_.GetNode(root.id, &persisted));
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kNotFound,
            store_.GetNode(child.id, &persisted));

  tab_tree::TreeNode cycle_a =
      NewFolder(workspace, std::nullopt, u"Cycle A", "b");
  tab_tree::TreeNode cycle_b =
      NewFolder(workspace, cycle_a.id, u"Cycle B", "a");
  cycle_a.parent_id = cycle_b.id;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kCycle,
            store_.CreateNodesAtomically({cycle_a, cycle_b}));
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kNothingToUndo,
            store_.UndoLastMutation());
}

TEST_F(SidebarTreeControllerTest, CreatesNestedFolderAtEndOfParent) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode parent =
      NewFolder(workspace, std::nullopt, u"Parent", "a");
  tab_tree::TreeNode existing =
      NewFolder(workspace, parent.id, u"Existing", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(parent));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(existing));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ExpandNode(parent.id));
  base::Uuid nested_id;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.CreateFolder(parent.id, u"Nested",
                                    base::Time::UnixEpoch() + base::Seconds(2),
                                    &nested_id));

  tab_tree::TreeNode nested;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(nested_id, &nested));
  EXPECT_EQ(parent.id, nested.parent_id);
  EXPECT_EQ(u"Nested", nested.title);
  EXPECT_GT(nested.sort_key, existing.sort_key);
  ASSERT_EQ(3U, controller.view_model().rows().size());
  EXPECT_EQ(nested_id, controller.view_model().rows().back().node_id);
  EXPECT_EQ(1U, controller.view_model().rows().back().depth);
}

TEST_F(SidebarTreeControllerTest, CreatesRootFolderInActiveWorkspace) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode existing =
      NewFolder(workspace, std::nullopt, u"Existing", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(existing));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  base::Uuid root_id;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.CreateFolder(std::nullopt, u"New root group",
                                    base::Time::UnixEpoch() + base::Seconds(2),
                                    &root_id));

  tab_tree::TreeNode root;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(root_id, &root));
  EXPECT_EQ(workspace.id, root.workspace_id);
  EXPECT_FALSE(root.parent_id.has_value());
  EXPECT_EQ(u"New root group", root.title);
  EXPECT_GT(root.sort_key, existing.sort_key);
  ASSERT_EQ(2U, controller.view_model().rows().size());
  EXPECT_EQ(root_id, controller.view_model().rows().back().node_id);
  EXPECT_EQ(0U, controller.view_model().rows().back().depth);
}

TEST_F(SidebarTreeControllerTest, CreatesTemporaryTabAtExactDropPosition) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode first = NewPage(workspace, std::nullopt, u"First", "a");
  tab_tree::TreeNode second = NewPage(workspace, std::nullopt, u"Second", "c");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(second));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  const SidebarTreeController::DropTarget target{
      .workspace_id = workspace.id,
      .target_node_id = second.id,
      .position = SidebarTreeController::DropPosition::kBefore};
  EXPECT_EQ(SidebarTreeController::DropValidationResult::kAllowed,
            controller.ValidateNewSavedPageDrop(target));
  tab_tree::TreeNode created;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.CreateSavedPageAtDrop(
                target, u"Temporary", GURL("https://temporary.example.test/"),
                base::Time::UnixEpoch() + base::Seconds(2), &created));
  EXPECT_EQ(workspace.id, created.workspace_id);
  EXPECT_FALSE(created.parent_id.has_value());
  EXPECT_GT(created.sort_key, first.sort_key);
  EXPECT_LT(created.sort_key, second.sort_key);
  ASSERT_EQ(3U, controller.view_model().rows().size());
  EXPECT_EQ(created.id, controller.view_model().rows()[1].node_id);
}

TEST_F(SidebarTreeControllerTest, CreatesFirstSavedPageAtEmptyWorkspaceRoot) {
  tab_tree::Workspace workspace = NewWorkspace(u"Empty", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  ASSERT_TRUE(controller.view_model().rows().empty());

  const SidebarTreeController::DropTarget root_target{
      .workspace_id = workspace.id,
      .target_node_id = std::nullopt,
      .position = SidebarTreeController::DropPosition::kInside};
  EXPECT_EQ(SidebarTreeController::DropValidationResult::kAllowed,
            controller.ValidateNewSavedPageDrop(root_target));

  tab_tree::TreeNode created;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.CreateSavedPageAtDrop(
                root_target, u"First tab", GURL("https://first.example/"),
                base::Time::UnixEpoch() + base::Seconds(2), &created));
  EXPECT_EQ(workspace.id, created.workspace_id);
  EXPECT_FALSE(created.parent_id.has_value());
  ASSERT_EQ(1U, controller.view_model().rows().size());
  EXPECT_EQ(created.id, controller.view_model().rows().front().node_id);
}

TEST_F(SidebarTreeControllerTest, OrdinaryMoveNeverCopiesTheSourceNode) {
  tab_tree::Workspace workspace = NewWorkspace(u"Move", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode first = NewPage(workspace, std::nullopt, u"First", "a");
  tab_tree::TreeNode second = NewPage(workspace, std::nullopt, u"Second", "c");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(second));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  const auto result = controller.PerformDrop(
      first.id,
      {.workspace_id = workspace.id,
       .target_node_id = second.id,
       .position = SidebarTreeController::DropPosition::kAfter},
      SidebarTreeController::DropOperation::kMove,
      base::Time::UnixEpoch() + base::Seconds(2));
  ASSERT_TRUE(result.ok());

  std::vector<tab_tree::TreeNode> children;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(workspace.id, std::nullopt, &children));
  ASSERT_EQ(2U, children.size());
  EXPECT_EQ(second.id, children[0].id);
  EXPECT_EQ(first.id, children[1].id);
}

TEST_F(SidebarTreeControllerTest,
       ExplicitDuplicateCopiesOneSavedRowWithinItsSection) {
  tab_tree::Workspace workspace = NewWorkspace(u"Duplicate", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode source = NewPage(workspace, std::nullopt, u"Source", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(source));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  const auto result = controller.PerformDrop(
      source.id,
      {.workspace_id = workspace.id,
       .target_node_id = source.id,
       .position = SidebarTreeController::DropPosition::kAfter},
      SidebarTreeController::DropOperation::kCopy,
      base::Time::UnixEpoch() + base::Seconds(2));
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.copied_root_id.has_value());
  EXPECT_NE(source.id, *result.copied_root_id);

  std::vector<tab_tree::TreeNode> children;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetChildren(workspace.id, std::nullopt, &children));
  ASSERT_EQ(2U, children.size());
  EXPECT_EQ(source.id, children[0].id);
  EXPECT_EQ(*result.copied_root_id, children[1].id);
}

TEST_F(SidebarTreeControllerTest,
       PersistsCustomEmojiAndAccentWithoutChangingTreeSchema) {
  tab_tree::Workspace workspace = NewWorkspace(u"Development", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  tab_tree::TreeNode folder =
      NewFolder(workspace, std::nullopt, u"Project", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(folder));

  SidebarTreeController controller(&store_);
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.ActivateWorkspace(workspace.id));
  constexpr uint32_t kAccent = 0xFF4F8DE8u;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller.UpdateFolderPresentation(
                folder.id, u"Emoji project", u"🛠️", kAccent,
                base::Time::UnixEpoch() + base::Seconds(2)));

  tab_tree::TreeNode persisted;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.GetNode(folder.id, &persisted));
  EXPECT_EQ(u"Emoji project", persisted.title);
  EXPECT_EQ(u"🛠️", persisted.icon);
  EXPECT_EQ(kAccent, persisted.accent_argb);
  ASSERT_NE(nullptr, controller.view_model().GetNode(folder.id));
  EXPECT_EQ(u"🛠️", controller.view_model().GetNode(folder.id)->icon);
}

}  // namespace

}  // namespace ahoi::sidebar
