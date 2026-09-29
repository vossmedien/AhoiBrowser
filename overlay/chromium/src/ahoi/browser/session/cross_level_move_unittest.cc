// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/cross_level_move.h"

#include <set>
#include <string>
#include <vector>

#include "ahoi/browser/session/portable_workspace_import_plan.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

using tab_tree::TreeNode;
using tab_tree::TreeNodeType;

base::Uuid Id(int n) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("50000000-0000-4000-8000-%012d", n));
}

const base::Time kNow = base::Time::UnixEpoch() + base::Days(20'000);

TreeNode Folder(int id, int workspace, std::optional<int> parent,
                const char* key) {
  TreeNode node;
  node.id = Id(id);
  node.workspace_id = Id(workspace);
  if (parent) {
    node.parent_id = Id(*parent);
  }
  node.type = TreeNodeType::kFolder;
  node.title = u"Folder";
  node.sort_key = key;
  node.created_at = kNow;
  node.modified_at = kNow;
  return node;
}

TreeNode Page(int id, int workspace, std::optional<int> parent,
              const char* key, const char* url) {
  TreeNode node = Folder(id, workspace, parent, key);
  node.type = TreeNodeType::kSavedPage;
  node.title = u"Page";
  node.url = GURL(url);
  node.target_kind = sync::SharedTabTargetKind::kWeb;
  return node;
}

// Source Workspace 1: folder 10 {page 11, page 12, folder 13 {page 14}},
// split pages 20 and 21 at the root, a browser page 30 and an open
// temporary page 40. Target Workspace 2 in another Profile: page 90.
struct Fixture {
  tab_tree::TabTreeSnapshot source;
  WorkspaceStructureState structure;
  tab_tree::TabTreeSnapshot target;

  Fixture() {
    source.workspaces.push_back({.id = Id(1),
                                 .name = u"Privat",
                                 .sort_key = "0",
                                 .created_at = kNow,
                                 .modified_at = kNow});
    source.nodes = {Folder(10, 1, std::nullopt, "a"),
                    Page(11, 1, 10, "a", "https://a.example/"),
                    Page(12, 1, 10, "b", "https://b.example/"),
                    Folder(13, 1, 10, "c"),
                    Page(14, 1, 13, "a", "https://c.example/"),
                    Page(20, 1, std::nullopt, "b", "https://left.example/"),
                    Page(21, 1, std::nullopt, "c", "https://right.example/"),
                    Page(30, 1, std::nullopt, "d", "chrome://settings/")};
    source.nodes.back().target_kind = sync::SharedTabTargetKind::kLocalOnly;
    source.nodes.back().local_scheme = "chrome";
    TreeNode temporary =
        Page(40, 1, std::nullopt, "e", "https://open.example/");
    temporary.is_temporary = true;
    source.nodes.push_back(temporary);

    WorkspaceStructureEntry split;
    split.record = sync::SplitGroupRecord{
        .id = Id(70),
        .workspace_id = Id(1),
        .topology = {.member_ids = {Id(20), Id(21)}}};
    structure.entries.emplace(Id(70), std::move(split));

    target.workspaces.push_back({.id = Id(2),
                                 .name = u"Arbeit",
                                 .sort_key = "1",
                                 .created_at = kNow,
                                 .modified_at = kNow});
    target.nodes = {Page(90, 2, std::nullopt, "m", "https://work.example/")};
  }
};

base::RepeatingCallback<base::Uuid()> Counter(int* next) {
  return base::BindRepeating([](int* next) { return Id((*next)++); }, next);
}

TEST(CrossLevelMoveTest, FolderMovesWithItsSubtreeParentFirst) {
  Fixture fixture;
  CrossLevelMovePayload payload;
  ASSERT_EQ(CrossLevelMoveCheck::kOk,
            ExtractCrossLevelMove(fixture.source, fixture.structure, {Id(10)},
                                  &payload));
  EXPECT_EQ(3u, payload.pages);
  EXPECT_EQ(2u, payload.folders);
  ASSERT_EQ(5u, payload.nodes.size());
  std::set<base::Uuid> placed;
  for (const auto& node : payload.nodes) {
    EXPECT_TRUE(!node.parent_id || placed.contains(*node.parent_id));
    placed.insert(node.id);
  }
  EXPECT_TRUE(payload.splits.empty());
}

TEST(CrossLevelMoveTest, SplitMovesWholeOrNotAtAll) {
  Fixture fixture;
  CrossLevelMovePayload payload;
  EXPECT_EQ(CrossLevelMoveCheck::kSplitWouldMix,
            ExtractCrossLevelMove(fixture.source, fixture.structure, {Id(20)},
                                  &payload));
  ASSERT_EQ(CrossLevelMoveCheck::kOk,
            ExtractCrossLevelMove(fixture.source, fixture.structure,
                                  {Id(20), Id(21)}, &payload));
  ASSERT_EQ(1u, payload.splits.size());
  EXPECT_EQ(Id(70), payload.splits.front().id);
}

TEST(CrossLevelMoveTest, RefusesLocalPagesAndOverlappingRoots) {
  Fixture fixture;
  CrossLevelMovePayload payload;
  EXPECT_EQ(CrossLevelMoveCheck::kLocalOnlyPage,
            ExtractCrossLevelMove(fixture.source, fixture.structure, {Id(30)},
                                  &payload));
  EXPECT_EQ(CrossLevelMoveCheck::kNothingToMove,
            ExtractCrossLevelMove(fixture.source, fixture.structure,
                                  {Id(10), Id(11)}, &payload));
  EXPECT_EQ(CrossLevelMoveCheck::kNothingToMove,
            ExtractCrossLevelMove(fixture.source, fixture.structure, {Id(99)},
                                  &payload));
}

TEST(CrossLevelMoveTest, TemporaryPageArrivesSaved) {
  Fixture fixture;
  CrossLevelMovePayload payload;
  ASSERT_EQ(CrossLevelMoveCheck::kOk,
            ExtractCrossLevelMove(fixture.source, fixture.structure, {Id(40)},
                                  &payload));
  ASSERT_EQ(1u, payload.nodes.size());
  EXPECT_FALSE(payload.nodes.front().is_temporary);
}

TEST(CrossLevelMoveTest, PlacementIsFreshAdditiveAndAfterLastRoot) {
  Fixture fixture;
  CrossLevelMovePayload payload;
  ASSERT_EQ(CrossLevelMoveCheck::kOk,
            ExtractCrossLevelMove(fixture.source, fixture.structure,
                                  {Id(10), Id(20), Id(21)}, &payload));
  int next = 500;
  const auto placement =
      PlaceCrossLevelMove(payload, fixture.target, Id(2), Counter(&next));
  ASSERT_TRUE(placement);
  ASSERT_EQ(3u, placement->root_ids.size());
  std::set<base::Uuid> source_ids;
  for (const auto& node : fixture.source.nodes) {
    source_ids.insert(node.id);
  }
  for (const auto& node : placement->import.tree.nodes) {
    EXPECT_EQ(Id(2), node.workspace_id);
    EXPECT_FALSE(source_ids.contains(node.id));
    if (!node.parent_id) {
      EXPECT_GT(node.sort_key, std::string("m"));
    }
  }
  ASSERT_EQ(1u, placement->import.splits.size());
  const auto& split = placement->import.splits.front();
  EXPECT_NE(Id(70), split.id);
  EXPECT_EQ(Id(2), split.workspace_id);
  EXPECT_EQ(placement->new_ids.at(Id(20)), split.topology.member_ids[0]);

  // The target's own record makes the import purely additive.
  const auto plan = PreparePortableWorkspaceImport(
      placement->import, fixture.target, WorkspaceStructureState(), Id(3),
      kNow);
  ASSERT_TRUE(plan);
  EXPECT_TRUE(plan->changed);
  EXPECT_EQ(fixture.target.nodes.size() + 7u, plan->tree.nodes.size());
  EXPECT_EQ(1u, plan->structure.entries.size());

  EXPECT_FALSE(
      PlaceCrossLevelMove(payload, fixture.target, Id(9), Counter(&next)));
}

TEST(CrossLevelMoveTest, UndoOnlyWhileTheMoveIsTheLatestChange) {
  CrossLevelMoveReceipt receipt;
  receipt.source_deletion_subject = Id(10);
  receipt.moved_at = kNow;
  const LatestTreeUndo deletion{.kind = tab_tree::UndoMutationKind::kDelete,
                                .subject_node_id = Id(10),
                                .created_at = kNow};
  const LatestTreeUndo later{.kind = tab_tree::UndoMutationKind::kRename,
                             .subject_node_id = Id(11),
                             .created_at = kNow + base::Seconds(5)};
  const LatestTreeUndo earlier{.kind = tab_tree::UndoMutationKind::kMove,
                               .subject_node_id = Id(12),
                               .created_at = kNow - base::Seconds(5)};
  EXPECT_TRUE(IsCrossLevelMoveLatestOnSource(receipt, deletion));
  EXPECT_FALSE(IsCrossLevelMoveLatestOnSource(receipt, later));
  EXPECT_FALSE(IsCrossLevelMoveLatestOnSource(receipt, std::nullopt));
  EXPECT_TRUE(IsCrossLevelMoveLatestOnTarget(receipt, std::nullopt));
  EXPECT_TRUE(IsCrossLevelMoveLatestOnTarget(receipt, earlier));
  EXPECT_FALSE(IsCrossLevelMoveLatestOnTarget(receipt, later));

  receipt.source_deletion_subject.reset();
  EXPECT_TRUE(IsCrossLevelMoveLatestOnSource(receipt, earlier));
  EXPECT_FALSE(IsCrossLevelMoveLatestOnSource(receipt, later));

  tab_tree::TabTreeSnapshot tree;
  tree.undo_operations = {
      {.operation_id = 4, .kind = tab_tree::UndoMutationKind::kDelete,
       .subject_node_id = Id(10)},
      {.operation_id = 2, .kind = tab_tree::UndoMutationKind::kMove,
       .subject_node_id = Id(12)}};
  const auto latest = FindLatestTreeUndo(tree);
  ASSERT_TRUE(latest);
  EXPECT_EQ(Id(10), latest->subject_node_id);

  RememberCrossLevelMove(receipt);
  ASSERT_TRUE(GetLatestCrossLevelMove());
  ForgetCrossLevelMove();
  EXPECT_FALSE(GetLatestCrossLevelMove());
}

}  // namespace
}  // namespace ahoi::session
