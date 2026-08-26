// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view_model.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/timer/elapsed_timer.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

namespace {

tab_tree::TreeNode MakeFolder(const base::Uuid& workspace_id,
                              std::optional<base::Uuid> parent_id,
                              std::u16string title,
                              std::string sort_key) {
  tab_tree::TreeNode node;
  node.id = base::Uuid::GenerateRandomV4();
  node.workspace_id = workspace_id;
  node.parent_id = parent_id;
  node.type = tab_tree::TreeNodeType::kFolder;
  node.title = std::move(title);
  node.sort_key = std::move(sort_key);
  node.created_at = base::Time::UnixEpoch() + base::Seconds(1);
  node.modified_at = node.created_at;
  return node;
}

tab_tree::TreeNode MakePage(const base::Uuid& workspace_id,
                            std::optional<base::Uuid> parent_id,
                            std::u16string title,
                            std::string sort_key) {
  tab_tree::TreeNode node = MakeFolder(workspace_id, parent_id,
                                       std::move(title), std::move(sort_key));
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.url = GURL("https://example.test/");
  return node;
}

class RecordingObserver : public SidebarTreeViewModelObserver {
 public:
  struct Delta {
    enum class Kind { kReset, kInserted, kRemoved, kChanged, kSelection };

    Kind kind;
    size_t first_row = 0;
    size_t count = 0;
  };

  void OnTreeReset() override {
    deltas.push_back({.kind = Delta::Kind::kReset});
  }
  void OnRowsInserted(size_t first_row, size_t count) override {
    deltas.push_back({.kind = Delta::Kind::kInserted,
                      .first_row = first_row,
                      .count = count});
  }
  void OnRowsRemoved(size_t first_row, size_t count) override {
    deltas.push_back({.kind = Delta::Kind::kRemoved,
                      .first_row = first_row,
                      .count = count});
  }
  void OnRowsChanged(size_t first_row, size_t count) override {
    deltas.push_back({.kind = Delta::Kind::kChanged,
                      .first_row = first_row,
                      .count = count});
  }
  void OnSelectionChanged(
      const std::optional<base::Uuid>& old_selection,
      const std::optional<base::Uuid>& new_selection) override {
    deltas.push_back({.kind = Delta::Kind::kSelection});
    selections.emplace_back(old_selection, new_selection);
  }

  size_t Count(Delta::Kind kind) const {
    return static_cast<size_t>(std::ranges::count(deltas, kind, &Delta::kind));
  }

  std::vector<Delta> deltas;
  std::vector<std::pair<std::optional<base::Uuid>, std::optional<base::Uuid>>>
      selections;
};

TEST(SidebarTreeViewModelTest,
     FlattensOnlyExpandedBranchesAndKeepsUuidSelectionStable) {
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();
  tab_tree::TreeNode folder =
      MakeFolder(workspace_id, std::nullopt, u"Folder", "a");
  tab_tree::TreeNode root_page =
      MakePage(workspace_id, std::nullopt, u"Root page", "b");
  tab_tree::TreeNode child_folder =
      MakeFolder(workspace_id, folder.id, u"Child folder", "a");
  tab_tree::TreeNode child_page =
      MakePage(workspace_id, folder.id, u"Child page", "b");
  tab_tree::TreeNode grandchild =
      MakePage(workspace_id, child_folder.id, u"Grandchild", "a");

  SidebarTreeViewModel model;
  ASSERT_TRUE(model.ResetWorkspace(workspace_id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {folder, root_page}));
  ASSERT_TRUE(model.ReplaceChildren(folder.id, {child_folder, child_page}));
  ASSERT_TRUE(model.ReplaceChildren(child_folder.id, {grandchild}));
  EXPECT_EQ(2U, model.rows().size());

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  EXPECT_EQ(4U, model.rows().size());
  EXPECT_FALSE(model.GetRowForNode(grandchild.id).has_value());
  ASSERT_TRUE(model.SetExpanded(child_folder.id, true));
  ASSERT_EQ(5U, model.rows().size());
  EXPECT_EQ(2U, model.rows()[2].depth);

  ASSERT_TRUE(model.SetSelectedNode(grandchild.id));
  ASSERT_TRUE(model.SetExpanded(folder.id, false));
  EXPECT_EQ(2U, model.rows().size());
  ASSERT_TRUE(model.selected_node_id().has_value());
  EXPECT_EQ(folder.id, *model.selected_node_id());

  ASSERT_TRUE(model.SetExpanded(folder.id, true));
  EXPECT_EQ(5U, model.rows().size());
  EXPECT_TRUE(model.IsExpanded(child_folder.id));
}

TEST(SidebarTreeViewModelTest, EmitsRowDeltasWithoutWorkspaceReset) {
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();
  tab_tree::TreeNode first =
      MakePage(workspace_id, std::nullopt, u"First", "a");
  tab_tree::TreeNode second =
      MakePage(workspace_id, std::nullopt, u"Second", "b");
  tab_tree::TreeNode third =
      MakePage(workspace_id, std::nullopt, u"Third", "c");

  RecordingObserver observer;
  SidebarTreeViewModel model;
  model.AddObserver(&observer);
  ASSERT_TRUE(model.ResetWorkspace(workspace_id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {first, second, third}));
  ASSERT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kReset));
  ASSERT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kInserted));

  second.title = u"Renamed";
  ASSERT_TRUE(model.CacheNode(second));
  EXPECT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kChanged));

  third.sort_key = "aa";
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {first, second, third}));
  EXPECT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kReset));
  EXPECT_GE(observer.Count(RecordingObserver::Delta::Kind::kRemoved), 1U);
  EXPECT_GE(observer.Count(RecordingObserver::Delta::Kind::kInserted), 2U);
  model.RemoveObserver(&observer);
}

