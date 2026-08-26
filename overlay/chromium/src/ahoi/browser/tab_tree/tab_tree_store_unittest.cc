// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/tab_tree/tab_tree_store.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_observer.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::tab_tree {

namespace {

class RecordingObserver : public TabTreeObserver {
 public:
  void OnTabTreeChanged(const TabTreeChange& change) override {
    changes.push_back(change);
  }

  std::vector<TabTreeChange> changes;
};

class AhoiTabTreeStoreTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    database_path_ = temp_dir_.GetPath().AppendASCII("AhoiTree.sqlite");
    ASSERT_TRUE(ReopenStore());
  }

 protected:
  bool ReopenStore() {
    store_ = std::make_unique<TabTreeStore>();
    return store_->Initialize(database_path_);
  }

  Workspace NewWorkspace(std::u16string name, std::string sort_key) {
    Workspace workspace;
    workspace.id = base::Uuid::GenerateRandomV4();
    workspace.name = std::move(name);
    workspace.icon = u"folder";
    workspace.sort_key = std::move(sort_key);
    workspace.created_at = base::Time::Now();
    workspace.modified_at = workspace.created_at;
    return workspace;
  }

  TreeNode NewFolder(const Workspace& workspace,
                     std::optional<base::Uuid> parent_id,
                     std::u16string title,
                     std::string sort_key) {
    TreeNode node;
    node.id = base::Uuid::GenerateRandomV4();
    node.workspace_id = workspace.id;
    node.parent_id = std::move(parent_id);
    node.type = TreeNodeType::kFolder;
    node.title = std::move(title);
    node.sort_key = std::move(sort_key);
    node.created_at = base::Time::Now();
    node.modified_at = node.created_at;
    return node;
  }

  TreeNode NewSavedPage(const Workspace& workspace,
                        std::optional<base::Uuid> parent_id,
                        std::u16string title,
                        const GURL& url,
                        std::string sort_key) {
    TreeNode node = NewFolder(workspace, std::move(parent_id), std::move(title),
                              std::move(sort_key));
    node.type = TreeNodeType::kSavedPage;
    node.url = url;
    return node;
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath database_path_;
  std::unique_ptr<TabTreeStore> store_;
};

TEST_F(AhoiTabTreeStoreTest, PersistsDeepHierarchyAndManualOrder) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));

  std::optional<base::Uuid> parent_id;
  std::vector<TreeNode> folders;
  for (int depth = 0; depth < 12; ++depth) {
    TreeNode folder = NewFolder(workspace, parent_id, u"Nested folder",
                                "folder-" + std::to_string(depth));
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
    parent_id = folder.id;
    folders.push_back(std::move(folder));
  }

  TreeNode page_b = NewSavedPage(workspace, parent_id, u"B page",
                                 GURL("https://example.test/b"), "b");
  TreeNode page_a = NewSavedPage(workspace, parent_id, u"A page",
                                 GURL("https://example.test/a"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page_b));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page_a));

  store_.reset();
  ASSERT_TRUE(ReopenStore());

  TreeNode persisted;
  EXPECT_EQ(TabTreeStore::Result::kOk, store_->GetNode(page_a.id, &persisted));
  EXPECT_EQ(page_a, persisted);

  std::vector<TreeNode> children;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetChildren(workspace.id, parent_id, &children));
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ(page_a.id, children[0].id);
  EXPECT_EQ(page_b.id, children[1].id);
  ASSERT_TRUE(children[0].parent_id.has_value());
  EXPECT_EQ(folders.back().id, *children[0].parent_id);
}

TEST_F(AhoiTabTreeStoreTest, ListsActiveWorkspacesInStableOrder) {
  Workspace later = NewWorkspace(u"Later", "b");
  Workspace earlier = NewWorkspace(u"Earlier", "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(later));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(earlier));

  std::vector<Workspace> workspaces;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetWorkspaces(&workspaces));
  ASSERT_EQ(2u, workspaces.size());
  EXPECT_EQ(earlier.id, workspaces[0].id);
  EXPECT_EQ(later.id, workspaces[1].id);
  EXPECT_EQ(TabTreeStore::Result::kInvalidArgument,
            store_->GetWorkspaces(nullptr));
}

