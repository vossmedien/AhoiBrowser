// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_structure_state.h"

#include <set>
#include <utility>
#include <vector>

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

sync::TabArchiveEntryRecord MakeArchive(std::vector<base::Uuid> pages,
                                        bool restored) {
  sync::TabArchiveEntryRecord archive;
  archive.id = base::Uuid::GenerateRandomV4();
  archive.snapshot.workspace_id = base::Uuid::GenerateRandomV4();
  for (const auto& id : pages) {
    sync::SharedArchivePageSnapshot page;
    page.tree_node_id = id;
    archive.snapshot.pages.push_back(page);
  }
  archive.restored = restored;
  return archive;
}

void Add(WorkspaceStructureState* state, sync::TabArchiveEntryRecord record) {
  const base::Uuid id = record.id;
  state->entries[id].record = std::move(record);
}

// Split archive journey on build 58: "Am ursprünglichen Ort
// wiederherstellen" restored both split members as unloaded temporary
// pages, and the sidebar hid them as closed orphans. The restored pages of
// a live entry must stay identifiable; archived, deleted entries must not.
TEST(WorkspaceStructureStateTest, RestoredArchivePagesAreListed) {
  const base::Uuid left = base::Uuid::GenerateRandomV4();
  const base::Uuid right = base::Uuid::GenerateRandomV4();
  const base::Uuid still_archived = base::Uuid::GenerateRandomV4();
  const base::Uuid deleted = base::Uuid::GenerateRandomV4();
  WorkspaceStructureState state;
  Add(&state, MakeArchive({left, right}, /*restored=*/true));
  Add(&state, MakeArchive({still_archived}, /*restored=*/false));
  sync::TabArchiveEntryRecord gone = MakeArchive({deleted}, true);
  gone.tombstone = true;
  Add(&state, std::move(gone));

  EXPECT_EQ((std::set<base::Uuid>{left, right}), RestoredArchivePageIds(state));
  EXPECT_TRUE(RestoredArchivePageIds(WorkspaceStructureState()).empty());
}

}  // namespace
}  // namespace ahoi::session
