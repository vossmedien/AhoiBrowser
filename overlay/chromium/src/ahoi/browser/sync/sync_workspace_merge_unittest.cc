// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// A merged Workspace's tombstone names its target (`merged_into`, ADR 0012,
// crest 084): codec, validation, field-group merge and the re-homing of
// nodes that reach the merged Workspace later.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

constexpr char kDevice[] = "10000000-0000-4000-8000-00000000d00a";

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

SyncVersion Version(int64_t offset) {
  return SyncVersion{
      .model_version = kCurrentModelVersion,
      .stamp = HlcStamp{.physical_time_us = kMinimumSyncClockPhysicalUs + offset,
                        .device_tiebreak = kDevice}};
}

base::Time Ts(int64_t offset) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(kMinimumSyncClockPhysicalUs + offset));
}

WorkspaceRecord Workspace(const char* id, const char* name, int64_t offset) {
  return WorkspaceRecord{.id = Id(id),
                         .name = name,
                         .sort_key = name,
                         .created_at = Ts(1),
                         .modified_at = Ts(offset),
                         .version = Version(offset)};
}

TreeNodeRecord Page(const char* id, const base::Uuid& workspace) {
  return TreeNodeRecord{.id = Id(id),
                        .workspace_id = workspace,
                        .kind = TreeNodeKind::kPage,
                        .title = "Offline page",
                        .url = "https://example.test/offline",
                        .sort_key = "m",
                        .created_at = Ts(1),
                        .modified_at = Ts(5),
                        .version = Version(5),
                        .target_kind = SharedTabTargetKind::kWeb};
}

constexpr char kSource[] = "60000000-0000-4000-8000-000000000001";
constexpr char kTarget[] = "60000000-0000-4000-8000-000000000002";
constexpr char kMiddle[] = "60000000-0000-4000-8000-000000000003";
constexpr char kNode[] = "60000000-0000-4000-8000-000000000010";
constexpr char kParent[] = "60000000-0000-4000-8000-000000000011";

TreeNodeRecord Folder(const base::Uuid& workspace) {
  TreeNodeRecord folder = Page(kParent, workspace);
  folder.kind = TreeNodeKind::kFolder;
  folder.url.clear();
  folder.target_kind.reset();
  return folder;
}

TEST(SyncWorkspaceMergeTest, MergeTargetRoundTripsWithItsTombstone) {
  WorkspaceRecord merged = Workspace(kSource, "Source", 10);
  merged.tombstone = true;
  merged.merged_into = Id(kTarget);
  ASSERT_TRUE(ValidateRecord(merged));
  std::string payload;
  ASSERT_TRUE(SerializeRecord(merged, &payload));
  EXPECT_NE(payload.find("\"merged_into\":\"" + std::string(kTarget) + "\""),
            std::string::npos);
  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kWorkspace, payload, &decoded));
  EXPECT_EQ(std::get<WorkspaceRecord>(decoded).merged_into, Id(kTarget));

  // Older peers never write the field; it decodes as "not merged".
  WorkspaceRecord plain = Workspace(kSource, "Source", 10);
  ASSERT_TRUE(SerializeRecord(plain, &payload));
  EXPECT_EQ(payload.find("merged_into"), std::string::npos);
  ASSERT_TRUE(DeserializeRecord(EntityType::kWorkspace, payload, &decoded));
  EXPECT_FALSE(std::get<WorkspaceRecord>(decoded).merged_into);
}

TEST(SyncWorkspaceMergeTest, MergeTargetOnlyAccompaniesATombstone) {
  WorkspaceRecord live = Workspace(kSource, "Source", 10);
  live.merged_into = Id(kTarget);
  EXPECT_FALSE(ValidateRecord(live));
  WorkspaceRecord self = Workspace(kSource, "Source", 10);
  self.tombstone = true;
  self.merged_into = Id(kSource);
  EXPECT_FALSE(ValidateRecord(self));
}

