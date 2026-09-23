// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/portable_workspace_import_plan.h"

#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

base::Uuid Id(const char* text) {
  return base::Uuid::ParseLowercase(text);
}

TEST(PortableWorkspaceImportPlanTest,
     AtomicFreshArchiveCommitReplaysWithoutDuplicatesAndRejectsConflict) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(20'000);
  const base::Uuid workspace_id = Id("83699047-edf8-580d-948d-9c37acc35cb6");
  const base::Uuid live_page_id = Id("40000000-0000-4000-8000-000000000001");
  const base::Uuid archived_page_id =
      Id("40000000-0000-4000-8000-000000000002");
  const base::Uuid device_id = Id("40000000-0000-4000-8000-000000000003");

  tab_tree::TabTreeSnapshot current;
  current.workspaces.push_back({.id = workspace_id,
                                .name = u"Inbox",
                                .sort_key = "0",
                                .created_at = now,
                                .modified_at = now});
  PortableWorkspaceStructure imported;
  imported.tree.workspaces.push_back(
      {.id = workspace_id, .name = u"Inbox", .sort_key = "0"});
  imported.tree.nodes.push_back({
      .id = live_page_id,
      .workspace_id = workspace_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = u"Example Domain",
      .sort_key = "A",
      .is_temporary = true,
      .target = sync::SharedTabTarget{.kind = sync::SharedTabTargetKind::kWeb,
                                      .url = "https://example.com/"},
  });

  sync::SharedArchiveSnapshot snapshot;
  snapshot.workspace_id = workspace_id;
  snapshot.pages.push_back({
      .tree_node_id = archived_page_id,
      .sort_key = "B",
      .title = "Archived example",
      .target = sync::SharedTabTarget{.kind = sync::SharedTabTargetKind::kWeb,
                                      .url = "https://example.org/"},
  });
  const base::Uuid archive_id = sync::ArchiveIdForSnapshot(snapshot);
  ASSERT_TRUE(archive_id.is_valid());
  imported.archives.push_back({.id = archive_id,
                               .snapshot = snapshot,
                               .reason = sync::SharedArchiveReason::kManual,
                               .archived_at = now});

  WorkspaceStructureState empty_structure;
  auto plan = PreparePortableWorkspaceImport(imported, current, empty_structure,
                                             device_id, now);
  ASSERT_TRUE(plan);
  ASSERT_TRUE(plan->changed);
  ASSERT_EQ(2u, plan->tree.nodes.size());
  ASSERT_EQ(1u, plan->structure.entries.size());

  tab_tree::TabTreeStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  tab_tree::TabTreeStore::PersistenceSnapshot proposed{
      .tree = plan->tree,
      .workspace_structure_state = plan->encoded_structure,
  };
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store.ReplacePersistenceSnapshot(proposed));
  tab_tree::TabTreeStore::PersistenceSnapshot durable;
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store.ExportPersistenceSnapshot(&durable));
  EXPECT_EQ(2u, durable.tree.nodes.size());
  EXPECT_TRUE(store.IsNodeArchived(archived_page_id));
  EXPECT_FALSE(store.IsNodeArchived(live_page_id));

  const auto saved_structure =
      DecodeWorkspaceStructureState(durable.workspace_structure_state);
  ASSERT_TRUE(saved_structure);
  auto replay =
      PreparePortableWorkspaceImport(imported, durable.tree, *saved_structure,
                                     device_id, now + base::Seconds(1));
  ASSERT_TRUE(replay);
  EXPECT_FALSE(replay->changed);
  EXPECT_EQ(durable.tree, replay->tree);
  EXPECT_EQ(durable.workspace_structure_state, replay->encoded_structure);

  auto changed_tree = durable.tree;
  for (auto& node : changed_tree.nodes) {
    if (node.id == live_page_id) {
      node.title = u"Locally edited title";
    }
  }
  EXPECT_FALSE(PreparePortableWorkspaceImport(imported, changed_tree,
                                              *saved_structure, device_id,
                                              now + base::Seconds(2)));
}

}  // namespace
}  // namespace ahoi::session