TEST_F(AhoiTabTreeStoreTest, UpdatesWorkspacePresentationAndPersistsIt) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));

  const base::Time modified_at = workspace.modified_at + base::Minutes(1);
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->UpdateWorkspacePresentation(
                workspace.id, u"Client work", u"C", 0xFF4F8DE8u, modified_at));

  store_.reset();
  ASSERT_TRUE(ReopenStore());
  Workspace persisted;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetWorkspace(workspace.id, &persisted));
  EXPECT_EQ(persisted.name, u"Client work");
  EXPECT_EQ(persisted.icon, u"C");
  EXPECT_EQ(persisted.accent_argb, 0xFF4F8DE8u);
  EXPECT_EQ(persisted.modified_at, modified_at);
}

TEST_F(AhoiTabTreeStoreTest, DeletesWorkspaceAndItsTreeAtomically) {
  Workspace deleted = NewWorkspace(u"Delete me", "workspace-a");
  Workspace fallback = NewWorkspace(u"Keep me", "workspace-b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(deleted));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(fallback));
  TreeNode folder = NewFolder(deleted, std::nullopt, u"Project", "folder-a");
  TreeNode page = NewSavedPage(deleted, folder.id, u"Page",
                               GURL("https://example.test/"), "page-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));

  RecordingObserver observer;
  store_->AddObserver(&observer);
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DeleteWorkspace(deleted.id, base::Time::Now()));
  ASSERT_EQ(observer.changes.size(), 1u);
  EXPECT_EQ(observer.changes.front().kind, MutationKind::kDeleted);
  EXPECT_EQ(observer.changes.front().node_ids.size(), 2u);
  store_->RemoveObserver(&observer);

  std::vector<Workspace> workspaces;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetWorkspaces(&workspaces));
  ASSERT_EQ(workspaces.size(), 1u);
  EXPECT_EQ(workspaces.front().id, fallback.id);
  TreeNode tombstoned;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(folder.id, &tombstoned));
  EXPECT_TRUE(tombstoned.tombstone);
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(page.id, &tombstoned));
  EXPECT_TRUE(tombstoned.tombstone);
  EXPECT_EQ(TabTreeStore::Result::kInvalidArgument,
            store_->DeleteWorkspace(fallback.id, base::Time::Now()));
}

TEST_F(AhoiTabTreeStoreTest, DuplicatesDeepWorkspaceAndRemapsParents) {
  Workspace source = NewWorkspace(u"Source", "workspace-a");
  Workspace duplicate = NewWorkspace(u"Source copy", "workspace-b");
  duplicate.icon = u"copy";
  duplicate.accent_argb = 0xFF4F8DE8u;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(source));

  TreeNode root = NewFolder(source, std::nullopt, u"Project", "a");
  TreeNode nested = NewFolder(source, root.id, u"Nested", "a");
  TreeNode page = NewSavedPage(source, nested.id, u"Issue tracker",
                               GURL("https://issues.example.test/"), "a");
  TreeNode root_page = NewSavedPage(source, std::nullopt, u"Readme",
                                    GURL("https://example.test/readme"), "b");
  TreeNode tombstoned_page = NewSavedPage(
      source, root.id, u"Removed", GURL("https://example.test/removed"), "z");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(nested));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root_page));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(tombstoned_page));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DeleteNode(tombstoned_page.id, base::Time::Now()));

  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DuplicateWorkspace(source.id, duplicate));

  Workspace persisted_duplicate;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetWorkspace(duplicate.id, &persisted_duplicate));
  EXPECT_EQ(duplicate, persisted_duplicate);

  std::vector<TreeNode> copied_roots;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetChildren(duplicate.id, std::nullopt, &copied_roots));
  ASSERT_EQ(2u, copied_roots.size());
  const TreeNode& copied_root = copied_roots[0];
  const TreeNode& copied_root_page = copied_roots[1];
  EXPECT_EQ(root.title, copied_root.title);
  EXPECT_EQ(root.sort_key, copied_root.sort_key);
  EXPECT_EQ(root.icon, copied_root.icon);
  EXPECT_EQ(root.created_at, copied_root.created_at);
  EXPECT_EQ(root.modified_at, copied_root.modified_at);
  EXPECT_EQ(duplicate.id, copied_root.workspace_id);
  EXPECT_FALSE(copied_root.parent_id.has_value());
  EXPECT_NE(root.id, copied_root.id);
  EXPECT_EQ(root_page.title, copied_root_page.title);
  EXPECT_EQ(root_page.url, copied_root_page.url);
  EXPECT_EQ(duplicate.id, copied_root_page.workspace_id);
  EXPECT_FALSE(copied_root_page.parent_id.has_value());
  EXPECT_NE(root_page.id, copied_root_page.id);
  EXPECT_NE(copied_root.id, copied_root_page.id);

  std::vector<TreeNode> copied_nested_nodes;
  ASSERT_EQ(
      TabTreeStore::Result::kOk,
      store_->GetChildren(duplicate.id, copied_root.id, &copied_nested_nodes));
  ASSERT_EQ(1u, copied_nested_nodes.size());
  const TreeNode& copied_nested = copied_nested_nodes.front();
  EXPECT_EQ(nested.title, copied_nested.title);
  EXPECT_EQ(nested.sort_key, copied_nested.sort_key);
  EXPECT_EQ(duplicate.id, copied_nested.workspace_id);
  ASSERT_TRUE(copied_nested.parent_id.has_value());
  EXPECT_EQ(copied_root.id, *copied_nested.parent_id);
  EXPECT_NE(nested.id, copied_nested.id);

  std::vector<TreeNode> copied_pages;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetChildren(duplicate.id, copied_nested.id, &copied_pages));
  ASSERT_EQ(1u, copied_pages.size());
  const TreeNode& copied_page = copied_pages.front();
  EXPECT_EQ(page.title, copied_page.title);
  EXPECT_EQ(page.url, copied_page.url);
  EXPECT_EQ(page.sort_key, copied_page.sort_key);
  EXPECT_EQ(duplicate.id, copied_page.workspace_id);
  ASSERT_TRUE(copied_page.parent_id.has_value());
  EXPECT_EQ(copied_nested.id, *copied_page.parent_id);
  EXPECT_NE(page.id, copied_page.id);

  for (const TreeNode& source_node : {root, nested, page, root_page}) {
    TreeNode persisted_source;
    ASSERT_EQ(TabTreeStore::Result::kOk,
              store_->GetNode(source_node.id, &persisted_source));
    EXPECT_EQ(source_node, persisted_source);
  }
  TreeNode persisted_tombstone;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(tombstoned_page.id, &persisted_tombstone));
  EXPECT_TRUE(persisted_tombstone.tombstone);
}