TEST(SyncWorkspaceMergeTest, TombstoneFieldGroupCarriesTheTarget) {
  SyncRecord existing = Workspace(kSource, "Source", 10);
  ASSERT_TRUE(StampLocalMutation(nullptr, &existing));
  WorkspaceRecord merge = std::get<WorkspaceRecord>(existing);
  merge.tombstone = true;
  merge.merged_into = Id(kTarget);
  merge.version = Version(20);
  SyncRecord incoming = merge;
  ASSERT_TRUE(StampLocalMutation(&existing, &incoming));

  SyncRecord merged;
  const MergeDecision decision =
      MergeRecordFields(existing, incoming, &merged);
  ASSERT_NE(decision, MergeDecision::kInvalid);
  const WorkspaceRecord& result = std::get<WorkspaceRecord>(merged);
  EXPECT_TRUE(result.tombstone);
  EXPECT_EQ(result.merged_into, Id(kTarget));

  // An undo that revives the Workspace clears the target in the same group.
  WorkspaceRecord revive = result;
  revive.tombstone = false;
  revive.merged_into.reset();
  revive.version = Version(30);
  SyncRecord revival = revive;
  ASSERT_TRUE(StampLocalMutation(&merged, &revival));
  SyncRecord after;
  ASSERT_NE(MergeRecordFields(merged, revival, &after), MergeDecision::kInvalid);
  EXPECT_FALSE(std::get<WorkspaceRecord>(after).tombstone);
  EXPECT_FALSE(std::get<WorkspaceRecord>(after).merged_into);
}

bool HasRecoveryFolder(const tab_tree::TabTreeSnapshot& snapshot) {
  return std::ranges::any_of(snapshot.nodes,
                             [](const tab_tree::TreeNode& node) {
                               return node.title == u"Wiederhergestellt";
                             });
}

TEST(SyncWorkspaceMergeTest, NodeInAMergedWorkspaceFollowsTheMerge) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  std::optional<tab_tree::TabTreeSnapshot> applied = ReconcileTabTreeRecords(
      {}, {source, target}, {Page(kNode, Id(kSource))});
  ASSERT_TRUE(applied);
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_FALSE(node->parent_id);
  EXPECT_FALSE(HasRecoveryFolder(*applied));
}

TEST(SyncWorkspaceMergeTest, NodeFollowsAChainOfMerges) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kMiddle);
  WorkspaceRecord middle = Workspace(kMiddle, "Middle", 10);
  middle.tombstone = true;
  middle.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  std::optional<tab_tree::TabTreeSnapshot> applied = ReconcileTabTreeRecords(
      {}, {source, middle, target}, {Page(kNode, Id(kSource))});
  ASSERT_TRUE(applied);
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_FALSE(HasRecoveryFolder(*applied));
}

TEST(SyncWorkspaceMergeTest, WithoutALiveTargetTheRecoveryStillApplies) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kMiddle);  // Unknown on this device.
  const WorkspaceRecord other = Workspace(kTarget, "Other", 10);
  std::optional<tab_tree::TabTreeSnapshot> applied = ReconcileTabTreeRecords(
      {}, {source, other}, {Page(kNode, Id(kSource))});
  ASSERT_TRUE(applied);
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_TRUE(HasRecoveryFolder(*applied));
}

TEST(SyncWorkspaceMergeTest, MissingOrInvalidParentKeepsLatePageAtMergeTargetRoot) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  const WorkspaceRecord other = Workspace(kMiddle, "Other", 10);
  TreeNodeRecord page = Page(kNode, Id(kSource));
  page.parent_id = Id(kParent);

  // Missing parent; deleted parent; parent in another Workspace; non-folder.
  for (int parent_case = 0; parent_case < 4; ++parent_case) {
    SCOPED_TRACE(parent_case);
    std::vector<TreeNodeRecord> nodes = {page};
    if (parent_case != 0) {
      TreeNodeRecord parent = Folder(Id(kTarget));
      if (parent_case == 1)
        parent.tombstone = true;
      if (parent_case == 2)
        parent.workspace_id = Id(kMiddle);
      if (parent_case == 3)
        parent = Page(kParent, Id(kTarget));
      nodes.push_back(parent);
    }
    const auto applied =
        ReconcileTabTreeRecords({}, {source, target, other}, nodes);
    ASSERT_TRUE(applied);
    const auto node =
        std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
    ASSERT_NE(node, applied->nodes.end());
    EXPECT_EQ(node->workspace_id, Id(kTarget));
    EXPECT_FALSE(node->parent_id);
    EXPECT_FALSE(HasRecoveryFolder(*applied));
  }
}

