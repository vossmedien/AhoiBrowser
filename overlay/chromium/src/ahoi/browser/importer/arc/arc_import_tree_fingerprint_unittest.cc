// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

base::Uuid Uuid(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

base::Time Time(int64_t microseconds) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

tab_tree::TabTreeSnapshot Snapshot() {
  tab_tree::Workspace first_workspace;
  first_workspace.id = Uuid("10000000-0000-4000-8000-000000000001");
  first_workspace.name = u"Private workspace";
  first_workspace.icon = u"folder";
  first_workspace.sort_key = "b";
  first_workspace.accent_argb = 0xff102030;
  first_workspace.created_at = Time(100);
  first_workspace.modified_at = Time(200);

  tab_tree::Workspace second_workspace = first_workspace;
  second_workspace.id = Uuid("10000000-0000-4000-8000-000000000002");
  second_workspace.name = u"Second workspace";
  second_workspace.sort_key = "a";

  tab_tree::TreeNode first_node;
  first_node.id = Uuid("20000000-0000-4000-8000-000000000001");
  first_node.workspace_id = first_workspace.id;
  first_node.type = tab_tree::TreeNodeType::kSavedPage;
  first_node.title = u"Private title";
  first_node.url = GURL("https://private.example/path");
  first_node.sort_key = "b";
  first_node.created_at = Time(300);
  first_node.modified_at = Time(400);

  tab_tree::TreeNode second_node = first_node;
  second_node.id = Uuid("20000000-0000-4000-8000-000000000002");
  second_node.workspace_id = second_workspace.id;
  second_node.title = u"Second title";
  second_node.sort_key = "a";

  tab_tree::TreeNode previous = first_node;
  previous.title = u"Previous private title";
  tab_tree::UndoOperationSnapshot first_undo{
      .operation_id = 2,
      .kind = tab_tree::UndoMutationKind::kRename,
      .subject_node_id = first_node.id,
      .created_at = Time(500),
      .nodes = {{.node_id = first_node.id, .previous = previous},
                {.node_id = second_node.id, .previous = std::nullopt}}};
  tab_tree::UndoOperationSnapshot second_undo{
      .operation_id = 1,
      .kind = tab_tree::UndoMutationKind::kCreate,
      .subject_node_id = second_node.id,
      .created_at = Time(450),
      .nodes = {{.node_id = second_node.id, .previous = std::nullopt}}};

  return {.workspaces = {first_workspace, second_workspace},
          .nodes = {first_node, second_node},
          .undo_operations = {first_undo, second_undo}};
}

template <typename Mutator>
void ExpectFingerprintChange(const tab_tree::TabTreeSnapshot& original,
                             Mutator mutator) {
  tab_tree::TabTreeSnapshot changed = original;
  mutator(&changed);
  EXPECT_NE(ComputeArcImportTreeFingerprint(original),
            ComputeArcImportTreeFingerprint(changed));
}

TEST(ArcImportTreeFingerprintTest, CanonicalizesPersistedCollectionOrder) {
  const tab_tree::TabTreeSnapshot original = Snapshot();
  tab_tree::TabTreeSnapshot reordered = original;
  std::ranges::reverse(reordered.workspaces);
  std::ranges::reverse(reordered.nodes);
  std::ranges::reverse(reordered.undo_operations);

  const std::string fingerprint = ComputeArcImportTreeFingerprint(original);
  EXPECT_TRUE(IsArcImportTreeFingerprint(fingerprint));
  EXPECT_EQ(fingerprint, ComputeArcImportTreeFingerprint(reordered));
  EXPECT_EQ(std::string::npos, fingerprint.find("Private"));
  EXPECT_EQ(std::string::npos, fingerprint.find("https://"));
  EXPECT_FALSE(IsArcImportTreeFingerprint(std::string(64, 'A')));
}

TEST(ArcImportTreeFingerprintTest, PreservesPersistedUndoOrdinal) {
  const tab_tree::TabTreeSnapshot original = Snapshot();
  tab_tree::TabTreeSnapshot reordered = original;
  std::ranges::reverse(reordered.undo_operations[0].nodes);

  EXPECT_NE(ComputeArcImportTreeFingerprint(original),
            ComputeArcImportTreeFingerprint(reordered));
}

TEST(ArcImportTreeFingerprintTest, CoversEveryWorkspaceField) {
  const tab_tree::TabTreeSnapshot original = Snapshot();
  ExpectFingerprintChange(
      original, [](auto* value) { ++value->workspaces[0].model_version; });
  ExpectFingerprintChange(original, [](auto* value) {
    value->workspaces[0].id = Uuid("10000000-0000-4000-8000-000000000003");
  });
  ExpectFingerprintChange(
      original, [](auto* value) { value->workspaces[0].name += u"x"; });
  ExpectFingerprintChange(
      original, [](auto* value) { value->workspaces[0].icon += u"x"; });
  ExpectFingerprintChange(
      original, [](auto* value) { value->workspaces[0].sort_key += "x"; });
  ExpectFingerprintChange(
      original, [](auto* value) { value->workspaces[0].accent_argb.reset(); });
  ExpectFingerprintChange(original, [](auto* value) {
    value->workspaces[0].created_at += base::Microseconds(1);
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->workspaces[0].modified_at += base::Microseconds(1);
  });
  ExpectFingerprintChange(
      original, [](auto* value) { value->workspaces[0].tombstone = true; });
}

TEST(ArcImportTreeFingerprintTest, CoversEveryTreeNodeField) {
  const tab_tree::TabTreeSnapshot original = Snapshot();
  ExpectFingerprintChange(original,
                          [](auto* value) { ++value->nodes[0].model_version; });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].id = Uuid("20000000-0000-4000-8000-000000000003");
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].workspace_id = value->workspaces[1].id;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].parent_id = value->nodes[1].id;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].type = tab_tree::TreeNodeType::kFolder;
  });
  ExpectFingerprintChange(original,
                          [](auto* value) { value->nodes[0].title += u"x"; });
  ExpectFingerprintChange(original,
                          [](auto* value) { value->nodes[0].icon += u"x"; });
  ExpectFingerprintChange(
      original, [](auto* value) { value->nodes[0].accent_argb = 0xffabcdef; });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].url = GURL("https://changed.example/");
  });
  ExpectFingerprintChange(original,
                          [](auto* value) { value->nodes[0].sort_key += "x"; });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].created_at += base::Microseconds(1);
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->nodes[0].modified_at += base::Microseconds(1);
  });
  ExpectFingerprintChange(
      original, [](auto* value) { value->nodes[0].tombstone = true; });
}

TEST(ArcImportTreeFingerprintTest, CoversUndoAndNestedPreviousFields) {
  const tab_tree::TabTreeSnapshot original = Snapshot();
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].operation_id = 3;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].kind = tab_tree::UndoMutationKind::kMove;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].subject_node_id = value->nodes[1].id;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].created_at += base::Microseconds(1);
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].nodes[0].node_id = value->nodes[1].id;
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].nodes[0].previous->title += u"x";
  });
  ExpectFingerprintChange(original, [](auto* value) {
    value->undo_operations[0].nodes[0].previous.reset();
  });
}

}  // namespace

}  // namespace ahoi::importer::arc