TEST_F(AhoiTabTreeStoreTest, RejectsInvalidWorkspaceDuplicateAtomically) {
  Workspace source = NewWorkspace(u"Source", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(source));
  TreeNode source_node =
      NewFolder(source, std::nullopt, u"Source root", "root");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(source_node));

  Workspace invalid_duplicate = NewWorkspace(u"Invalid copy", "workspace-b");
  invalid_duplicate.sort_key.clear();
  EXPECT_EQ(TabTreeStore::Result::kInvalidArgument,
            store_->DuplicateWorkspace(source.id, invalid_duplicate));

  std::vector<Workspace> workspaces;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetWorkspaces(&workspaces));
  ASSERT_EQ(1u, workspaces.size());
  EXPECT_EQ(source, workspaces.front());
  Workspace missing_duplicate;
  EXPECT_EQ(TabTreeStore::Result::kNotFound,
            store_->GetWorkspace(invalid_duplicate.id, &missing_duplicate));
  TreeNode persisted_source;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(source_node.id, &persisted_source));
  EXPECT_EQ(source_node, persisted_source);
}

TEST_F(AhoiTabTreeStoreTest, FindsSavedPagesByUrlAtAnyNestingDepth) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode folder = NewFolder(workspace, std::nullopt, u"Project", "folder-a");
  TreeNode nested = NewSavedPage(workspace, folder.id, u"Nested",
                                 GURL("https://example.test/reused"), "a");
  TreeNode root = NewSavedPage(workspace, std::nullopt, u"Root",
                               GURL("https://example.test/reused"), "b");
  TreeNode other = NewSavedPage(workspace, folder.id, u"Other",
                                GURL("https://example.test/other"), "c");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(nested));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(other));

  std::vector<TreeNode> matches;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->FindSavedPagesByUrl(
                workspace.id, GURL("https://example.test/reused"), &matches));
  ASSERT_EQ(2u, matches.size());
  EXPECT_EQ(root.id, matches[0].id);
  EXPECT_EQ(nested.id, matches[1].id);

  store_.reset();
  ASSERT_TRUE(ReopenStore());
  matches.clear();
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->FindSavedPagesByUrl(
                workspace.id, GURL("https://example.test/reused"), &matches));
  ASSERT_EQ(2u, matches.size());
  EXPECT_EQ(root.id, matches[0].id);
  EXPECT_EQ(nested.id, matches[1].id);
}