TEST(SyncWorkspaceMergeTest, ValidParentSurvivesBeforeOrTogetherWithLateChild) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  TreeNodeRecord page = Page(kNode, Id(kSource));
  page.parent_id = Id(kParent);
  for (const auto& parent_workspace : {Id(kSource), Id(kTarget)}) {
    const TreeNodeRecord parent = Folder(parent_workspace);
    for (const auto& nodes : {std::vector<TreeNodeRecord>{page, parent},
                             std::vector<TreeNodeRecord>{parent, page}}) {
      const auto applied = ReconcileTabTreeRecords({}, {source, target}, nodes);
      ASSERT_TRUE(applied);
      const auto node =
          std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
      ASSERT_NE(node, applied->nodes.end());
      EXPECT_EQ(node->workspace_id, Id(kTarget));
      EXPECT_EQ(node->parent_id, Id(kParent));
      EXPECT_FALSE(HasRecoveryFolder(*applied));
    }
  }
}

TEST(SyncWorkspaceMergeTest, CompactionRetainsOnlyTheRouteAndPreservesRawPage) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  const TreeNodeRecord page = Page(kNode, Id(kSource));
  ASSERT_EQ(store.PutLocalRecord(source, "merge"), SyncStore::Result::kOk);
  ASSERT_EQ(store.PutLocalRecord(target, "target"), SyncStore::Result::kOk);
  ASSERT_EQ(store.PutLocalRecord(page, "page"), SyncStore::Result::kOk);
  // PutLocalRecord normalizes the field map. Compare against the actual
  // stored wire authority, not the unstamped input helper's empty map.
  SyncRecord page_before;
  ASSERT_EQ(store.GetRecord(EntityType::kTreeNode, Id(kNode), &page_before),
            SyncStore::Result::kOk);
  const TreeNodeRecord stored_page_before = std::get<TreeNodeRecord>(page_before);
  std::string bytes_before;
  ASSERT_TRUE(SerializeRecord(page_before, &bytes_before));
  const base::Time later = base::Time::Now() + base::Days(31);
  std::map<base::Uuid, base::Uuid> routes;

  // The pending source outbox still prevents both compaction and a route row.
  ASSERT_EQ(store.CompactExpiredTombstones(later, base::Days(30)),
            SyncStore::Result::kOk);
  SyncRecord record;
  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, Id(kSource), &record),
            SyncStore::Result::kOk);
  ASSERT_EQ(store.ReadCompactedWorkspaceMergeTargets(&routes),
            SyncStore::Result::kOk);
  EXPECT_TRUE(routes.empty());

  ASSERT_EQ(store.AcknowledgeOutbox({"merge"}), SyncStore::Result::kOk);
  ASSERT_EQ(store.CompactExpiredTombstones(later, base::Days(30)),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.GetRecord(EntityType::kWorkspace, Id(kSource), &record),
            SyncStore::Result::kNotFound);
  ASSERT_EQ(store.ReadCompactedWorkspaceMergeTargets(&routes),
            SyncStore::Result::kOk);
  ASSERT_EQ(routes.size(), 1u);
  EXPECT_EQ(routes.at(Id(kSource)), Id(kTarget));
  ASSERT_EQ(store.GetRecord(EntityType::kTreeNode, Id(kNode), &record),
            SyncStore::Result::kOk);
  const auto& raw_page = std::get<TreeNodeRecord>(record);
  EXPECT_EQ(raw_page.workspace_id, Id(kSource));
  EXPECT_EQ(raw_page, stored_page_before);  // Includes every field clock.
  std::string bytes_after;
  ASSERT_TRUE(SerializeRecord(record, &bytes_after));
  EXPECT_EQ(bytes_after, bytes_before);

  const auto applied = ReconcileTabTreeRecords({}, {target}, {raw_page}, routes);
  ASSERT_TRUE(applied);
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_FALSE(HasRecoveryFolder(*applied));
}

TEST(SyncWorkspaceMergeTest, CompactedSourceIsNotResurrectedAsNativeFallback) {
  tab_tree::TabTreeSnapshot native;
  native.workspaces.push_back({.id = Id(kSource),
                               .name = u"Old source",
                               .sort_key = "a",
                               .created_at = Ts(1),
                               .modified_at = Ts(10)});
  const std::map<base::Uuid, base::Uuid> routes = {
      {Id(kSource), Id(kTarget)}};
  // No authoritative live Workspace is available yet. The stale native
  // source is known compacted and cannot override its retained merge route.
  EXPECT_FALSE(ReconcileTabTreeRecords(
      native, {}, {Page(kNode, Id(kSource))}, routes));
}

