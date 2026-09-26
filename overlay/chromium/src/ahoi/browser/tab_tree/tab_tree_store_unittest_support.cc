// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/tab_tree/tab_tree_store_unittest_support.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"

namespace ahoi::tab_tree::test_support {

AhoiTabTreeStoreTest::AhoiTabTreeStoreTest() = default;

AhoiTabTreeStoreTest::~AhoiTabTreeStoreTest() = default;

void AhoiTabTreeStoreTest::SetUp() {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  database_path_ = temp_dir_.GetPath().AppendASCII("AhoiTree.sqlite");
  ASSERT_TRUE(ReopenStore());
}

bool AhoiTabTreeStoreTest::ReopenStore() {
  store_ = std::make_unique<TabTreeStore>();
  return store_->Initialize(database_path_);
}

Workspace AhoiTabTreeStoreTest::NewWorkspace(std::u16string name,
                                             std::string sort_key) {
  Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = std::move(name);
  workspace.icon = u"folder";
  workspace.sort_key = std::move(sort_key);
  workspace.created_at = base::Time::Now();
  workspace.modified_at = workspace.created_at;
  return workspace;
}

TreeNode AhoiTabTreeStoreTest::NewFolder(const Workspace& workspace,
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

TreeNode AhoiTabTreeStoreTest::NewSavedPage(
    const Workspace& workspace,
    std::optional<base::Uuid> parent_id,
    std::u16string title,
    const GURL& url,
    std::string sort_key) {
  TreeNode node = NewFolder(workspace, std::move(parent_id), std::move(title),
                            std::move(sort_key));
  node.type = TreeNodeType::kSavedPage;
  node.url = url;
  // The store records a saved page's Home on creation; expect it.
  InitializeSavedHome(&node);
  return node;
}

}  // namespace ahoi::tab_tree::test_support