TEST_F(AhoiTabTreeStoreTest, RejectsCycleWithoutCreatingUndoEntry) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode root = NewFolder(workspace, std::nullopt, u"Root", "root-sort-key");
  TreeNode child = NewFolder(workspace, root.id, u"Child", "child-sort-key");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(child));

  RecordingObserver observer;
  store_->AddObserver(&observer);
  EXPECT_EQ(TabTreeStore::Result::kCycle,
            store_->MoveNode(root.id, workspace.id, child.id, "moved",
                             base::Time::Now()));
  EXPECT_TRUE(observer.changes.empty());

  TreeNode persisted_root;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(root.id, &persisted_root));
  EXPECT_FALSE(persisted_root.parent_id.has_value());

  // The failed move did not append an undo record: the latest successful
  // mutation (creating `child`) is undone instead.
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  TreeNode missing_child;
  EXPECT_EQ(TabTreeStore::Result::kNotFound,
            store_->GetNode(child.id, &missing_child));
  ASSERT_EQ(1u, observer.changes.size());
  EXPECT_EQ(MutationKind::kUndone, observer.changes[0].kind);
  store_->RemoveObserver(&observer);
}

TEST_F(AhoiTabTreeStoreTest, MovesWholeSubtreeAcrossWorkspacesAndUndoes) {
  Workspace source = NewWorkspace(u"Source", "workspace-a");
  Workspace destination = NewWorkspace(u"Destination", "workspace-b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(source));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(destination));

  TreeNode root = NewFolder(source, std::nullopt, u"Project", "a");
  TreeNode child = NewFolder(source, root.id, u"Frontend", "a");
  TreeNode page = NewSavedPage(source, child.id, u"Local app",
                               GURL("https://localhost.test/"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(child));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));

  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->MoveNode(root.id, destination.id, std::nullopt,
                             "destination-a", base::Time::Now()));
  for (const base::Uuid& id : {root.id, child.id, page.id}) {
    TreeNode moved;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(id, &moved));
    EXPECT_EQ(destination.id, moved.workspace_id);
  }

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  for (const base::Uuid& id : {root.id, child.id, page.id}) {
    TreeNode restored;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(id, &restored));
    EXPECT_EQ(source.id, restored.workspace_id);
  }
  TreeNode restored_root;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(root.id, &restored_root));
  EXPECT_FALSE(restored_root.parent_id.has_value());
  EXPECT_EQ("a", restored_root.sort_key);
}

TEST_F(AhoiTabTreeStoreTest,
       SameWorkspaceMoveDoesNotRewriteDescendantsAndUndoes) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode destination =
      NewFolder(workspace, std::nullopt, u"Destination", "a");
  TreeNode moved_root = NewFolder(workspace, std::nullopt, u"Moved root", "b");
  TreeNode child = NewFolder(workspace, moved_root.id, u"Unchanged child", "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(destination));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(moved_root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(child));

  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->MoveNode(moved_root.id, workspace.id, destination.id, "a",
                             base::Time::Now()));
  TreeNode persisted_child;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(child.id, &persisted_child));
  EXPECT_EQ(child, persisted_child);

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  TreeNode restored_root;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(moved_root.id, &restored_root));
  EXPECT_FALSE(restored_root.parent_id.has_value());
  EXPECT_EQ("b", restored_root.sort_key);
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(child.id, &persisted_child));
  EXPECT_EQ(child, persisted_child);
}

TEST_F(AhoiTabTreeStoreTest, MovesSavedPagesAtomicallyAndOneUndoRestoresBoth) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode source = NewFolder(workspace, std::nullopt, u"Source", "a");
  TreeNode destination =
      NewFolder(workspace, std::nullopt, u"Destination", "b");
  TreeNode first = NewSavedPage(workspace, source.id, u"First",
                                GURL("https://example.test/first"), "a");
  TreeNode second = NewSavedPage(workspace, source.id, u"Second",
                                 GURL("https://example.test/second"), "b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(source));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(destination));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(first));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(second));

  RecordingObserver observer;
  store_->AddObserver(&observer);
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->MoveSavedPagesAtomically({{.node_id = first.id,
                                               .workspace_id = workspace.id,
                                               .parent_id = destination.id,
                                               .sort_key = "a"},
                                              {.node_id = second.id,
                                               .workspace_id = workspace.id,
                                               .parent_id = destination.id,
                                               .sort_key = "b"}},
                                             base::Time::Now()));
  ASSERT_EQ(1u, observer.changes.size());
  EXPECT_EQ(MutationKind::kMoved, observer.changes.back().kind);
  EXPECT_EQ((std::vector<base::Uuid>{first.id, second.id}),
            observer.changes.back().node_ids);

  for (const base::Uuid& node_id : {first.id, second.id}) {
    TreeNode moved;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(node_id, &moved));
    ASSERT_TRUE(moved.parent_id.has_value());
    EXPECT_EQ(destination.id, *moved.parent_id);
  }

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  TreeNode restored_first;
  TreeNode restored_second;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(first.id, &restored_first));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(second.id, &restored_second));
  EXPECT_EQ(first, restored_first);
  EXPECT_EQ(second, restored_second);
  store_->RemoveObserver(&observer);
}

