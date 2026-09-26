// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <optional>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_observer.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_unittest_support.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

// ADR 0012 / WS-MERGE-01..03, 05: the store primitive behind "Zusammenführen
// mit …".
namespace ahoi::tab_tree {

namespace {

using test_support::AhoiTabTreeStoreTest;
using Result = TabTreeStore::Result;

class RecordingObserver : public TabTreeObserver {
 public:
  void OnTabTreeChanged(const TabTreeChange& change) override {
    changes.push_back(change);
  }

  std::vector<TabTreeChange> changes;
};

class AhoiTabTreeMergeWorkspaceTest : public AhoiTabTreeStoreTest {
 protected:
  void SetUp() override {
    AhoiTabTreeStoreTest::SetUp();
    source_ = NewWorkspace(u"Research", "workspace-a");
    source_.icon = u"book";
    source_.accent_argb = 0xff3366ccu;
    target_ = NewWorkspace(u"Work", "workspace-b");
    ASSERT_EQ(Result::kOk, store_->CreateWorkspace(source_));
    ASSERT_EQ(Result::kOk, store_->CreateWorkspace(target_));
  }

  TreeNode NewOpenTab(std::u16string title, std::string sort_key) {
    TreeNode node = NewFolder(source_, std::nullopt, std::move(title),
                              std::move(sort_key));
    node.type = TreeNodeType::kSavedPage;
    node.url = GURL("https://example.test/open");
    node.is_temporary = true;
    return node;
  }

  TreeNode Get(const base::Uuid& id) {
    TreeNode node;
    EXPECT_EQ(Result::kOk, store_->GetNode(id, &node));
    return node;
  }

  // Also returns tombstoned Workspaces, unlike GetWorkspace().
  Workspace StoredWorkspace(const base::Uuid& id) {
    TabTreeSnapshot snapshot;
    EXPECT_EQ(Result::kOk, store_->ExportSnapshot(&snapshot));
    for (const Workspace& workspace : snapshot.workspaces) {
      if (workspace.id == id) {
        return workspace;
      }
    }
    ADD_FAILURE() << "workspace missing from snapshot";
    return Workspace();
  }

  bool IsVisible(const Workspace& workspace) {
    std::vector<Workspace> workspaces;
    EXPECT_EQ(Result::kOk, store_->GetWorkspaces(&workspaces));
    for (const Workspace& visible : workspaces) {
      if (visible.id == workspace.id) {
        return true;
      }
    }
    return false;
  }