TEST(SidebarTreeViewModelTest,
     TenThousandVisibleNodesUseOneInsertionDeltaWithinBudget) {
  constexpr size_t kNodeCount = 10000;
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();
  std::vector<tab_tree::TreeNode> nodes;
  nodes.reserve(kNodeCount);
  for (size_t index = 0; index < kNodeCount; ++index) {
    nodes.push_back(
        MakePage(workspace_id, std::nullopt, u"Page", std::to_string(index)));
  }

  RecordingObserver observer;
  SidebarTreeViewModel model;
  model.AddObserver(&observer);
  ASSERT_TRUE(model.ResetWorkspace(workspace_id));
  base::ElapsedTimer timer;
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, std::move(nodes)));
  const base::TimeDelta elapsed = timer.Elapsed();

  EXPECT_EQ(kNodeCount, model.rows().size());
  EXPECT_EQ(kNodeCount, model.cached_node_count_for_testing());
  ASSERT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kInserted));
  const auto inserted = std::ranges::find(
      observer.deltas, RecordingObserver::Delta::Kind::kInserted,
      &RecordingObserver::Delta::kind);
  ASSERT_NE(observer.deltas.end(), inserted);
  EXPECT_EQ(kNodeCount, inserted->count);
  // This is intentionally generous for debug/ASan bots. Its primary contract
  // is one O(n) projection and one delta, not a machine-specific benchmark.
  EXPECT_LT(elapsed, base::Seconds(10));
  model.RemoveObserver(&observer);
}

TEST(SidebarTreeViewModelTest,
     TenThousandCollapsedDescendantsDoNotEnterVisibleProjection) {
  constexpr size_t kNodeCount = 10000;
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();
  std::vector<tab_tree::TreeNode> chain;
  chain.reserve(kNodeCount);
  std::optional<base::Uuid> parent_id;
  for (size_t index = 0; index < kNodeCount; ++index) {
    chain.push_back(
        MakeFolder(workspace_id, parent_id, u"Folder", std::to_string(index)));
    parent_id = chain.back().id;
  }

  RecordingObserver observer;
  SidebarTreeViewModel model;
  model.AddObserver(&observer);
  ASSERT_TRUE(model.ResetWorkspace(workspace_id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {chain.front()}));
  base::ElapsedTimer timer;
  for (size_t index = 1; index < chain.size(); ++index) {
    ASSERT_TRUE(model.ReplaceChildren(chain[index - 1].id, {chain[index]}));
  }
  const base::TimeDelta elapsed = timer.Elapsed();

  EXPECT_EQ(1U, model.rows().size());
  EXPECT_EQ(kNodeCount, model.cached_node_count_for_testing());
  EXPECT_EQ(1U, observer.Count(RecordingObserver::Delta::Kind::kInserted));
  EXPECT_LT(elapsed, base::Seconds(10));
  ASSERT_TRUE(model.SetExpanded(chain.front().id, true));
  EXPECT_EQ(2U, model.rows().size());
  model.RemoveObserver(&observer);
}

TEST(SidebarTreeViewModelTest, RejectsInvalidReplacementAtomically) {
  const base::Uuid workspace_id = base::Uuid::GenerateRandomV4();
  tab_tree::TreeNode valid =
      MakePage(workspace_id, std::nullopt, u"Valid", "a");
  SidebarTreeViewModel model;
  ASSERT_TRUE(model.ResetWorkspace(workspace_id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {valid}));

  tab_tree::TreeNode invalid = valid;
  invalid.workspace_id = base::Uuid::GenerateRandomV4();
  EXPECT_FALSE(model.ReplaceChildren(std::nullopt, {invalid}));
  ASSERT_EQ(1U, model.rows().size());
  EXPECT_EQ(valid.id, model.rows().front().node_id);
}

}  // namespace

}  // namespace ahoi::sidebar
