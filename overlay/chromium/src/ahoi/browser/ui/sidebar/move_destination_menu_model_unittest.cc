// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {

namespace {

tab_tree::Workspace Workspace(std::u16string name, std::string sort_key) {
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = std::move(name);
  workspace.sort_key = std::move(sort_key);
  workspace.created_at = base::Time::UnixEpoch();
  workspace.modified_at = workspace.created_at;
  return workspace;
}

tab_tree::TreeNode Folder(const tab_tree::Workspace& workspace,
                          std::optional<base::Uuid> parent_id,
                          std::u16string title,
                          std::string sort_key) {
  tab_tree::TreeNode folder;
  folder.id = base::Uuid::GenerateRandomV4();
  folder.workspace_id = workspace.id;
  folder.parent_id = parent_id;
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = std::move(title);
  folder.sort_key = std::move(sort_key);
  folder.created_at = base::Time::UnixEpoch();
  folder.modified_at = folder.created_at;
  return folder;
}

tab_tree::TreeNode Page(const tab_tree::Workspace& workspace,
                        std::optional<base::Uuid> parent_id,
                        std::u16string title,
                        std::string sort_key) {
  tab_tree::TreeNode page =
      Folder(workspace, parent_id, std::move(title), std::move(sort_key));
  page.type = tab_tree::TreeNodeType::kSavedPage;
  return page;
}

TEST(MoveDestinationMenuModelTest,
     KeepsNestedHierarchyAndExcludesSourceSubtree) {
  const tab_tree::Workspace work = Workspace(u"Work", "a");
  const tab_tree::Workspace personal = Workspace(u"Personal", "b");
  const tab_tree::TreeNode current =
      Folder(work, std::nullopt, u"Current", "a");
  const tab_tree::TreeNode source = Folder(work, current.id, u"Source", "a");
  const tab_tree::TreeNode source_child =
      Folder(work, source.id, u"Must be excluded", "a");
  const tab_tree::TreeNode current_child =
      Folder(work, current.id, u"Sibling destination", "b");
  const tab_tree::TreeNode deep =
      Folder(work, current_child.id, u"Deep destination", "a");
  const tab_tree::TreeNode other =
      Folder(personal, std::nullopt, u"Other root", "a");

  tab_tree::TabTreeSnapshot snapshot;
  snapshot.workspaces = {work, personal};
  snapshot.nodes = {current, source, source_child, current_child, deep, other};
  const std::vector<MoveDestinationWorkspace> destinations =
      BuildMoveDestinationMenuModel({work, personal}, snapshot, &source);

  ASSERT_EQ(2U, destinations.size());
  EXPECT_EQ(work.id, destinations[0].id);
  EXPECT_TRUE(destinations[0].root_selectable);
  ASSERT_EQ(1U, destinations[0].folders.size());
  EXPECT_EQ(current.id, destinations[0].folders[0].id);
  EXPECT_FALSE(destinations[0].folders[0].selectable);
  ASSERT_EQ(1U, destinations[0].folders[0].children.size());
  EXPECT_EQ(current_child.id, destinations[0].folders[0].children[0].id);
  ASSERT_EQ(1U, destinations[0].folders[0].children[0].children.size());
  EXPECT_EQ(deep.id, destinations[0].folders[0].children[0].children[0].id);
  EXPECT_EQ(personal.id, destinations[1].id);
  ASSERT_EQ(1U, destinations[1].folders.size());
  EXPECT_EQ(other.id, destinations[1].folders[0].id);
}

TEST(MoveDestinationMenuModelTest,
     RootSourceDoesNotOfferCurrentRootButKeepsFolderTargets) {
  const tab_tree::Workspace workspace = Workspace(u"Work", "a");
  const tab_tree::TreeNode source = Page(workspace, std::nullopt, u"Page", "a");
  const tab_tree::TreeNode folder =
      Folder(workspace, std::nullopt, u"Folder", "b");
  tab_tree::TabTreeSnapshot snapshot;
  snapshot.workspaces = {workspace};
  snapshot.nodes = {source, folder};

  const std::vector<MoveDestinationWorkspace> destinations =
      BuildMoveDestinationMenuModel({workspace}, snapshot, &source);

  ASSERT_EQ(1U, destinations.size());
  EXPECT_FALSE(destinations[0].root_selectable);
  ASSERT_EQ(1U, destinations[0].folders.size());
  EXPECT_EQ(folder.id, destinations[0].folders[0].id);
  EXPECT_TRUE(destinations[0].folders[0].selectable);
}

}  // namespace

}  // namespace ahoi::sidebar