  Workspace source_;
  Workspace target_;
};

TEST_F(AhoiTabTreeMergeWorkspaceTest, MergesIntoOneFolderAndUndoRevivesSource) {
  TreeNode folder = NewFolder(source_, std::nullopt, u"Papers", "b");
  TreeNode nested = NewSavedPage(source_, folder.id, u"Paper",
                                 GURL("https://example.test/paper"), "a");
  TreeNode loose = NewSavedPage(source_, std::nullopt, u"Notes",
                                GURL("https://example.test/notes"), "c");
  TreeNode kept = NewSavedPage(target_, std::nullopt, u"Mail",
                               GURL("https://example.test/mail"), "m");
  for (const TreeNode& node : {folder, nested, loose, kept}) {
    ASSERT_EQ(Result::kOk, store_->CreateNode(node));
  }

  RecordingObserver observer;
  store_->AddObserver(&observer);
  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = true,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  store_->RemoveObserver(&observer);
  ASSERT_TRUE(merge_folder.has_value());
  ASSERT_EQ(observer.changes.size(), 1u);
  EXPECT_EQ(observer.changes.front().kind, MutationKind::kMoved);
  EXPECT_EQ(observer.changes.front().node_ids.size(), 4u);

  const TreeNode created = Get(*merge_folder);
  EXPECT_EQ(created.workspace_id, target_.id);
  EXPECT_FALSE(created.parent_id.has_value());
  EXPECT_EQ(created.type, TreeNodeType::kFolder);
  EXPECT_EQ(created.title, u"Research");
  EXPECT_EQ(created.icon, u"book");
  EXPECT_EQ(created.accent_argb, 0xff3366ccu);
  EXPECT_EQ(created.sort_key, "n");

  std::vector<TreeNode> children;
  ASSERT_EQ(Result::kOk,
            store_->GetChildren(target_.id, *merge_folder, &children));
  ASSERT_EQ(children.size(), 2u);
  EXPECT_EQ(children[0].id, folder.id);
  EXPECT_EQ(children[1].id, loose.id);
  EXPECT_EQ(Get(nested.id).workspace_id, target_.id);
  EXPECT_EQ(Get(nested.id).parent_id, folder.id);
  EXPECT_EQ(Get(kept.id).sort_key, "m");
  EXPECT_FALSE(IsVisible(source_));
  // Crest 084: the source's tombstone names the target for sync peers.
  EXPECT_EQ(StoredWorkspace(source_.id).merged_into, target_.id);

  // WS-MERGE-03: one undo restores the source with its IDs and order.
  ASSERT_EQ(Result::kOk, store_->UndoLastMutation());
  EXPECT_TRUE(IsVisible(source_));
  TreeNode gone;
  EXPECT_EQ(Result::kNotFound, store_->GetNode(*merge_folder, &gone));
  for (const TreeNode& node : {folder, nested, loose}) {
    const TreeNode restored = Get(node.id);
    EXPECT_EQ(restored.workspace_id, source_.id);
    EXPECT_EQ(restored.parent_id, node.parent_id);
    EXPECT_EQ(restored.sort_key, node.sort_key);
    EXPECT_FALSE(restored.tombstone);
  }
  Workspace revived;
  ASSERT_EQ(Result::kOk, store_->GetWorkspace(source_.id, &revived));
  EXPECT_FALSE(revived.tombstone);
  EXPECT_GT(revived.modified_at, source_.modified_at);
  EXPECT_FALSE(revived.merged_into);
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, MergesFlatAfterTargetInSourceOrder) {
  TreeNode first = NewSavedPage(source_, std::nullopt, u"First",
                                GURL("https://example.test/1"), "a");
  TreeNode second = NewSavedPage(source_, std::nullopt, u"Second",
                                 GURL("https://example.test/2"), "b");
  TreeNode kept = NewSavedPage(target_, std::nullopt, u"Mail",
                               GURL("https://example.test/mail"), "m");
  for (const TreeNode& node : {second, first, kept}) {
    ASSERT_EQ(Result::kOk, store_->CreateNode(node));
  }

  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = false,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  EXPECT_FALSE(merge_folder.has_value());
  std::vector<TreeNode> roots;
  ASSERT_EQ(Result::kOk,
            store_->GetChildren(target_.id, std::nullopt, &roots));
  ASSERT_EQ(roots.size(), 3u);
  EXPECT_EQ(roots[0].id, kept.id);
  EXPECT_EQ(roots[1].id, first.id);
  EXPECT_EQ(roots[1].sort_key, "na");
  EXPECT_EQ(roots[2].id, second.id);
  EXPECT_FALSE(IsVisible(source_));
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, OpenTabsStayOutOfTheFolder) {
  TreeNode saved = NewSavedPage(source_, std::nullopt, u"Saved",
                                GURL("https://example.test/saved"), "a");
  TreeNode open = NewOpenTab(u"Open", "b");
  TreeNode closing = NewOpenTab(u"Closing", "c");
  ASSERT_EQ(Result::kOk, store_->CreateNode(saved));
  ASSERT_EQ(Result::kOk, store_->CreateTemporaryPage(open));
  ASSERT_EQ(Result::kOk, store_->CreateTemporaryPage(closing));

  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = true,
                                    .sort_key = "n",
                                    .closing_temporary_ids = {closing.id},
                                    .modified_at = base::Time::Now()},
                                   &merge_folder));
  ASSERT_TRUE(merge_folder.has_value());
  EXPECT_EQ(Get(saved.id).parent_id, merge_folder);
  const TreeNode moved_open = Get(open.id);
  EXPECT_EQ(moved_open.workspace_id, target_.id);
  EXPECT_FALSE(moved_open.parent_id.has_value());
  EXPECT_TRUE(moved_open.is_temporary);
  EXPECT_EQ(moved_open.sort_key, "nb");
  EXPECT_TRUE(Get(closing.id).tombstone);

  // Undo brings the moved pages back; the closed tab stays closed.
  ASSERT_EQ(Result::kOk, store_->UndoLastMutation());
  EXPECT_EQ(Get(open.id).workspace_id, source_.id);
  EXPECT_EQ(Get(saved.id).workspace_id, source_.id);
  EXPECT_TRUE(Get(closing.id).tombstone);
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, RejectsInvalidMergesWithoutChange) {
  TreeNode page = NewSavedPage(source_, std::nullopt, u"Page",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(Result::kOk, store_->CreateNode(page));
  std::optional<base::Uuid> merge_folder;
  EXPECT_EQ(Result::kInvalidArgument,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = source_.id,
                                    .into_folder = true,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  EXPECT_EQ(Result::kNotFound,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = base::Uuid::GenerateRandomV4(),
                                    .into_folder = true,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  // A flat key beyond the sync limit is refused before any row changes.
  EXPECT_EQ(Result::kInvalidArgument,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = false,
                                    .sort_key = std::string(1024, 'n'),
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  EXPECT_TRUE(IsVisible(source_));
  EXPECT_EQ(Get(page.id).workspace_id, source_.id);
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, EmptySourceLeavesNoUndoEntry) {
  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = true,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  EXPECT_FALSE(merge_folder.has_value());
  EXPECT_FALSE(IsVisible(source_));
  EXPECT_EQ(Result::kNothingToUndo, store_->UndoLastMutation());
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, NoUndoWhenTheCallerRetiresState) {
  TreeNode page = NewSavedPage(source_, std::nullopt, u"Page",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(Result::kOk, store_->CreateNode(page));
  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .sort_key = "n",
                                    .record_undo = false,
                                    .modified_at = base::Time::Now()},
                                   &merge_folder));
  EXPECT_EQ(Get(page.id).workspace_id, target_.id);
  EXPECT_FALSE(IsVisible(source_));
  // The next undo is the page's creation, not the merge.
  ASSERT_EQ(Result::kOk, store_->UndoLastMutation());
  TreeNode gone;
  EXPECT_EQ(Result::kNotFound, store_->GetNode(page.id, &gone));
  EXPECT_FALSE(IsVisible(source_));
}

TEST_F(AhoiTabTreeMergeWorkspaceTest, MergeSurvivesReopen) {
  TreeNode page = NewSavedPage(source_, std::nullopt, u"Page",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(Result::kOk, store_->CreateNode(page));
  std::optional<base::Uuid> merge_folder;
  ASSERT_EQ(Result::kOk,
            store_->MergeWorkspace({.source_workspace_id = source_.id,
                                    .target_workspace_id = target_.id,
                                    .into_folder = true,
                                    .sort_key = "n",
                                                                        .modified_at = base::Time::Now()},
                                   &merge_folder));
  ASSERT_TRUE(ReopenStore());
  EXPECT_FALSE(IsVisible(source_));
  EXPECT_EQ(Get(page.id).parent_id, merge_folder);
  // The durable undo entry survives the restart too.
  ASSERT_EQ(Result::kOk, store_->UndoLastMutation());
  EXPECT_TRUE(IsVisible(source_));
}

}  // namespace

}  // namespace ahoi::tab_tree