TEST(SyncWorkspaceMergeTest, NativeFallbackSkipsCompactedSourceForKnownTarget) {
  tab_tree::TabTreeSnapshot native;
  native.workspaces.push_back({.id = Id(kSource),
                               .name = u"Old source",
                               .sort_key = "a",
                               .created_at = Ts(1),
                               .modified_at = Ts(10)});
  native.workspaces.push_back({.id = Id(kTarget),
                               .name = u"Known target",
                               .sort_key = "b",
                               .created_at = Ts(1),
                               .modified_at = Ts(10)});
  const std::map<base::Uuid, base::Uuid> routes = {
      {Id(kSource), Id(kTarget)}};
  const auto applied = ReconcileTabTreeRecords(
      native, {}, {Page(kNode, Id(kSource))}, routes);
  ASSERT_TRUE(applied);
  EXPECT_EQ(std::ranges::find(applied->workspaces, Id(kSource),
                             &tab_tree::Workspace::id),
            applied->workspaces.end());
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_FALSE(HasRecoveryFolder(*applied));
}

TEST(SyncWorkspaceMergeTest, CompactedMergeChainSurvivesStoreReopen) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("merge.sqlite");
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    WorkspaceRecord source = Workspace(kSource, "Source", 10);
    source.tombstone = true;
    source.merged_into = Id(kMiddle);
    WorkspaceRecord middle = Workspace(kMiddle, "Middle", 10);
    middle.tombstone = true;
    middle.merged_into = Id(kTarget);
    ASSERT_EQ(store.PutLocalRecord(source, "first"), SyncStore::Result::kOk);
    ASSERT_EQ(store.PutLocalRecord(middle, "second"), SyncStore::Result::kOk);
    ASSERT_EQ(store.PutLocalRecord(Workspace(kTarget, "Target", 10), "target"),
              SyncStore::Result::kOk);
    ASSERT_EQ(store.AcknowledgeOutbox({"first", "second"}),
              SyncStore::Result::kOk);
    ASSERT_EQ(store.CompactExpiredTombstones(base::Time::Now() + base::Days(31),
                                            base::Days(30)),
              SyncStore::Result::kOk);
  }
  SyncStore reopened;
  ASSERT_TRUE(reopened.Initialize(path));
  std::map<base::Uuid, base::Uuid> routes;
  ASSERT_EQ(reopened.ReadCompactedWorkspaceMergeTargets(&routes),
            SyncStore::Result::kOk);
  ASSERT_EQ(routes.size(), 2u);
  SyncRecord target_record;
  ASSERT_EQ(reopened.GetRecord(EntityType::kWorkspace, Id(kTarget), &target_record),
            SyncStore::Result::kOk);
  const auto& target = std::get<WorkspaceRecord>(target_record);
  const auto applied = ReconcileTabTreeRecords(
      {}, {target}, {Page(kNode, Id(kSource))}, routes);
  ASSERT_TRUE(applied);
  const auto node =
      std::ranges::find(applied->nodes, Id(kNode), &tab_tree::TreeNode::id);
  ASSERT_NE(node, applied->nodes.end());
  EXPECT_EQ(node->workspace_id, Id(kTarget));
  EXPECT_FALSE(HasRecoveryFolder(*applied));

  // A corrupt/cyclic route never fabricates a live merge destination.
  routes[Id(kMiddle)] = Id(kSource);
  const auto cycle = ReconcileTabTreeRecords(
      {}, {target}, {Page(kNode, Id(kSource))}, routes);
  ASSERT_TRUE(cycle);
  EXPECT_TRUE(HasRecoveryFolder(*cycle));
}