TEST_F(AhoiTabTreeStoreTest, WrapsSplitPagesInOneFolderAndUndoRestoresBoth) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode source = NewFolder(workspace, std::nullopt, u"Source", "a");
  TreeNode first = NewSavedPage(workspace, source.id, u"First",
                                GURL("https://example.test/first"), "a");
  TreeNode second = NewSavedPage(workspace, source.id, u"Second",
                                 GURL("https://example.test/second"), "b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(source));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(first));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(second));

  base::Uuid folder_id;
  ASSERT_EQ(
      TabTreeStore::Result::kOk,
      store_->CreateFolderAroundNodes({first.id, second.id}, u"Split project",
                                      base::Time::Now(), &folder_id));
  TreeNode folder;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(folder_id, &folder));
  EXPECT_EQ(source.id, folder.parent_id);
  EXPECT_EQ(first.sort_key, folder.sort_key);

  std::vector<TreeNode> children;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetChildren(workspace.id, folder_id, &children));
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ(first.id, children[0].id);
  EXPECT_EQ(second.id, children[1].id);

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  EXPECT_EQ(TabTreeStore::Result::kNotFound,
            store_->GetNode(folder_id, &folder));
  TreeNode restored_first;
  TreeNode restored_second;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(first.id, &restored_first));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(second.id, &restored_second));
  EXPECT_EQ(first, restored_first);
  EXPECT_EQ(second, restored_second);
}

TEST_F(AhoiTabTreeStoreTest, DeleteTombstonesSubtreeAndDurableUndoRestoresIt) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode root = NewFolder(workspace, std::nullopt, u"Project", "a");
  TreeNode child = NewSavedPage(workspace, root.id, u"Issue tracker",
                                GURL("https://issues.example.test/"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(root));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(child));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DeleteNode(root.id, base::Time::Now()));

  for (const base::Uuid& id : {root.id, child.id}) {
    TreeNode deleted;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(id, &deleted));
    EXPECT_TRUE(deleted.tombstone);
  }
  std::vector<TreeNode> visible_roots;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetChildren(workspace.id, std::nullopt, &visible_roots));
  EXPECT_TRUE(visible_roots.empty());

  store_.reset();
  ASSERT_TRUE(ReopenStore());
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  for (const base::Uuid& id : {root.id, child.id}) {
    TreeNode restored;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(id, &restored));
    EXPECT_FALSE(restored.tombstone);
  }
}

TEST_F(AhoiTabTreeStoreTest, AtomicDeleteAndUndoKeepsSplitPagesTogether) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode first = NewSavedPage(workspace, std::nullopt, u"First",
                                GURL("https://example.test/first"), "a");
  TreeNode second = NewSavedPage(workspace, std::nullopt, u"Second",
                                 GURL("https://example.test/second"), "b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(first));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(second));

  RecordingObserver observer;
  store_->AddObserver(&observer);
  ASSERT_EQ(
      TabTreeStore::Result::kOk,
      store_->DeleteNodesAtomically({first.id, second.id}, base::Time::Now()));
  ASSERT_EQ(1u, observer.changes.size());
  EXPECT_EQ(MutationKind::kDeleted, observer.changes.back().kind);
  EXPECT_EQ((std::vector<base::Uuid>{first.id, second.id}),
            observer.changes.back().node_ids);
  for (const base::Uuid& node_id : {first.id, second.id}) {
    TreeNode deleted;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(node_id, &deleted));
    EXPECT_TRUE(deleted.tombstone);
  }

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  TreeNode restored_first;
  TreeNode restored_second;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(first.id, &restored_first));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->GetNode(second.id, &restored_second));
  EXPECT_EQ(first, restored_first);
  EXPECT_EQ(second, restored_second);
  store_->RemoveObserver(&observer);
}

