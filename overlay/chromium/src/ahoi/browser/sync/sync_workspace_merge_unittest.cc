// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// A merged Workspace's tombstone names its target (`merged_into`, ADR 0012,
// crest 084): codec, validation, field-group merge and the re-homing of
// nodes that reach the merged Workspace later.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
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

}  // namespace
}  // namespace ahoi::sync
