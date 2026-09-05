// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_journal.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Result = SyncStore::Result;

constexpr char kLocalDevice[] = "94000000-0000-4000-8000-000000000001";
constexpr char kPeerDevice[] = "94000000-0000-4000-8000-000000000002";
constexpr int64_t kCreatedAt = 11644473601000000LL;

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("93000000-0000-4000-8000-%012x", value));
}

std::string Key(unsigned value, bool account = false) {
  return NativeBookmarkKey(Id(value), account);
}

NativeBookmarkEntry Page(unsigned value, size_t index = 0) {
  return {.native_key = Key(value),
          .root = BookmarkRoot::kOther,
          .kind = BookmarkKind::kUrl,
          .index = index,
          .title = base::StringPrintf("Native page %u", value),
          .url = base::StringPrintf("https://example.test/bookmark/%u", value),
          .created_at = base::Time::FromDeltaSinceWindowsEpoch(
              base::Microseconds(kCreatedAt + value))};
}

NativeBookmarkEntry Folder(unsigned value, size_t index = 0) {
  auto entry = Page(value, index);
  entry.kind = BookmarkKind::kFolder;
  entry.url.clear();
  return entry;
}

NativeBookmarkEntry Under(NativeBookmarkEntry entry,
                          const std::string& parent) {
  entry.root.reset();
  entry.parent_key = parent;
  return entry;
}

base::Uuid LogicalId(const BookmarkSyncProjection& projection,
                     const std::string& native_key) {
  const auto found = std::ranges::find(projection.bindings, native_key,
                                       &BookmarkNativeBinding::native_key);
  EXPECT_NE(projection.bindings.end(), found);
  return found == projection.bindings.end() ? base::Uuid() : found->logical_id;
}

BookmarkRecord ReadBookmark(SyncStore& store, const base::Uuid& id) {
  SyncRecord value;
  EXPECT_EQ(Result::kOk, store.GetRecord(EntityType::kBookmark, id, &value));
  const auto* bookmark = std::get_if<BookmarkRecord>(&value);
  EXPECT_NE(nullptr, bookmark);
  return bookmark ? *bookmark : BookmarkRecord();
}

std::vector<SyncChange> ReadOutbox(SyncStore& store) {
  std::vector<SyncChange> changes;
  EXPECT_EQ(Result::kOk, store.ReadOutbox(100, &changes));
  return changes;
}

void AcknowledgeOutbox(SyncStore& store) {
  std::vector<std::string> ids;
  for (const auto& change : ReadOutbox(store)) {
    ids.push_back(change.mutation_id);
  }
  ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox(ids));
}

SyncChange ChangeFor(const BookmarkRecord& record, const char* mutation) {
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {.mutation_id = mutation,
          .entity_type = EntityType::kBookmark,
          .entity_id = record.id,
          .kind = record.tombstone ? ChangeKind::kDelete : ChangeKind::kUpsert,
          .version = record.version,
          .payload = std::move(payload)};
}

BookmarkRecord PeerRename(BookmarkRecord record) {
  record.title = "Renamed on mobile 海";
  record.version.stamp.physical_time_us += 1000000;
  record.version.stamp.logical = 0;
  record.version.stamp.device_tiebreak = kPeerDevice;
  record.field_versions.insert_or_assign("title", record.version.stamp);
  return record;
}

BookmarkRecord RemotePage(unsigned value = 99) {
  const auto entry = Page(value);
  return {.id = Id(value),
          .kind = entry.kind,
          .root_kind = entry.root,
          .sort_key = "remote-order",
          .title = entry.title,
          .url = entry.url,
          .created_at = entry.created_at,
          .version = {.stamp = {.physical_time_us = kCreatedAt + 100,
                                .device_tiebreak = kPeerDevice}}};
}

class BookmarkSyncJournalTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(store_.InitializeInMemory()); }

  SyncStore store_;
  BookmarkSyncJournal journal_{&store_};
  HybridLogicalClock clock_{kLocalDevice};
};

