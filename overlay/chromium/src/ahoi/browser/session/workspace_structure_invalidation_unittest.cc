// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_structure_controller.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

// Handoff 011 S3: a selection change while an archive commit is in flight
// must not cancel it.
TEST(WorkspaceStructureInvalidationTest, SelectionOnlyKeepsStructureAuthority) {
  EXPECT_FALSE(
      TabStripChangeInvalidatesStructure(TabStripModelChange::kSelectionOnly));
  EXPECT_TRUE(
      TabStripChangeInvalidatesStructure(TabStripModelChange::kInserted));
  EXPECT_TRUE(
      TabStripChangeInvalidatesStructure(TabStripModelChange::kRemoved));
  EXPECT_TRUE(
      TabStripChangeInvalidatesStructure(TabStripModelChange::kMoved));
  EXPECT_TRUE(
      TabStripChangeInvalidatesStructure(TabStripModelChange::kReplaced));
}

// A loading page updates its saved title and URL (kRenamed) while the archive
// entry is being written; the archive survives.
TEST(WorkspaceStructureInvalidationTest, ArchiveSurvivesUnrelatedTitleChange) {
  EXPECT_FALSE(
      TreeChangeInvalidatesStructure(tab_tree::MutationKind::kRenamed));
  EXPECT_TRUE(
      TreeChangeInvalidatesStructure(tab_tree::MutationKind::kCreated));
  EXPECT_TRUE(
      TreeChangeInvalidatesStructure(tab_tree::MutationKind::kMoved));
  EXPECT_TRUE(
      TreeChangeInvalidatesStructure(tab_tree::MutationKind::kDeleted));
  EXPECT_TRUE(
      TreeChangeInvalidatesStructure(tab_tree::MutationKind::kUndone));
}

// Only a page that newly becomes protected (audio, capture, unsaved form)
// cancels a pending archive or remote dissolve.
TEST(WorkspaceStructureInvalidationTest, OnlyNewProtectionCancels) {
  EXPECT_TRUE(ResourceChangeInvalidatesStructure(false, true));
  EXPECT_FALSE(ResourceChangeInvalidatesStructure(true, true));
  EXPECT_FALSE(ResourceChangeInvalidatesStructure(true, false));
  EXPECT_FALSE(ResourceChangeInvalidatesStructure(false, false));
}

}  // namespace
}  // namespace ahoi::session