TEST(SyncWorkspaceMergeTest, TailProjectionPreservesExplicitMovesAndRebases) {
  WorkspaceRecord source = Workspace(kSource, "Source", 10);
  source.tombstone = true;
  source.merged_into = Id(kTarget);
  const WorkspaceRecord target = Workspace(kTarget, "Target", 10);
  TreeNodeRecord kept = Page(kParent, Id(kTarget));
  kept.sort_key = "Z";
  TreeNodeRecord first = Page(kNode, Id(kSource));
  first.sort_key = "A";
  TreeNodeRecord second = Page("60000000-0000-4000-8000-000000000012",
                               Id(kSource));
  second.sort_key = "B";
  const auto roots = [](const tab_tree::TabTreeSnapshot& view,
                        const base::Uuid& workspace) {
    std::vector<const tab_tree::TreeNode*> ordered;
    for (const auto& node : view.nodes) {
      if (!node.tombstone && !node.parent_id && node.workspace_id == workspace)
        ordered.push_back(&node);
    }
    std::ranges::sort(ordered, [](const auto* a, const auto* b) {
      return std::tie(a->sort_key, a->id) < std::tie(b->sort_key, b->id);
    });
    std::vector<base::Uuid> ids;
    for (const auto* node : ordered)
      ids.push_back(node->id);
    return ids;
  };
  auto view = ReconcileTabTreeRecords({}, {source, target}, {kept, first, second});
  ASSERT_TRUE(view);
  EXPECT_EQ(roots(*view, target.id),
            (std::vector<base::Uuid>{kept.id, first.id, second.id}));

  const auto last =
      std::ranges::find(view->nodes, second.id, &tab_tree::TreeNode::id);
  ASSERT_NE(last, view->nodes.end());
  SyncRecord before = first;
  ASSERT_TRUE(NormalizeFieldVersions(&before));
  first = std::get<TreeNodeRecord>(before);
  first.workspace_id = target.id;
  first.sort_key = last->sort_key + "@";  // Native append-after operation.
  first.version = Version(20);
  first.modified_at = Ts(20);
  SyncRecord moved = first;
  ASSERT_TRUE(StampLocalMutation(&before, &moved));
  first = std::get<TreeNodeRecord>(moved);
  view = ReconcileTabTreeRecords({}, {source, target}, {kept, first, second});
  ASSERT_TRUE(view);
  EXPECT_EQ(roots(*view, target.id),
            (std::vector<base::Uuid>{kept.id, second.id, first.id}));

  // Move the only ordinary target root after the segment. The remaining
  // passive node cannot repeatedly overtake these explicit positions.
  const auto moved_node =
      std::ranges::find(view->nodes, first.id, &tab_tree::TreeNode::id);
  ASSERT_NE(moved_node, view->nodes.end());
  SyncRecord kept_before = kept;
  ASSERT_TRUE(NormalizeFieldVersions(&kept_before));
  kept = std::get<TreeNodeRecord>(kept_before);
  kept.sort_key = moved_node->sort_key + "@";
  kept.version = Version(30);
  kept.modified_at = Ts(30);
  SyncRecord kept_after = kept;
  ASSERT_TRUE(StampLocalMutation(&kept_before, &kept_after));
  kept = std::get<TreeNodeRecord>(kept_after);
  view = ReconcileTabTreeRecords({}, {source, target}, {kept, first, second});
  ASSERT_TRUE(view);
  EXPECT_EQ(roots(*view, target.id),
            (std::vector<base::Uuid>{second.id, first.id, kept.id}));

  // Reviving the source affects only the still-passive Page. Explicitly
  // moved first remains in B and its new location register is unchanged.
  SyncRecord source_before = source;
  ASSERT_TRUE(NormalizeFieldVersions(&source_before));
  source = std::get<WorkspaceRecord>(source_before);
  source.tombstone = false;
  source.merged_into.reset();
  source.version = Version(40);
  source.modified_at = Ts(40);
  SyncRecord source_after = source;
  ASSERT_TRUE(StampLocalMutation(&source_before, &source_after));
  source = std::get<WorkspaceRecord>(source_after);
  const auto first_before = first;
  view = ReconcileTabTreeRecords({}, {source, target}, {kept, first, second});
  ASSERT_TRUE(view);
  EXPECT_EQ(roots(*view, target.id),
            (std::vector<base::Uuid>{first.id, kept.id}));
  EXPECT_EQ(roots(*view, source.id), (std::vector<base::Uuid>{second.id}));
  EXPECT_EQ(first, first_before);
}

}  // namespace
}  // namespace ahoi::sync