TEST_F(BookmarkSyncJournalTest, SeedsNativeHierarchyAndMergedRootOrder) {
  const auto folder = Folder(1, 1);
  auto account_page = Page(2);
  account_page.native_key = Key(2, true);
  const auto child = Under(Page(3), folder.native_key);
  auto bar_page = Page(4);
  bar_page.root = BookmarkRoot::kBookmarkBar;
  const NativeBookmarkSnapshot snapshot{
      .entries = {child, folder, bar_page, account_page}};
  const auto projection = journal_.ReconcileLocal(snapshot, &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(4u, projection->records.size());
  ASSERT_EQ(4u, projection->bindings.size());
  const auto folder_id = LogicalId(*projection, folder.native_key);
  EXPECT_EQ(InitialBookmarkSyncId(folder.native_key), folder_id);
  const auto saved_child = ReadBookmark(store_, LogicalId(*projection, Key(3)));
  EXPECT_EQ(folder_id, saved_child.parent_id);
  EXPECT_FALSE(saved_child.root_kind);
  EXPECT_EQ(child.created_at, saved_child.created_at);
  const auto saved_account =
      ReadBookmark(store_, LogicalId(*projection, account_page.native_key));
  EXPECT_LT(saved_account.sort_key, ReadBookmark(store_, folder_id).sort_key);
  EXPECT_EQ(BookmarkRoot::kOther, saved_account.root_kind);
  EXPECT_EQ(BookmarkRoot::kBookmarkBar,
            ReadBookmark(store_, LogicalId(*projection, Key(4))).root_kind);
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(4u, changes.size());
  for (const auto& change : changes) {
    EXPECT_EQ(EntityType::kBookmark, change.entity_type);
    EXPECT_EQ(ChangeKind::kUpsert, change.kind);
    EXPECT_EQ(kLocalDevice, change.version.stamp.device_tiebreak);
    EXPECT_TRUE(base::Uuid::ParseLowercase(change.mutation_id).is_valid());
    EXPECT_TRUE(
        HasCompleteFieldVersions(ReadBookmark(store_, change.entity_id)));
  }
}

TEST_F(BookmarkSyncJournalTest, IdenticalSnapshotsKeepVersionAndOutbox) {
  const NativeBookmarkSnapshot snapshot{.entries = {Page(1)}};
  const auto initial = journal_.ReconcileLocal(snapshot, &clock_);
  ASSERT_TRUE(initial);
  const auto id = LogicalId(*initial, Key(1));
  const auto before = ReadBookmark(store_, id);
  const auto queued = ReadOutbox(store_);
  ASSERT_EQ(1u, queued.size());
  const HlcStamp clock_before = clock_.last();
  ASSERT_TRUE(journal_.ReconcileLocal(snapshot, &clock_));
  EXPECT_EQ(before, ReadBookmark(store_, id));
  EXPECT_EQ(clock_before, clock_.last());
  const auto repeated = ReadOutbox(store_);
  ASSERT_EQ(1u, repeated.size());
  EXPECT_EQ(queued[0].mutation_id, repeated[0].mutation_id);
  EXPECT_EQ(queued[0].payload, repeated[0].payload);
  AcknowledgeOutbox(store_);
  ASSERT_TRUE(journal_.ReconcileLocal(snapshot, &clock_));
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, LocalAndAccountUuidCollisionHasDistinctIds) {
  const auto local = Page(1);
  auto account = local;
  account.native_key = Key(1, true);
  account.index = 1;
  const auto projection =
      journal_.ReconcileLocal({.entries = {local, account}}, &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(2u, projection->records.size());
  const auto local_id = LogicalId(*projection, local.native_key);
  const auto account_id = LogicalId(*projection, account.native_key);
  EXPECT_NE(local_id, account_id);
  EXPECT_EQ(InitialBookmarkSyncId(local.native_key), local_id);
  EXPECT_EQ(InitialBookmarkSyncId(account.native_key), account_id);
  EXPECT_EQ(2, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, NativeCloneGetsNewIdentityDespiteEqualContent) {
  const auto original = Page(1);
  const auto initial =
      journal_.ReconcileLocal({.entries = {original}}, &clock_);
  ASSERT_TRUE(initial);
  const auto original_id = LogicalId(*initial, original.native_key);
  AcknowledgeOutbox(store_);
  auto clone = original;
  clone.native_key = Key(2);
  clone.index = 1;
  const auto projection =
      journal_.ReconcileLocal({.entries = {clone, original}}, &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(2u, projection->records.size());
  EXPECT_EQ(original_id, LogicalId(*projection, original.native_key));
  const auto clone_id = LogicalId(*projection, clone.native_key);
  EXPECT_NE(original_id, clone_id);
  EXPECT_EQ(original.title, ReadBookmark(store_, clone_id).title);
  EXPECT_EQ(original.url, ReadBookmark(store_, clone_id).url);
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(1u, changes.size());
  EXPECT_EQ(clone_id, changes[0].entity_id);
}

TEST_F(BookmarkSyncJournalTest, ObservedStorageMoveRetainsLogicalIdentity) {
  const auto original = Page(1);
  const auto initial =
      journal_.ReconcileLocal({.entries = {original}}, &clock_);
  ASSERT_TRUE(initial);
  const auto id = LogicalId(*initial, original.native_key);
  AcknowledgeOutbox(store_);
  auto moved = original;
  moved.previous_native_key = original.native_key;
  moved.native_key = Key(2, true);
  moved.root = BookmarkRoot::kMobile;
  NativeBookmarkSnapshot snapshot{.entries = {moved},
                                  .removed_keys = {original.native_key}};
  const auto projection = journal_.ReconcileLocal(snapshot, &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->records.size());
  EXPECT_EQ(id, LogicalId(*projection, moved.native_key));
  EXPECT_EQ(id, LogicalId(*projection, original.native_key));
  EXPECT_EQ(BookmarkRoot::kMobile, ReadBookmark(store_, id).root_kind);
  EXPECT_FALSE(ReadBookmark(store_, id).tombstone);
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(1u, changes.size());
  EXPECT_EQ(id, changes[0].entity_id);
  EXPECT_EQ(ChangeKind::kUpsert, changes[0].kind);
  snapshot.entries[0].previous_native_key.reset();
  snapshot.removed_keys.clear();
  ASSERT_TRUE(journal_.ReconcileLocal(snapshot, &clock_));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, FolderStorageMoveKeepsDescendantIdentity) {
  const auto folder = Folder(1);
  const auto child = Under(Page(2), folder.native_key);
  const auto initial =
      journal_.ReconcileLocal({.entries = {folder, child}}, &clock_);
  ASSERT_TRUE(initial);
  const auto folder_id = LogicalId(*initial, folder.native_key);
  const auto child_id = LogicalId(*initial, child.native_key);
  const auto child_before = ReadBookmark(store_, child_id);
  AcknowledgeOutbox(store_);
  auto moved_folder = folder;
  moved_folder.previous_native_key = folder.native_key;
  moved_folder.native_key = Key(3, true);
  moved_folder.root = BookmarkRoot::kBookmarkBar;
  auto moved_child = child;
  moved_child.previous_native_key = child.native_key;
  moved_child.native_key = Key(4, true);
  moved_child.parent_key = moved_folder.native_key;
  const auto projection = journal_.ReconcileLocal(
      {.entries = {moved_child, moved_folder},
       .removed_keys = {folder.native_key, child.native_key}},
      &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(2u, projection->records.size());
  EXPECT_EQ(folder_id, LogicalId(*projection, moved_folder.native_key));
  EXPECT_EQ(child_id, LogicalId(*projection, moved_child.native_key));
  EXPECT_EQ(child_before, ReadBookmark(store_, child_id));
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(1u, changes.size());
  EXPECT_EQ(folder_id, changes[0].entity_id);
}

TEST_F(BookmarkSyncJournalTest, RejectsSimultaneousNodesClaimingOneAlias) {
  const auto original = Page(1);
  ASSERT_TRUE(journal_.ReconcileLocal({.entries = {original}}, &clock_));
  auto moved = original;
  moved.native_key = Key(2, true);
  moved.previous_native_key = original.native_key;
  ASSERT_TRUE(journal_.ReconcileLocal({.entries = {moved}}, &clock_));
  AcknowledgeOutbox(store_);
  moved.previous_native_key.reset();
  moved.index = 1;
  EXPECT_FALSE(
      journal_.ReconcileLocal({.entries = {original, moved}}, &clock_));
  const auto projection = journal_.ReadProjection();
  ASSERT_TRUE(projection);
  EXPECT_EQ(1u, projection->records.size());
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, RemoteTitleAndLocalUrlPreserveBothFieldEdits) {
  NativeBookmarkSnapshot snapshot{.entries = {Page(1)}};
  const auto initial = journal_.ReconcileLocal(snapshot, &clock_);
  ASSERT_TRUE(initial);
  const auto id = LogicalId(*initial, Key(1));
  AcknowledgeOutbox(store_);
  const auto remote = PeerRename(ReadBookmark(store_, id));
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(remote, "peer-title")}}));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  snapshot.entries[0].url = "https://example.test/locally-edited";
  ASSERT_TRUE(journal_.ReconcileLocal(snapshot, &clock_));
  const auto merged = ReadBookmark(store_, id);
  EXPECT_EQ(remote.title, merged.title);
  EXPECT_EQ(snapshot.entries[0].url, merged.url);
  EXPECT_EQ(remote.field_versions.at("title"),
            merged.field_versions.at("title"));
  EXPECT_EQ(kLocalDevice, merged.field_versions.at("url").device_tiebreak);
  EXPECT_EQ(1, store_.PendingOutboxCount());
  snapshot.entries[0].title = merged.title;
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(snapshot));
  ASSERT_TRUE(journal_.ReconcileLocal(snapshot, &clock_));
  EXPECT_EQ(merged, ReadBookmark(store_, id));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, NativeAcknowledgementNeverEchoesRemoteRecords) {
  const auto remote = RemotePage();
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(remote, "peer-new")}}));
  const auto before = ReadBookmark(store_, remote.id);
  const auto projection = journal_.ReadProjection();
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->bindings.size());
  auto native = Page(99);
  native.native_key = projection->bindings[0].native_key;
  const NativeBookmarkSnapshot snapshot{.entries = {native}};
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(snapshot));
  EXPECT_EQ(before, ReadBookmark(store_, remote.id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  const auto repeated = journal_.ReconcileLocal(snapshot, &clock_);
  ASSERT_TRUE(repeated);
  EXPECT_EQ(remote.id, LogicalId(*repeated, native.native_key));
  EXPECT_EQ(before, ReadBookmark(store_, remote.id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, PlannedRemoteApplyBeforeAckKeepsIdentity) {
  const auto remote = RemotePage();
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(remote, "peer-new")}}));
  const auto planned = journal_.ReadProjection();
  ASSERT_TRUE(planned);
  ASSERT_EQ(1u, planned->bindings.size());
  auto native = Page(99);
  native.native_key = planned->bindings[0].native_key;
  const auto observed = journal_.ReconcileLocal({.entries = {native}}, &clock_);
  ASSERT_TRUE(observed);
  EXPECT_EQ(remote.id, LogicalId(*observed, native.native_key));
  EXPECT_EQ(1u, observed->records.size());
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, MissingAndIncompleteSnapshotsNeverDelete) {
  const auto folder = Folder(1);
  const auto child = Under(Page(2), folder.native_key);
  auto account = Page(3, 1);
  account.native_key = Key(3, true);
  const NativeBookmarkSnapshot complete{.entries = {folder, child, account}};
  ASSERT_TRUE(journal_.ReconcileLocal(complete, &clock_));
  AcknowledgeOutbox(store_);
  ASSERT_TRUE(journal_.ReconcileLocal({.entries = {child}}, &clock_));
  const auto absent = journal_.ReconcileLocal({}, &clock_);
  ASSERT_TRUE(absent);
  ASSERT_EQ(3u, absent->records.size());
  for (const auto& record : absent->records) {
    EXPECT_FALSE(record.tombstone);
  }
  ASSERT_TRUE(journal_.ReconcileLocal(complete, &clock_));
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, ExplicitSubtreeRemovalDeletesEveryCapturedKey) {
  const auto folder = Folder(1);
  const auto nested = Under(Folder(2), folder.native_key);
  const auto child = Under(Page(3), nested.native_key);
  auto survivor = Page(4, 1);
  const auto initial = journal_.ReconcileLocal(
      {.entries = {folder, nested, child, survivor}}, &clock_);
  ASSERT_TRUE(initial);
  const std::set<base::Uuid> deleted_ids{LogicalId(*initial, Key(1)),
                                         LogicalId(*initial, Key(2)),
                                         LogicalId(*initial, Key(3))};
  const auto survivor_id = LogicalId(*initial, survivor.native_key);
  AcknowledgeOutbox(store_);
  survivor.index = 0;
  const NativeBookmarkSnapshot removed{
      .entries = {survivor}, .removed_keys = {Key(1), Key(2), Key(3)}};
  const auto projection = journal_.ReconcileLocal(removed, &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(4u, projection->records.size());
  for (const auto& id : deleted_ids) {
    EXPECT_TRUE(ReadBookmark(store_, id).tombstone);
  }
  EXPECT_FALSE(ReadBookmark(store_, survivor_id).tombstone);
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(3u, changes.size());
  for (const auto& change : changes) {
    EXPECT_TRUE(deleted_ids.contains(change.entity_id));
    EXPECT_EQ(ChangeKind::kDelete, change.kind);
  }
  ASSERT_TRUE(journal_.ReconcileLocal(removed, &clock_));
  EXPECT_EQ(3, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, UndoneAndUnknownRemovalsAreNoOps) {
  const auto page = Page(1);
  ASSERT_TRUE(journal_.ReconcileLocal({.entries = {page}}, &clock_));
  AcknowledgeOutbox(store_);
  const auto projection = journal_.ReconcileLocal(
      {.entries = {page}, .removed_keys = {page.native_key, Key(999)}},
      &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->records.size());
  EXPECT_FALSE(projection->records[0].tombstone);
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest, InvalidInitialSnapshotsLeaveNoJournalState) {
  auto both = Page(1);
  both.parent_key = Key(2);
  auto neither = Page(1);
  neither.root.reset();
  auto bad_key = Page(1);
  bad_key.native_key = "local:not-a-uuid";
  const std::vector<NativeBookmarkSnapshot> invalid{
      {.entries = {both, Folder(2, 1)}},
      {.entries = {neither}},
      {.entries = {bad_key}},
      {.entries = {Page(1), Page(1)}},
      {.entries = {Page(1), Page(2)}},
      {.entries = {Under(Page(1), Key(999))}},
      {.entries = {Page(1), Under(Page(2), Key(1))}},
      {.entries = {Under(Folder(1), Key(2)), Under(Folder(2), Key(1))}}};
  for (size_t index = 0; index < invalid.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_FALSE(journal_.ReconcileLocal(invalid[index], &clock_));
    const auto projection = journal_.ReadProjection();
    ASSERT_TRUE(projection);
    EXPECT_TRUE(projection->records.empty());
    EXPECT_TRUE(projection->bindings.empty());
    EXPECT_EQ(0, store_.PendingOutboxCount());
  }
}

TEST_F(BookmarkSyncJournalTest,
       LateKindFailureRollsBackRecordsBindingsAndOutbox) {
  const auto original = Page(2, 1);
  const auto immutable = Page(3, 2);
  const auto initial =
      journal_.ReconcileLocal({.entries = {original, immutable}}, &clock_);
  ASSERT_TRUE(initial);
  const auto original_id = LogicalId(*initial, original.native_key);
  const auto original_before = ReadBookmark(store_, original_id);
  AcknowledgeOutbox(store_);
  auto changed = original;
  changed.title = "Must roll back";
  // Native keys sort 1, 2, 3: the new row, its binding, and the existing title
  // update precede this valid-shaped but immutable-kind conflict in the SQL
  // transaction. Validation failure must undo all of them together.
  EXPECT_FALSE(journal_.ReconcileLocal(
      {.entries = {Page(1), changed, Folder(3, 2)}}, &clock_));
  EXPECT_EQ(original_before, ReadBookmark(store_, original_id));
  SyncRecord missing;
  EXPECT_EQ(Result::kNotFound,
            store_.GetRecord(EntityType::kBookmark,
                             InitialBookmarkSyncId(Key(1)), &missing));
  const auto after = journal_.ReadProjection();
  ASSERT_TRUE(after);
  EXPECT_EQ(2u, after->records.size());
  EXPECT_EQ(2u, after->bindings.size());
  EXPECT_EQ(0, store_.PendingOutboxCount());
  ASSERT_TRUE(journal_.ReconcileLocal(
      {.entries = {Page(1), original, immutable}}, &clock_));
  EXPECT_EQ(original_before, ReadBookmark(store_, original_id));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncJournalTest,
       IncompleteSnapshotCannotCommitAChildOfStoredUrl) {
  const auto parent = Page(1);
  ASSERT_TRUE(journal_.ReconcileLocal({.entries = {parent}}, &clock_));
  AcknowledgeOutbox(store_);
  EXPECT_FALSE(journal_.ReconcileLocal(
      {.entries = {Under(Page(2), parent.native_key)}}, &clock_));
  std::vector<SyncRecord> records;
  ASSERT_EQ(Result::kOk, store_.GetRecords(EntityType::kBookmark, &records));
  EXPECT_EQ(1u, records.size());
  EXPECT_EQ(0, store_.PendingOutboxCount());
  const auto projection = journal_.ReadProjection();
  ASSERT_TRUE(projection);
  EXPECT_EQ(1u, projection->bindings.size());
}

TEST_F(BookmarkSyncJournalTest,
       IncompleteSnapshotCannotCommitAStoredGraphCycle) {
  const auto parent = Folder(1);
  const auto child = Under(Folder(2), parent.native_key);
  const auto initial =
      journal_.ReconcileLocal({.entries = {parent, child}}, &clock_);
  ASSERT_TRUE(initial);
  const auto parent_id = LogicalId(*initial, parent.native_key);
  const auto before = ReadBookmark(store_, parent_id);
  AcknowledgeOutbox(store_);
  EXPECT_FALSE(journal_.ReconcileLocal(
      {.entries = {Under(parent, child.native_key)}}, &clock_));
  EXPECT_EQ(before, ReadBookmark(store_, parent_id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  EXPECT_TRUE(journal_.ReadProjection());
}

TEST_F(BookmarkSyncJournalTest, FailedAcknowledgementRollsBackEarlierBaseline) {
  const NativeBookmarkSnapshot initial_snapshot{
      .entries = {Page(1), Page(2, 1)}};
  const auto initial = journal_.ReconcileLocal(initial_snapshot, &clock_);
  ASSERT_TRUE(initial);
  const auto id = LogicalId(*initial, Key(1));
  AcknowledgeOutbox(store_);
  const auto remote = PeerRename(ReadBookmark(store_, id));
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(remote, "peer-title")}}));
  auto invalid_ack = initial_snapshot;
  invalid_ack.entries[0].title = remote.title;
  invalid_ack.entries[1].title = "Does not match the projected record";
  EXPECT_FALSE(journal_.AcknowledgeNativeProjection(invalid_ack));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  auto local = initial_snapshot;
  local.entries[0].url = "https://example.test/local-after-failed-ack";
  ASSERT_TRUE(journal_.ReconcileLocal(local, &clock_));
  const auto merged = ReadBookmark(store_, id);
  EXPECT_EQ(remote.title, merged.title);
  EXPECT_EQ(local.entries[0].url, merged.url);
}