TEST_F(AhoiTabTreeStoreTest, CreateRenameAndUndoAreLifoAndAtomic) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode page = NewSavedPage(workspace, std::nullopt, u"Before",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->RenameNode(page.id, u"After", base::Time::Now()));

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  TreeNode restored;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(page.id, &restored));
  EXPECT_EQ(u"Before", restored.title);

  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  EXPECT_EQ(TabTreeStore::Result::kNotFound,
            store_->GetNode(page.id, &restored));
  EXPECT_EQ(TabTreeStore::Result::kNothingToUndo, store_->UndoLastMutation());
}

TEST_F(AhoiTabTreeStoreTest, LiveMetadataUpdatePersistsWithoutPollutingUndo) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode page = NewSavedPage(workspace, std::nullopt, u"Manual title",
                               GURL("https://example.test/before"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->UpdateSavedPageMetadata(page.id, page.title,
                                            GURL("https://example.test/after"),
                                            base::Time::Now()));

  store_.reset();
  ASSERT_TRUE(ReopenStore());
  TreeNode updated;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->GetNode(page.id, &updated));
  EXPECT_EQ(page.title, updated.title);
  EXPECT_EQ(GURL("https://example.test/after"), updated.url);

  // The automatic destination refresh did not become a separate tree-edit
  // undo step, so undo still reverses the user's original create operation.
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->UndoLastMutation());
  EXPECT_EQ(TabTreeStore::Result::kNotFound,
            store_->GetNode(page.id, &updated));
}

TEST_F(AhoiTabTreeStoreTest, SnapshotRoundTripsTreeTombstonesAndUndoHistory) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode folder = NewFolder(workspace, std::nullopt, u"Project", "folder-a");
  TreeNode page = NewSavedPage(workspace, folder.id, u"Before",
                               GURL("https://example.test/"), "page-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->RenameNode(page.id, u"After", base::Time::Now()));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DeleteNode(folder.id, base::Time::Now()));

  TabTreeSnapshot snapshot;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->ExportSnapshot(&snapshot));
  ASSERT_EQ(1u, snapshot.workspaces.size());
  ASSERT_EQ(2u, snapshot.nodes.size());
  ASSERT_EQ(4u, snapshot.undo_operations.size());

  TabTreeStore restored;
  ASSERT_TRUE(restored.InitializeInMemory());
  ASSERT_EQ(TabTreeStore::Result::kOk, restored.ReplaceWithSnapshot(snapshot));
  TabTreeSnapshot restored_snapshot;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.ExportSnapshot(&restored_snapshot));
  EXPECT_EQ(snapshot, restored_snapshot);

  ASSERT_EQ(TabTreeStore::Result::kOk, restored.UndoLastMutation());
  TreeNode restored_page;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.GetNode(page.id, &restored_page));
  EXPECT_FALSE(restored_page.tombstone);
  EXPECT_EQ(u"After", restored_page.title);
  ASSERT_EQ(TabTreeStore::Result::kOk, restored.UndoLastMutation());
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.GetNode(page.id, &restored_page));
  EXPECT_EQ(u"Before", restored_page.title);
}

TEST_F(AhoiTabTreeStoreTest, RejectsSavedPageAsParent) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode page = NewSavedPage(workspace, std::nullopt, u"Page",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  TreeNode invalid_child = NewFolder(workspace, page.id, u"Invalid child", "a");
  EXPECT_EQ(TabTreeStore::Result::kInvalidArgument,
            store_->CreateNode(invalid_child));
}

TEST_F(AhoiTabTreeStoreTest, RefusesTooNewSchemaWithoutRazingData) {
  store_.reset();
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(database_path_));
    sql::MetaTable meta_table;
    ASSERT_TRUE(meta_table.Init(&database, TabTreeStore::kCurrentSchemaVersion,
                                TabTreeStore::kCurrentSchemaVersion));
    ASSERT_TRUE(
        meta_table.SetVersionNumber(TabTreeStore::kCurrentSchemaVersion + 1));
    ASSERT_TRUE(meta_table.SetCompatibleVersionNumber(
        TabTreeStore::kCurrentSchemaVersion + 1));
    ASSERT_TRUE(database.Execute(
        "CREATE TABLE schema_sentinel(value INTEGER NOT NULL)"));
  }

  store_ = std::make_unique<TabTreeStore>();
  EXPECT_FALSE(store_->Initialize(database_path_));
  store_.reset();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(database_path_));
  EXPECT_TRUE(database.DoesTableExist("schema_sentinel"));
}

}  // namespace

}  // namespace ahoi::tab_tree
