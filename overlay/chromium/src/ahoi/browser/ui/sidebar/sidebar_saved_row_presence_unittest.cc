// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_saved_row_presence.h"

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

namespace {

tab_tree::TreeNode MakePage(bool is_temporary) {
  tab_tree::TreeNode node;
  node.id = base::Uuid::GenerateRandomV4();
  node.workspace_id = base::Uuid::GenerateRandomV4();
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.title = u"Ahoi auth required";
  node.url = GURL("http://127.0.0.1:8793/a/");
  node.sort_key = "@";
  node.is_temporary = is_temporary;
  return node;
}

}  // namespace

// Owner report: a click on a "Saved tabs" row whose tab was gone reopened it
// as a temporary tab and the row left the saved section. Those rows were
// closed temporary tabs of an unrestored session, never saved pages.
TEST(SidebarSavedRowPresenceTest, SavedRowStaysAfterItsTabClosed) {
  const tab_tree::TreeNode saved = MakePage(/*is_temporary=*/false);
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(saved, {}));
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(
      saved, {.has_live_tab = true}));
  tab_tree::TreeNode blank = saved;
  blank.url = GURL("about:blank");
  blank.title = u"about:blank";
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(blank, {}));
}

TEST(SidebarSavedRowPresenceTest, ClosedLocalTemporaryPageIsNotASavedRow) {
  const tab_tree::TreeNode temporary = MakePage(/*is_temporary=*/true);
  EXPECT_TRUE(ShouldHideClosedTemporaryPageRow(temporary, {}));
  // Once a restored or reopened tab binds it, "Open tabs" presents it.
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(
      temporary, {.has_live_tab = true}));
}

TEST(SidebarSavedRowPresenceTest, RemoteAndArchivedTemporaryPagesStay) {
  const tab_tree::TreeNode temporary = MakePage(/*is_temporary=*/true);
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(
      temporary, {.created_on_other_device = true}));
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(
      temporary, {.archived = true}));
}

TEST(SidebarSavedRowPresenceTest, FoldersAndTombstonesAreNeverHiddenHere) {
  tab_tree::TreeNode folder = MakePage(/*is_temporary=*/false);
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.url = GURL();
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(folder, {}));
  tab_tree::TreeNode deleted = MakePage(/*is_temporary=*/true);
  deleted.tombstone = true;
  EXPECT_FALSE(ShouldHideClosedTemporaryPageRow(deleted, {}));
}

}  // namespace ahoi::sidebar