TEST_F(BookmarkSyncJournalTest,
       RejectsUnknownParentAndMalformedAcknowledgement) {
  const auto page = Page(1);
  const auto initial = journal_.ReconcileLocal({.entries = {page}}, &clock_);
  ASSERT_TRUE(initial);
  const auto id = LogicalId(*initial, page.native_key);
  const auto before = ReadBookmark(store_, id);
  AcknowledgeOutbox(store_);
  EXPECT_FALSE(journal_.AcknowledgeNativeProjection(
      {.entries = {Under(page, Key(999))}}));
  auto both = page;
  both.parent_key = page.native_key;
  EXPECT_FALSE(journal_.AcknowledgeNativeProjection({.entries = {both}}));
  auto neither = page;
  neither.root.reset();
  EXPECT_FALSE(journal_.AcknowledgeNativeProjection({.entries = {neither}}));
  EXPECT_EQ(before, ReadBookmark(store_, id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  EXPECT_TRUE(journal_.ReadProjection());
}

TEST(BookmarkSyncJournalRestartTest,
     StorageMoveAliasesSurviveWithoutNewOutbox) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  const auto original = Page(1);
  auto moved = original;
  moved.native_key = Key(2, true);
  moved.previous_native_key = original.native_key;
  moved.root = BookmarkRoot::kMobile;
  base::Uuid expected_id;
  BookmarkRecord expected;
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    const auto seeded = journal.ReconcileLocal({.entries = {original}}, &clock);
    ASSERT_TRUE(seeded);
    expected_id = LogicalId(*seeded, original.native_key);
    ASSERT_TRUE(journal.ReconcileLocal({.entries = {moved}}, &clock));
    expected = ReadBookmark(store, expected_id);
    AcknowledgeOutbox(store);
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    moved.previous_native_key.reset();
    const auto restored = journal.ReconcileLocal({.entries = {moved}}, &clock);
    ASSERT_TRUE(restored);
    ASSERT_EQ(1u, restored->records.size());
    EXPECT_EQ(expected_id, LogicalId(*restored, original.native_key));
    EXPECT_EQ(expected_id, LogicalId(*restored, moved.native_key));
    EXPECT_EQ(expected, ReadBookmark(store, expected_id));
    EXPECT_EQ(0, store.PendingOutboxCount());
  }
}

