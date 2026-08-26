// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_link_copy.h"

#include <optional>
#include <string>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sidebar {
namespace {

tab_tree::Workspace MakeWorkspace() {
  const base::Time now = base::Time::Now();
  return {.id = base::Uuid::GenerateRandomV4(),
          .name = u"Ahoi",
          .icon = u"A",
          .sort_key = "a",
          .created_at = now,
          .modified_at = now};
}

tab_tree::TreeNode MakeFolder(const base::Uuid& workspace_id,
                              std::optional<base::Uuid> parent_id,
                              std::u16string title,
                              std::string sort_key) {
  const base::Time now = base::Time::Now();
  return {.id = base::Uuid::GenerateRandomV4(),
          .workspace_id = workspace_id,
          .parent_id = parent_id,
          .type = tab_tree::TreeNodeType::kFolder,
          .title = std::move(title),
          .icon = u"folder",
          .sort_key = std::move(sort_key),
          .created_at = now,
          .modified_at = now};
}

tab_tree::TreeNode MakePage(const base::Uuid& workspace_id,
                            std::optional<base::Uuid> parent_id,
                            std::string url,
                            std::string sort_key) {
  const base::Time now = base::Time::Now();
  return {.id = base::Uuid::GenerateRandomV4(),
          .workspace_id = workspace_id,
          .parent_id = parent_id,
          .type = tab_tree::TreeNodeType::kSavedPage,
          .title = u"Page",
          .url = GURL(std::move(url)),
          .sort_key = std::move(sort_key),
          .created_at = now,
          .modified_at = now};
}

TEST(SidebarLinkCopyTest, PreservesDepthFirstManualOrder) {
  tab_tree::TabTreeStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const tab_tree::Workspace workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store.CreateWorkspace(workspace));

  const tab_tree::TreeNode folder =
      MakeFolder(workspace.id, std::nullopt, u"Folder", "a");
  const tab_tree::TreeNode nested =
      MakeFolder(workspace.id, folder.id, u"Nested", "b");
  const tab_tree::TreeNode first =
      MakePage(workspace.id, folder.id, "https://first.example/", "a");
  const tab_tree::TreeNode nested_page =
      MakePage(workspace.id, nested.id, "https://nested.example/", "a");
  const tab_tree::TreeNode last =
      MakePage(workspace.id, std::nullopt, "https://last.example/", "b");
  for (const tab_tree::TreeNode* node :
       {&folder, &nested, &first, &nested_page, &last}) {
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store.CreateNode(*node));
  }

  std::u16string links;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            BuildOrderedLinkList(&store, workspace.id, std::nullopt, &links));
  EXPECT_EQ(
      u"https://first.example/\nhttps://nested.example/\n"
      u"https://last.example/",
      links);

  EXPECT_EQ(tab_tree::TabTreeStore::Result::kOk,
            BuildOrderedLinkList(&store, workspace.id, folder.id, &links));
  EXPECT_EQ(u"https://first.example/\nhttps://nested.example/", links);
}

TEST(SidebarLinkCopyTest, RejectsFolderFromAnotherWorkspace) {
  tab_tree::TabTreeStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const tab_tree::Workspace first = MakeWorkspace();
  tab_tree::Workspace second = MakeWorkspace();
  second.sort_key = "b";
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store.CreateWorkspace(first));
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store.CreateWorkspace(second));
  const tab_tree::TreeNode folder =
      MakeFolder(first.id, std::nullopt, u"Folder", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store.CreateNode(folder));

  std::u16string links;
  EXPECT_EQ(tab_tree::TabTreeStore::Result::kInvalidArgument,
            BuildOrderedLinkList(&store, second.id, folder.id, &links));
}

}  // namespace
}  // namespace ahoi::sidebar