TEST(BookmarkSyncJournalRestartTest,
     PlannedRemoteGuidSurvivesBeforeNativeApply) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  const auto remote = RemotePage();
  std::string planned_key;
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                               {.changes = {ChangeFor(remote, "peer-new")}}));
    BookmarkSyncJournal journal(&store);
    const auto planned = journal.ReadProjection();
    ASSERT_TRUE(planned);
    ASSERT_EQ(1u, planned->bindings.size());
    planned_key = planned->bindings[0].native_key;
    EXPECT_EQ(remote.id, planned->bindings[0].logical_id);
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    const auto restored = journal.ReadProjection();
    ASSERT_TRUE(restored);
    ASSERT_EQ(1u, restored->bindings.size());
    EXPECT_EQ(planned_key, restored->bindings[0].native_key);
    EXPECT_EQ(remote.id, restored->bindings[0].logical_id);
    auto native = Page(99);
    native.native_key = planned_key;
    const NativeBookmarkSnapshot snapshot{.entries = {native}};
    ASSERT_TRUE(journal.AcknowledgeNativeProjection(snapshot));
    const auto observed = journal.ReconcileLocal(snapshot, &clock);
    ASSERT_TRUE(observed);
    EXPECT_EQ(remote.id, LogicalId(*observed, planned_key));
    EXPECT_EQ(1u, observed->records.size());
    EXPECT_EQ(0, store.PendingOutboxCount());
  }
}

}  // namespace
}  // namespace ahoi::sync
